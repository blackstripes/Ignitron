#define CONFIG_LITTLEFS_SPIFFS_COMPAT

#include <Arduino.h>
#include <NimBLEDevice.h> // github NimBLE
#include <SPI.h>
#include <Wire.h>
#include <string>

#include "SparkDataControl.h"
#include "SparkPresetControl.h"
#include "SparkStatus.h"
#include "PersistentEventLog.h"
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
#else
#include "PanelLanDisplay.h"
#endif
#ifdef PANELAN_LVGL_UI_MODE
#include "controller/ControllerState.h"
#include "controller/ControllerActions.h"
#endif
#endif

using namespace std;

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
#else
PanelLanDisplay panelLanDisplay;
#endif
#ifdef PANELAN_LVGL_UI_MODE
ControllerState controllerState;
ControllerActions controllerActions(controllerState);
#endif
#endif

unsigned long lastInitialPresetTimestamp = 0;
unsigned long currentTimestamp = 0;
int initialRequestInterval = 3000;

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
#ifdef PANELAN_SC05X_MODE
    // USB CDC hosts do not always assert DTR (and the display must not depend
    // on a serial terminal being open). Give the host a brief chance, then
    // continue with display and BLE bring-up.
    const unsigned long serialReadyDeadline = millis() + 1500;
    while (!Serial && millis() < serialReadyDeadline) {
        delay(10);
    }
#else
    while (!Serial)
        ;
#endif

    Serial.println("Initializing");
#ifdef PANELAN_SC05X_MODE
 #ifdef PANELAN_LVGL_UI_MODE
    panelLanDisplay.setActions(&controllerActions);
 #endif
    panelLanDisplay.begin();
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
    const bool littleFsMounted = LittleFS.begin(false);
    if (!littleFsMounted) Serial.println("LittleFS Mount failed; continuing with RAM-only event log");
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
    operationMode = spark_dc->init(operationMode);
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

    Serial.println("Initialization done.");
    persistentEventLog.record(PersistentEvent::InitComplete, static_cast<uint16_t>(operationMode), true);
}

void loop() {

    // The sole regular flash-write context. BLE callbacks only append RAM records.
    persistentEventLog.service();

    // Methods to call only in APP mode
    if (operationMode == SPARK_MODE_APP) {
#ifdef HEADLESS_SERIAL_MODE
        // Keep the serial console responsive while BLE scans/connects. The stock
        // firmware stays in a blocking loop here because its buttons/display are
        // its only user interface.
        const bool sparkConnected = spark_dc->checkBLEConnection();
#ifdef PANELAN_SC05X_MODE
#ifdef PANELAN_LVGL_UI_MODE
        controllerState.refreshFromSpark(*spark_dc);
        panelLanDisplay.update(controllerState.snapshot());
#else
        SparkStatus &status = SparkStatus::getInstance();
        panelLanDisplay.setSparkIdentity(status.ampName().c_str(), status.ampSerialNumber().c_str());
        panelLanDisplay.update(sparkConnected);
#endif
#endif
        if (!sparkConnected) {
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
            spark_dc->getSerialNumber();
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
#endif

#ifdef HEADLESS_SERIAL_MODE
    serialCLI->update();
#ifdef PANELAN_SC05X_MODE
#ifdef PANELAN_LVGL_UI_MODE
    controllerState.refreshFromSpark(*spark_dc);
    panelLanDisplay.update(controllerState.snapshot());
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
