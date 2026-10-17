#define CONFIG_LITTLEFS_SPIFFS_COMPAT

#include <Arduino.h>
#include <NimBLEDevice.h> // github NimBLE
#include <SPI.h>
#include <Wire.h>
#include <string>
#include <esp_partition.h>

#include "SparkDataControl.h"
#include "SparkPresetControl.h"
#include "SparkStatus.h"
#include "PersistentEventLog.h"
#include "StartupDiagnostics.h"
#include "FlightDisplayHealth.h"
#ifndef HEADLESS_SERIAL_MODE
#include "SparkButtonHandler.h"
#include "SparkDisplayControl.h"
#include "SparkLEDControl.h"
#endif
#ifdef HEADLESS_SERIAL_MODE
#include "SparkSerialCLI.h"
#endif
#ifdef PANELAN_SC05X_MODE
#ifdef PANELAN_LVGL_UI_MODE
#include "PanelLanLVGLUI.h"
#include "PanelLanMiniDisplay.h"
#else
#include "PanelLanDisplay.h"
#endif
#ifdef PANELAN_LVGL_UI_MODE
#include "controller/ControllerState.h"
#include "controller/ControllerActions.h"
#include "controller/PhysicalSwitchRouting.h"
#endif
#endif

using namespace std;

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
// ESP32-S3 Arduino loop runs on core 1. This observer cannot service LVGL or
// the controller; it only detects stalled progress and writes bounded RTC records.
static void displayHealthObserver(void *) {
    FlightDisplayHealth::Observer observer;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        const uint32_t now = millis();
        observer.poll(FlightDisplayHealth::progress, now,
            [](FlightDisplayHealth::Stall state, FlightDisplayHealth::Stall previous, uint16_t age) {
                FlightRecorder::record(FlightRecorder::DisplayStall, static_cast<uint8_t>(state),
                                       state == FlightDisplayHealth::None ? previous : age);
            });
        FlightDisplayHealth::progress.observerStackBytes.store(uxTaskGetStackHighWaterMark(nullptr), std::memory_order_relaxed);
    }
}
#endif

// Device Info Definitions
const string DEVICE_NAME = "Ignitron";

// Control classes
#ifdef HEADLESS_SERIAL_MODE
// Construct the Spark control stack after Arduino/USB initialization.  The
// original firmware constructed it during C++ static initialization, which
// continuously resets the ESP32-S3 before setup() can run.
SparkDataControl *spark_dc = nullptr;
SparkSerialCLI *serialCLI = nullptr;
#else
SparkDataControl spark_dc;
SparkButtonHandler spark_bh;
SparkLEDControl spark_led;
SparkDisplayControl sparkDisplay;
#endif
SparkPresetControl &presetControl = SparkPresetControl::getInstance();
#ifdef PANELAN_SC05X_MODE
#ifdef PANELAN_LVGL_UI_MODE
PanelLanLVGLUI panelLanDisplay;
PanelLanMiniDisplay panelLanMiniDisplay;
#else
PanelLanDisplay panelLanDisplay;
#endif
#ifdef PANELAN_LVGL_UI_MODE
ControllerState controllerState;
ControllerActions controllerActions(controllerState);
PhysicalSwitches physicalSwitches;
PhysicalSwitchRouting physicalRouting;
void pollPhysicalSwitches() {
    static uint32_t lastPollAt = 0;
    static bool polled = false;
    const uint32_t now = millis();
    if (polled && now - lastPollAt < 5) return;
    polled = true;
    lastPollAt = now;
    uint8_t pressed = 0;
    const bool valid = panelLanMiniDisplay.readSwitches(pressed);
    const uint8_t count = static_cast<uint8_t>(presetControl.numberOfHWBanks() * PRESETS_PER_BANK);
    physicalRouting.observe(controllerState.snapshot(), count);
    physicalSwitches.sample(valid, pressed, now, [&](uint8_t key, PhysicalSwitches::Kind kind) {
#ifdef PANELAN_PRESET_TRACE
        const uint32_t previousActionId = controllerState.snapshot().miniActionId;
        controllerActions.tracePhysicalInput(now);
#endif
        physicalRouting.event(key, kind, count, controllerState.snapshot(), panelLanDisplay, controllerActions);
#ifdef PANELAN_PRESET_TRACE
        controllerActions.tracePhysicalInput(0);
        const uint32_t actionId = controllerState.snapshot().miniActionId;
        Serial.printf("PRESET_TRACE t=%lu event=physical key=%u kind=%s id=%lu\n",
                      static_cast<unsigned long>(now), key, kind == PhysicalSwitches::Kind::Short ? "short" : "long",
                      static_cast<unsigned long>(actionId != previousActionId ? actionId : 0));
#endif
    });
}
#endif
#endif

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)
void updatePanelLanScreens() {
#ifdef PANELAN_MINI_SPI_MODE
    static bool firstUpdate = true;
    if (firstUpdate) StartupDiagnostics::mark(StartupDiagnostics::MainUiUpdate, false);
#endif
    const ControllerSnapshot &snapshot = controllerState.snapshot();
#ifdef PANELAN_MINI_COEXISTENCE_TRACE
    PanelLanMiniDisplay::traceHardware("before main update");
#endif
    // LVGL flushes synchronously inside update(); then service the selected mini backend.
    panelLanDisplay.update(snapshot);
#ifdef PANELAN_MINI_SPI_MODE
    if (firstUpdate) {
        firstUpdate = false;
        StartupDiagnostics::mark(StartupDiagnostics::MainUiUpdate, true);
    }
#endif
#ifdef PANELAN_MINI_COEXISTENCE_TRACE
    PanelLanMiniDisplay::traceHardware("after main update (flush/touch)");
#endif
    panelLanMiniDisplay.update(snapshot, panelLanDisplay.activeView(), physicalRouting.browsedBank(),
                               static_cast<uint8_t>(presetControl.numberOfHWBanks() * PRESETS_PER_BANK));
#ifdef PANELAN_PRESET_TRACE
    panelLanDisplay.drainMainTrace();
#endif
#ifdef PANELAN_MINI_COEXISTENCE_TRACE
    PanelLanMiniDisplay::traceHardware("after mini update");
#endif
}
#endif

unsigned long lastInitialPresetTimestamp = 0;
unsigned long currentTimestamp = 0;
int initialRequestInterval = 3000;

// Only auto-format a filesystem partition that is truly erased. A nonblank
// unmountable image may contain recoverable user presets, so leave it intact.
bool littleFsPartitionIsBlank() {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!partition) {
        Serial.println("LittleFS: filesystem partition 'spiffs' not found");
        return false;
    }

    uint8_t buffer[1024];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(buffer)) {
        const size_t count = partition->size - offset < sizeof(buffer)
                                 ? partition->size - offset
                                 : sizeof(buffer);
        if (esp_partition_read(partition, offset, buffer, count) != ESP_OK) {
            Serial.println("LittleFS: unable to inspect partition; preserving it");
            return false;
        }
        for (size_t i = 0; i < count; ++i)
            if (buffer[i] != 0xff) return false;
    }
    return true;
}

// Check for initial boot
bool isInitBoot;
OperationMode operationMode = SPARK_MODE_APP;

#if defined(PANELAN_SC05X_MODE) && !defined(PANELAN_LVGL_UI_MODE)
bool selectTouchPreset(uint8_t preset) {
    if (SparkDataControl::isAmpConnected() && spark_dc->ampNameReceived()) {
        // The hardware preset command is the smallest, most reliable control
        // path for this bring-up UI. It avoids stale cached-preset state after
        // a Spark power cycle.
        return spark_dc->changeHWPreset(preset);
    }
    return false;
}
#endif

/////////////////////////////////////////////////////////
//
// INIT AND RUN
//
/////////////////////////////////////////////////////////

void setup() {

    Serial.begin(115200);
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
    FlightRecorder::boot();
    StartupDiagnostics::boot(); // Before the bounded USB CDC wait: do not require DTR.
    StartupDiagnostics::mark(StartupDiagnostics::Boot, false);
#endif
#ifdef PANELAN_SC05X_MODE
    // USB CDC hosts do not always assert DTR (and the display must not depend
    // on a serial terminal being open). Give the host a brief chance, then
    // continue with display and BLE bring-up.
    const unsigned long serialReadyDeadline = millis() + 1500;
    while (!Serial && millis() < serialReadyDeadline) {
        delay(10);
    }
#else
    const unsigned long serialReadyDeadline = millis() + 1500;
    while (!Serial && millis() < serialReadyDeadline) delay(10);
#endif

    Serial.println("Initializing");
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
    if (Serial) FlightRecorder::replay(Serial);
#endif
#ifdef PANELAN_MINI_SPI_MODE
    StartupDiagnostics::mark(StartupDiagnostics::Boot, true);
#endif
#ifdef PANELAN_SC05X_MODE
 #ifdef PANELAN_LVGL_UI_MODE
    panelLanDisplay.setActions(&controllerActions);
 #endif
#ifdef PANELAN_MINI_SPI_MODE
    StartupDiagnostics::mark(StartupDiagnostics::MainDisplay, false);
#endif
    panelLanDisplay.begin();
#ifdef PANELAN_MINI_SPI_MODE
    StartupDiagnostics::mark(StartupDiagnostics::MainDisplay, true);
#endif
#ifndef PANELAN_LVGL_UI_MODE
    panelLanDisplay.setPresetCallback(selectTouchPreset);
#endif
#endif
    spark_dc = new SparkDataControl();
    #if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)
    serialCLI = new SparkSerialCLI(spark_dc, &controllerActions);
    #else
    serialCLI = new SparkSerialCLI(spark_dc);
    #endif
    SparkPresetControl::getInstance().setDataControl(spark_dc);
    bool littleFsMounted = LittleFS.begin(false);
    if (!littleFsMounted) {
        if (littleFsPartitionIsBlank()) {
            Serial.println("LittleFS: blank partition; formatting fresh filesystem");
            littleFsMounted = LittleFS.begin(true);
            if (littleFsMounted) Serial.println("LittleFS: fresh filesystem mounted");
        } else {
            Serial.println("LittleFS: nonblank mount failure; preserving data and using RAM-only event log");
        }
        if (!littleFsMounted) Serial.println("LittleFS Mount failed; continuing with RAM-only event log");
    }
    if (littleFsMounted)
        Serial.printf("LittleFS: mounted (%lu/%lu bytes used)\n",
                      static_cast<unsigned long>(LittleFS.usedBytes()),
                      static_cast<unsigned long>(LittleFS.totalBytes()));
    persistentEventLog.begin(littleFsMounted);
    persistentEventLog.record(littleFsMounted ? PersistentEvent::Boot : PersistentEvent::FilesystemUnavailable, 0, true);

#ifdef HEADLESS_SERIAL_MODE
    // Headless builds are always direct controllers (APP mode). Do not depend on
    // button state during boot because no buttons are required for the prototype.
    operationMode = SPARK_MODE_APP;
#else
    spark_bh.setDataControl(&spark_dc);
    operationMode = spark_bh.checkBootOperationMode();
#endif

    // Setting operation mode before initializing
#ifdef PANELAN_MINI_SPI_MODE
    StartupDiagnostics::mark(StartupDiagnostics::SparkInit, false);
#endif
    operationMode = spark_dc->init(operationMode);
#ifdef PANELAN_MINI_SPI_MODE
    StartupDiagnostics::mark(StartupDiagnostics::SparkInit, true);
#endif
#ifndef HEADLESS_SERIAL_MODE
    spark_bh.configureButtons();
#endif
    Serial.printf("Operation mode: %d\n", operationMode);

    switch (operationMode) {
    case SPARK_MODE_APP:
        Serial.println("======= Entering APP mode =======");
        break;
    case SPARK_MODE_AMP:
        Serial.println("======= Entering AMP mode =======");
        break;
    case SPARK_MODE_KEYBOARD:
        Serial.println("======= Entering Keyboard mode =======");
        break;
    }

#ifdef HEADLESS_SERIAL_MODE
    serialCLI->begin();
#else
    sparkDisplay.setDataControl(&spark_dc);
    spark_dc.setDisplayControl(&sparkDisplay);
    sparkDisplay.init(operationMode);
    // Assigning data control to buttons;
    spark_bh.setDataControl(&spark_dc);
    // Initializing control classes
    spark_led.setDataControl(&spark_dc);
#endif

#ifdef PANELAN_LVGL_UI_MODE
    // Initialize the selected external mini backend after board/controller setup.
    // IO10-14 are dedicated expansion pins, not main LCD/touch pins.
    Serial.println("Initializing top-left ST7735S mini display");
    panelLanMiniDisplay.begin();
    // The trace target also starts with live state rendering.
    panelLanMiniDisplay.update(controllerState.snapshot(), panelLanDisplay.activeView());
#endif

#ifdef PANELAN_MINI_SPI_MODE
    StartupDiagnostics::mark(StartupDiagnostics::InitComplete, false);
#endif
    Serial.println("Initialization done.");
    persistentEventLog.record(PersistentEvent::InitComplete, static_cast<uint16_t>(operationMode), true);
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
    persistentEventLog.record(PersistentEvent::BootResetSummary, FlightRecorder::resetSummary(), true);
#endif
#ifdef PANELAN_MINI_SPI_MODE
    StartupDiagnostics::mark(StartupDiagnostics::InitComplete, true);
    StartupDiagnostics::drain(); // All mini SPI selections have been released.
#endif
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
    FlightDisplayHealth::progress.loop(millis());
    // 2048 FreeRTOS stack bytes; idle priority, below BLE and the Arduino loop.
    if (xTaskCreatePinnedToCore(displayHealthObserver, "displayHealth", 2048, nullptr,
                                tskIDLE_PRIORITY, nullptr, 0) != pdPASS)
        FlightRecorder::record(FlightRecorder::TaskFailure, 0x84);
#endif
}

void loop() {
#ifdef PANELAN_MINI_SPI_MODE
    // Ordinary frames only publish progress; a core-0 observer detects hard stalls.
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)
    static uint32_t previousIterations = 0, previousServices = 0, previousBegins = 0, previousRequests = 0;
    static uint32_t previousReady = 0, previousFlushes = 0, previousInvalidations = 0, previousRevisions = 0;
    auto &dh = FlightDisplayHealth::health;
    dh.loop(millis());
    FlightDisplayHealth::progress.loop(dh.lastLoop);
#endif
    StartupDiagnostics::drain(); // Controller task, before any mini drawing this pass.
    static uint32_t lastHeartbeat = millis();
    const uint32_t heartbeatNow = millis();
    if (heartbeatNow - lastHeartbeat >= 10000) {
        lastHeartbeat = heartbeatNow;
        FlightRecorder::heapSample(0x40);
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)
        // Compact records per 10s; detail selects units. Counts are deltas,
        // ms durations/ages saturate; service begin/end is sampled separately.
        FlightRecorder::record(FlightRecorder::DisplayHealth, 0, FlightDisplayHealth::clamp(dh.iterations - previousIterations));
        FlightRecorder::record(FlightRecorder::DisplayHealth, 1, FlightDisplayHealth::clamp(dh.maxGap));
        FlightRecorder::record(FlightRecorder::DisplayProgress, 0, FlightDisplayHealth::clamp(dh.services - previousServices));
        FlightRecorder::record(FlightRecorder::DisplayProgress, 1, FlightDisplayHealth::pair(dh.requests - previousRequests, dh.ready - previousReady));
        FlightRecorder::record(FlightRecorder::DisplayProgress, 2, FlightDisplayHealth::clamp(dh.flushes - previousFlushes));
        FlightRecorder::record(FlightRecorder::DisplayProgress, 3, FlightDisplayHealth::pair(dh.invalidations - previousInvalidations, dh.revisions - previousRevisions));
        FlightRecorder::record(FlightRecorder::DisplayProgress, 4, FlightDisplayHealth::pair(dh.serviceBegins - previousBegins, dh.services - previousServices));
        FlightRecorder::record(FlightRecorder::DisplayTiming, 0, FlightDisplayHealth::pair(dh.lastFlushMs, dh.maxFlushMs));
        FlightRecorder::record(FlightRecorder::DisplayTiming, 1, FlightDisplayHealth::clamp(dh.flushes ? heartbeatNow - dh.lastFlush : 0));
        FlightRecorder::record(FlightRecorder::DisplayTiming, 2, FlightDisplayHealth::pair(heartbeatNow - dh.serviceBegin, heartbeatNow - dh.serviceEnd));
        FlightRecorder::record(FlightRecorder::DisplayTiming, 3, FlightDisplayHealth::clamp(dh.maxServiceMs));
        FlightRecorder::record(FlightRecorder::DisplayState, static_cast<uint8_t>(controllerState.snapshot().connectionPhase),
            FlightDisplayHealth::pair(SparkDataControl::isAmpConnected() ? 1 : 0, dh.pending ? 1 : 0));
        FlightRecorder::record(FlightRecorder::DisplayState, 0x80,
            FlightDisplayHealth::pair(uxTaskGetStackHighWaterMark(nullptr) / 64, ESP.getFreeHeap() / 4096));
        FlightRecorder::record(FlightRecorder::DisplayState, 0x81,
            FlightDisplayHealth::clamp(FlightDisplayHealth::progress.observerStackBytes.load(std::memory_order_relaxed)));
        if (dh.slowestFlush) FlightRecorder::record(FlightRecorder::DisplaySlow, 0, FlightDisplayHealth::clamp(dh.slowestFlush));
        previousIterations = dh.iterations; previousServices = dh.services; previousBegins = dh.serviceBegins;
        previousRequests = dh.requests; previousReady = dh.ready; previousFlushes = dh.flushes;
        previousInvalidations = dh.invalidations; previousRevisions = dh.revisions;
        dh.maxGap = 0; dh.maxFlushMs = 0; dh.maxServiceMs = 0; dh.slowestFlush = 0;
#endif
        if (Serial) {
            Serial.printf("[startup %lu ms] heartbeat ampConnected=%u", static_cast<unsigned long>(heartbeatNow),
                          static_cast<unsigned>(SparkDataControl::isAmpConnected()));
            StartupDiagnostics::health();
        }
    }
#endif
#ifdef PANELAN_MINI_COEXISTENCE_TRACE
    if (!panelLanMiniDisplay.diagnosticRunController()) {
        serialCLI->update();
        if (panelLanMiniDisplay.diagnosticServiceMain()) {
            panelLanDisplay.update(controllerState.snapshot());
            PanelLanMiniDisplay::traceHardware("main-only service");
        }
        delay(10);
        return;
    }
#endif

    // The sole regular flash-write context. BLE callbacks only append RAM records.
    persistentEventLog.service();
#ifdef PANELAN_MINI_COEXISTENCE_TRACE
    PanelLanMiniDisplay::traceHardware("after event log service");
#endif

    // Methods to call only in APP mode
    if (operationMode == SPARK_MODE_APP) {
#ifdef HEADLESS_SERIAL_MODE
        // Keep the serial console responsive while BLE scans/connects. The stock
        // firmware stays in a blocking loop here because its buttons/display are
        // its only user interface.
        const bool sparkConnected = spark_dc->checkBLEConnection();
#ifdef PANELAN_MINI_COEXISTENCE_TRACE
        PanelLanMiniDisplay::traceHardware("after BLE connection check");
#endif
#ifdef PANELAN_SC05X_MODE
#ifdef PANELAN_LVGL_UI_MODE
        controllerState.refreshFromSpark(*spark_dc);
        pollPhysicalSwitches();
        updatePanelLanScreens();
#else
        SparkStatus &status = SparkStatus::getInstance();
        panelLanDisplay.setSparkIdentity(status.ampName().c_str(), status.ampSerialNumber().c_str());
        panelLanDisplay.update(sparkConnected);
#endif
#endif
        if (!sparkConnected) {
#ifdef PANELAN_LVGL_UI_MODE
            // process() below is unreachable on this path. Discard all action
            // work before a later link can dispatch or confirm it.
            controllerActions.onAmpDisconnected();
#endif
            serialCLI->update();
            delay(10);
            return;
        }
#else
        while (!(spark_dc.checkBLEConnection())) {
            sparkDisplay.update(spark_dc.isInitBoot());
            spark_led.updateLEDs();
            spark_bh.readButtons();
        }
#endif

        // After connection is established, continue.
        // On first boot, get the amp type and initial state.
        if (spark_dc->isInitBoot()) {
            spark_dc->requestSerialNumber();
            spark_dc->isInitBoot() = false;
        }
    }

    // Check if presets have been updated (not needed in Keyboard mode)
    if (operationMode != SPARK_MODE_KEYBOARD) {
        spark_dc->checkForUpdates();
    }

#ifdef PANELAN_LVGL_UI_MODE
    controllerState.refreshFromSpark(*spark_dc);
    controllerActions.process(*spark_dc);
    const bool foregroundReady = controllerActions.backgroundQueriesAllowed();
#else
    const bool foregroundReady = true;
#endif
    if (operationMode != SPARK_MODE_KEYBOARD) {
        spark_dc->serviceBackgroundQueries(foregroundReady);
    }

#ifdef HEADLESS_SERIAL_MODE
    serialCLI->update();
#ifdef PANELAN_SC05X_MODE
#ifdef PANELAN_LVGL_UI_MODE
    controllerState.refreshFromSpark(*spark_dc);
    updatePanelLanScreens();
#else
    panelLanDisplay.update(true);
#endif
#endif
    delay(1);
#else
    // Reading button input
    spark_bh.configureButtons();
    spark_bh.readButtons();
#ifdef ENABLE_BATTERY_STATUS_INDICATOR
    // Update battery level
    spark_dc.updateBatteryLevel();
#endif
    // Update LED status
    spark_led.updateLEDs();
    // Update display
    sparkDisplay.update();
#endif
}
