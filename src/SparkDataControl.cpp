/*
 * SparkDataControl.cpp
 *
 *  Created on: 19.08.2021
 *      Author: stangreg
 */

#include "SparkDataControl.h"
#include "PersistentEventLog.h"
#include "SparkMessageSequence.h"

#ifdef PANELAN_PRESET_TRACE
namespace {
// Fixed-size, payload-free observation ring. Producers only copy a small record
// under the spinlock; Serial output belongs exclusively to the controller task.
enum class IngressTraceKind : uint8_t { Seen, Enqueue, Skip, DropBusy, DropFull };
struct IngressTraceEvent {
    uint32_t atMs, id;
    size_t length;
    IngressTraceKind kind;
};
constexpr size_t kIngressTraceCapacity = 64;
IngressTraceEvent ingressTraceRing[kIngressTraceCapacity];
size_t ingressTraceRead = 0, ingressTraceWrite = 0, ingressTraceSize = 0;
uint32_t ingressTraceLost = 0;
portMUX_TYPE ingressTraceMux = portMUX_INITIALIZER_UNLOCKED;

void recordIngressTrace(IngressTraceKind kind, uint32_t id, uint32_t atMs, size_t length) {
    portENTER_CRITICAL(&ingressTraceMux);
    if (ingressTraceSize == kIngressTraceCapacity) {
        if (ingressTraceLost != UINT32_MAX) ++ingressTraceLost;
    } else {
        ingressTraceRing[ingressTraceWrite] = {atMs, id, length, kind};
        ingressTraceWrite = (ingressTraceWrite + 1) % kIngressTraceCapacity;
        ++ingressTraceSize;
    }
    portEXIT_CRITICAL(&ingressTraceMux);
}

void flushIngressTrace() {
    // Snapshot at most one ring's worth per controller pass, then print outside
    // the critical section. Producers never wait for slow serial output.
    IngressTraceEvent events[kIngressTraceCapacity];
    size_t count;
    uint32_t lost;
    portENTER_CRITICAL(&ingressTraceMux);
    count = ingressTraceSize;
    for (size_t i = 0; i < count; ++i) {
        events[i] = ingressTraceRing[ingressTraceRead];
        ingressTraceRead = (ingressTraceRead + 1) % kIngressTraceCapacity;
    }
    ingressTraceSize = 0;
    lost = ingressTraceLost;
    ingressTraceLost = 0;
    portEXIT_CRITICAL(&ingressTraceMux);

    if (lost) Serial.printf("PRESET_TRACE t=%lu event=ingress_trace_lost count=%lu\n",
                            (unsigned long)millis(), (unsigned long)lost);
    for (size_t i = 0; i < count; ++i) {
        const auto &event = events[i];
        switch (event.kind) {
        case IngressTraceKind::Seen:
            Serial.printf("PRESET_TRACE t=%lu event=ingress_seen id=%lu len=%u\n",
                          (unsigned long)event.atMs, (unsigned long)event.id, (unsigned)event.length);
            break;
        case IngressTraceKind::Enqueue:
        case IngressTraceKind::DropFull:
            Serial.printf("PRESET_TRACE t=%lu event=ingress_%s id=%lu len=%u\n",
                          (unsigned long)event.atMs,
                          event.kind == IngressTraceKind::Enqueue ? "enqueue" : "drop_full",
                          (unsigned long)event.id, (unsigned)event.length);
            break;
        case IngressTraceKind::Skip:
        case IngressTraceKind::DropBusy:
            Serial.printf("PRESET_TRACE t=%lu event=ingress_%s id=%lu reason=%s\n",
                          (unsigned long)event.atMs,
                          event.kind == IngressTraceKind::Skip ? "skip" : "drop",
                          (unsigned long)event.id,
                          event.kind == IngressTraceKind::Skip ? "empty_or_unready" : "busy");
            break;
        }
    }
}
} // namespace
#endif

SparkBTControl *SparkDataControl::bleControl = nullptr;
SparkStreamReader SparkDataControl::sparkSsr;
SparkStatus &SparkDataControl::statusObject = SparkStatus::getInstance();
SparkMessage SparkDataControl::sparkMsg;

SparkDisplayControl *SparkDataControl::sparkDisplay = nullptr;
SparkKeyboardControl *SparkDataControl::keyboardControl = nullptr;
SparkLooperControl SparkDataControl::looperControl_;
SparkBLEKeyboard SparkDataControl::bleKeyboard = SparkBLEKeyboard();

#ifdef PANELAN_PRESET_TRACE
queue<SparkDataControl::QueuedIngress> SparkDataControl::msgQueue;
atomic_uint32_t SparkDataControl::nextIngressId_{0};
#else
queue<ByteVector> SparkDataControl::msgQueue;
#endif
SemaphoreHandle_t SparkDataControl::msgQueueMutex = nullptr;
atomic_bool SparkDataControl::ingressInvalidated_{false};
SparkOutbound<CmdData> SparkDataControl::currentCommand;
SparkResponseLane SparkDataControl::responseLane_;
SparkTransportTelemetry SparkDataControl::telemetry_;
uint32_t SparkDataControl::queryTelemetryId_ = 0, SparkDataControl::writeTelemetryId_ = 0;
uint32_t SparkDataControl::lastMutationTelemetryId_ = 0;
uint32_t SparkDataControl::presetTelemetryId_ = 0, SparkDataControl::fxTelemetryId_ = 0;
uint32_t SparkDataControl::busySince_[2] = {};
bool SparkDataControl::busySeen_[2] = {};
atomic_uint32_t SparkDataControl::firstNotificationAt_{UINT32_MAX};
uint8_t SparkDataControl::notificationQueryMessageNumber_ = 0;
atomic_bool SparkDataControl::notificationArmed_{false};
atomic_uint32_t SparkDataControl::ingressHighWater_{0};
SparkRetainedIntents SparkDataControl::retainedIntents_;
SparkSubmission SparkDataControl::lastSubmissionStatus_ = SparkSubmission::Failed;
deque<AckData> SparkDataControl::pendingLooperAcks;
uint32_t SparkDataControl::finalAckRevision_ = 0;
AckData SparkDataControl::lastFinalAck_;
ProtocolObservations<AckData> SparkDataControl::finalAckEvents_;
ProtocolObservations<SparkDataControl::HardwareNumberEvent> SparkDataControl::hardwareNumberEvents_;
vector<pair<string, uint32_t>> SparkDataControl::fxModelObservationRevisions_;
uint32_t SparkDataControl::fullPresetObservationRevision_ = 0;
uint8_t SparkDataControl::fullPresetObservationMessageNumber_ = 0;
uint8_t SparkDataControl::controllerFullPresetMessageNumber_ = 0;
uint32_t SparkDataControl::looperStatusObservationRevision_ = 0;
uint32_t SparkDataControl::looperSettingsObservationRevision_ = 0;
uint32_t SparkDataControl::looperCommandObservationRevision_ = 0;
uint32_t SparkDataControl::ignoreTunerOutputUntilMs_ = 0;
atomic_uint32_t SparkDataControl::bleDisconnectCount_{0};
atomic_uint32_t SparkDataControl::bleReconnectCount_{0};
atomic_uint32_t SparkDataControl::ingressDropBusyCount_{0};
atomic_uint32_t SparkDataControl::ingressDropFullCount_{0};
atomic_uint32_t SparkDataControl::completedFullPresetCount_{0};
atomic_uint32_t SparkDataControl::controllerPresetSendCount_{0};
atomic_uint32_t SparkDataControl::controllerPresetConfirmCount_{0};
atomic_uint32_t SparkDataControl::controllerPresetFailureCount_{0};
atomic_uint32_t SparkDataControl::controllerFxSendCount_{0};
atomic_uint32_t SparkDataControl::controllerFxConfirmCount_{0};
atomic_uint32_t SparkDataControl::controllerFxFailureCount_{0};

byte SparkDataControl::nextMessageNum = 0x01;

bool SparkDataControl::ampNameReceived_ = false;

// LooperSetting *SparkDataControl::looperSetting_ = nullptr;
int SparkDataControl::tapEntrySize = 5;
CircularBuffer SparkDataControl::tapEntries(tapEntrySize);
bool SparkDataControl::recordStartFlag = false;

vector<CmdData>
    SparkDataControl::ackMsg;
vector<CmdData> SparkDataControl::currentMsg;

bool SparkDataControl::customPresetNumberChangePending = false;
bool SparkDataControl::customPresetNumberChangeReady_ = false;
OperationMode SparkDataControl::operationMode_ = SPARK_MODE_APP;
SubMode SparkDataControl::subMode_ = SUB_MODE_PRESET;

BTMode SparkDataControl::currentBTMode_ = BT_MODE_BLE;
OperationMode SparkDataControl::sparkModeAmp = SPARK_MODE_AMP;
OperationMode SparkDataControl::sparkModeApp = SPARK_MODE_APP;
AmpType SparkDataControl::sparkAmpType = AMP_TYPE_40;
string SparkDataControl::sparkAmpName = AMP_NAME_SPARK_40;
bool SparkDataControl::withDelay = false;
ByteVector SparkDataControl::checksums = {};

#ifdef ENABLE_BATTERY_STATUS_INDICATOR
BatteryLevel SparkDataControl::batteryLevel_ = BATTERY_LEVEL_0;
#endif

bool SparkDataControl::isInitBoot_ = true;
byte SparkDataControl::specialMsgNum = 0xEE;
uint8_t SparkDataControl::pendingHWPresetSlot_ = 0;
string SparkDataControl::pendingHWPresetSerial_;
uint32_t SparkDataControl::linkGeneration_ = 0;
uint32_t SparkDataControl::pendingHWPresetLink_ = 0;
uint32_t SparkDataControl::pendingHWPresetChecksums_ = 0;

SparkDataControl::SparkDataControl() {
    // init();
    bleControl = new SparkBTControl(this);
    keyboardControl = new SparkKeyboardControl();
    keyboardControl->init();
    tapEntries = CircularBuffer(tapEntrySize);
    if (!msgQueueMutex) {
        msgQueueMutex = xSemaphoreCreateMutex();
    }
}

SparkDataControl::~SparkDataControl() {
    if (bleControl)
        delete bleControl;
#ifndef HEADLESS_SERIAL_MODE
    if (sparkDisplay)
        delete sparkDisplay;
#endif
    if (keyboardControl)
        delete keyboardControl;
}

OperationMode SparkDataControl::init(OperationMode opModeInput) {
    operationMode_ = opModeInput;

    tapEntries = CircularBuffer(tapEntrySize);

    readOpModeFromFile();
    SparkPresetControl::getInstance().init();

    // Define MAC address required for keyboard
    uint8_t macKeyboard[] = {0xB4, 0xE6, 0x2D, 0xB2, 0x1B, 0x36}; //{0x36, 0x33, 0x33, 0x33, 0x33, 0x33};

    switch (operationMode_) {
    case SPARK_MODE_APP:
#ifndef HEADLESS_SERIAL_MODE
        // Set MAC address for BLE keyboard
        esp_base_mac_addr_set(&macKeyboard[0]);

        // initialize BLE
        bleKeyboard.setName("Ignitron BLE");
        bleKeyboard.begin();
        // delay(2000);
        bleKeyboard.end();
#endif
        bleControl->initBLE(&bleNotificationCallback);
        DEBUG_PRINTLN("Starting regular check for empty HW presets.");

        xTaskCreatePinnedToCore(
            startLooperTimer,
            "LooperTimer",
            10000,
            NULL,
            0,
            NULL,
            1);
        break;
    case SPARK_MODE_AMP:
        readBTModeFromFile();
        if (currentBTMode_ == BT_MODE_BLE) {
            bleControl->startServer();
        } else if (currentBTMode_ == BT_MODE_SERIAL) {
            bleControl->startBTSerial();
        }
        readPresetChecksums();
        break;
    case SPARK_MODE_KEYBOARD:
        // Set MAC address for BLE keyboard
        esp_base_mac_addr_set(&macKeyboard[0]);

        // initialize BLE
        bleKeyboard.setName("Ignitron BLE");
        bleKeyboard.begin();
        break;
    }

    return operationMode_;
}

void SparkDataControl::switchSubMode(SubMode subMode) {
    lastSubmissionStatus_ = SparkSubmission::Sent;
    // A refused tuner write must not claim a local mode transition. Do this
    // before keyboard/tail-window side effects as well.
    if (subMode_ == SUB_MODE_TUNER && subMode != SUB_MODE_TUNER && !switchTuner(false)) return;
    if (subMode == SUB_MODE_TUNER && subMode_ != SUB_MODE_TUNER && !switchTuner(true)) return;
    // TODO: Check if that works fine
    if (subMode == SUB_MODE_LOOPER) {
        bleKeyboard.start();
    } else {
        bleKeyboard.end();
    }
    // Switch off tuner mode at amp if was enabled before but is not matching current subMode
    if (subMode_ == SUB_MODE_TUNER && subMode_ != subMode) {
        // Spark 2 can leave one pitch packet queued after an explicit tuner
        // exit. Ignore only this short tail; a later sample is still allowed
        // to reveal a genuinely active externally-entered tuner session.
        ignoreTunerOutputUntilMs_ = millis() + 2000;
    }
    subMode_ = subMode;
    SparkPresetControl::getInstance().updatePendingWithActive();
}

bool SparkDataControl::toggleSubMode() {

    if (!processAction() || operationMode_ == SPARK_MODE_AMP) {
        Serial.println("Spark Amp not connected or in AMP mode, doing nothing.");
        return false;
    }

    Serial.print("Switching to ");
    if (operationMode_ == SPARK_MODE_APP) {
        switch (subMode_) {
        case SUB_MODE_FX:
            Serial.println("PRESET mode");
            subMode_ = SUB_MODE_PRESET;
            break;
        case SUB_MODE_PRESET:
            Serial.println("FX mode");
            subMode_ = SUB_MODE_FX;
            SparkPresetControl::getInstance().updatePendingWithActive();
            break;
        case SUB_MODE_LOOP_CONTROL:
            Serial.println("Looper CONFIG mode");
            subMode_ = SUB_MODE_LOOP_CONFIG;
            break;
        case SUB_MODE_LOOP_CONFIG:
            Serial.println("Looper CONTROL mode");
            subMode_ = SUB_MODE_LOOP_CONTROL;
            SparkPresetControl::getInstance().updatePendingWithActive();
            break;
        default:
            Serial.println("Unexpected mode. Defaulting to PRESET mode");
            subMode_ = SUB_MODE_PRESET;
            break;
        } // SWITCH
    }
    return true;
}

bool SparkDataControl::toggleLooperAppMode() {

    if (operationMode_ == SPARK_MODE_AMP || !processAction()) {
        Serial.println("Spark Amp not connected or in AMP mode, doing nothing.");
        return false;
    }
    SubMode newSubMode;
    Serial.print("Switching to ");
    switch (subMode_) {
    case SUB_MODE_PRESET:
    case SUB_MODE_FX:
        Serial.println("LOOPER mode");
        if (sparkAmpName == AMP_NAME_SPARK_2) {
            newSubMode = SUB_MODE_LOOP_CONTROL;
            looperControl_.stop();
            looperControl_.reset();
            requestLooperSync();
        } else {
            newSubMode = SUB_MODE_LOOPER;
        }
        break;
    case SUB_MODE_LOOPER:
    case SUB_MODE_SPK_LOOPER:
        Serial.println("APP mode");
        newSubMode = SUB_MODE_PRESET;
        looperControl_.triggerReset();
        break;
    default:
        Serial.println("Unexpected mode. Defaulting to APP mode");
        newSubMode = SUB_MODE_PRESET;
        break;
    } // SWITCH
    switchSubMode(newSubMode);
    return true;
}

void SparkDataControl::setDisplayControl(SparkDisplayControl *display) {
    sparkDisplay = display;
}

void SparkDataControl::restartESP(bool resetSparkMode) {
    // RESET Ignitron
    Serial.print("!!! Restarting !!! ");
    if (resetSparkMode) {
        Serial.print("Resetting Spark mode");
        bool sparkModeFileExists = LittleFS.exists(sparkModeFileName.c_str());
        if (sparkModeFileExists) {
            LittleFS.remove(sparkModeFileName.c_str());
        }
    }
    Serial.println();
    ESP.restart();
}

void SparkDataControl::readOpModeFromFile() {
    OperationMode sparkModeInput;
    Serial.println("Reading opmode file.");
    if (!LittleFS.exists(sparkModeFileName.c_str())) {
        Serial.println("Spark mode config file does not exist.");
        return;
    }
    File file = LittleFS.open(sparkModeFileName.c_str());
    string line;

    if (!(file)) {
        Serial.println("Error reading Spark mode file.");
        return;
    }

    while (file.available()) {
        line += file.read();
    }
    file.close();
    Serial.printf("OPMode: %s\n", line.c_str());

    sparkModeInput = (OperationMode)(line[0] - '0'); // was: stoi(line);

    if (sparkModeInput != 0) {
        operationMode_ = sparkModeInput;
        Serial.printf("Reading operation mode from file: %d\n", sparkModeInput);
    }
}

void SparkDataControl::readBTModeFromFile() {
    string line;
    File file = LittleFS.open(btModeFileName.c_str());

    while (file.available()) {
        line += file.read();
    }
    Serial.printf("BTMode: %s\n", line.c_str());
    file.close();
    currentBTMode_ = (BTMode)(line[0] - '0'); // was: stoi(line);
}

#ifdef ENABLE_BATTERY_STATUS_INDICATOR
void SparkDataControl::updateBatteryLevel() {
#if BATTERY_TYPE == BATTERY_TYPE_LI_ION || BATTERY_TYPE == BATTERY_TYPE_LI_FE_PO4
    const int analogReading = analogRead(BATTERY_VOLTAGE_ADC_PIN);
    // analogReading ranges from 0 to 4095
    // 0V = 0, 3.3V = 4095 (BATTERY_MAX_LEVEL)
    float analogVoltage = (analogReading / BATTERY_MAX_LEVEL) * 3.3;
    float batteryVoltage = analogVoltage / VOLTAGE_DIVIDER_R1 * (VOLTAGE_DIVIDER_R1 + VOLTAGE_DIVIDER_R2);
#elif BATTERY_TYPE == BATTERY_TYPE_AMP
    float batteryVoltage = SparkStatus::getInstance().ampBatteryLevel();
#endif

    // Set battery level
    batteryLevel_ = batteryVoltage < BATTERY_CAPACITY_VOLTAGE_THRESHOLD_10
                        ? BATTERY_LEVEL_0
                    : batteryVoltage < BATTERY_CAPACITY_VOLTAGE_THRESHOLD_50
                        ? BATTERY_LEVEL_1
                    : batteryVoltage < BATTERY_CAPACITY_VOLTAGE_THRESHOLD_90
                        ? BATTERY_LEVEL_2
                        : BATTERY_LEVEL_3;

#if BATTERY_TYPE == BATTERY_TYPE_AMP
    if (SparkStatus::getInstance().ampBatteryChargingStatus() == BATTERY_CHARGING_STATUS_CHARGING) {
        batteryLevel_ = BATTERY_LEVEL_CHARGING;
    }
#endif
}
#endif

void SparkDataControl::resetStatus() {
    Serial.println("Resetting Status");
    ampNameReceived_ = false;
    isInitBoot_ = true;
    operationMode_ = SPARK_MODE_APP;
    subMode_ = SUB_MODE_PRESET;
    nextMessageNum = 0x01;
    ++linkGeneration_;
    cancelHWPresetRead();
    customPresetNumberChangePending = false;
    customPresetNumberChangeReady_ = false;
    sparkAmpType = AMP_TYPE_40;
    sparkAmpName = "Spark 40";
    withDelay = false;
    lastAmpBatteryUpdate = 0;
    ampBatteryPoll_.reset();
    ingressInvalidated_.store(false);
    clearQueuedMessages();
    sparkSsr.reset();
    currentCommand.clear();
    telemetry_.end(queryTelemetryId_, SparkTransportTelemetry::Reason::LinkReset);
    telemetry_.end(presetTelemetryId_, SparkTransportTelemetry::Reason::LinkReset);
    telemetry_.end(fxTelemetryId_, SparkTransportTelemetry::Reason::LinkReset);
    queryTelemetryId_ = 0;
    presetTelemetryId_ = fxTelemetryId_ = lastMutationTelemetryId_ = writeTelemetryId_ = 0;
    notificationArmed_.store(false);
    busySeen_[0] = busySeen_[1] = false;
#ifdef PANELAN_PRESET_TRACE
    if (controllerFullPresetMessageNumber_ != 0) {
        Serial.printf("PRESET_TRACE t=%lu event=controller_full_revoke path=link_reset prior=%u owner_active=%u owner_msg=%u owner_sub=%02X\n",
                      (unsigned long)millis(), controllerFullPresetMessageNumber_, responseLane_.active(),
                      responseLane_.traceMessageNumber(), responseLane_.traceSubcommand());
    }
#endif
    responseLane_.reset();
    controllerFullPresetMessageNumber_ = 0;
    retainedIntents_.reset();
    lastSubmissionStatus_ = SparkSubmission::Failed;
    pendingLooperAcks.clear();
    currentMsg.clear();
    ackMsg.clear();
    SparkPresetControl::getInstance().resetStatus();
    SparkStatus::getInstance().resetStatus();
    looperStatusObservationRevision_ = 0;
    looperSettingsObservationRevision_ = 0;
    looperCommandObservationRevision_ = 0;
}

/////////////////////////////////////////////////////////
// SPARK COMMUNICATION CONTROL
/////////////////////////////////////////////////////////

void SparkDataControl::setAmpParameters() {

    string ampName = sparkAmpName;
    DEBUG_PRINTF("Amp name: %s\n", ampName.c_str());
    if (ampName == AMP_NAME_SPARK_40 || ampName == AMP_NAME_SPARK_GO || ampName == AMP_NAME_SPARK_NEO) {
        sparkMsg.maxChunkSizeToSpark() = 0x80;
        sparkMsg.maxBlockSizeToSpark() = 0xAD;
        sparkMsg.withHeader() = true;
        bleControl->setMaxBleMsgSize(0xAD);
        withDelay = false;
    }
    if (ampName == AMP_NAME_SPARK_MINI || ampName == AMP_NAME_SPARK_2) { // || ampName == AMP_NAME_SPARK_NEO) {
        sparkMsg.maxChunkSizeToSpark() = 0x80;
        sparkMsg.maxBlockSizeToSpark() = 0xAD;
        sparkMsg.withHeader() = true;
        bleControl->setMaxBleMsgSize(0x64);
        withDelay = true;
    }
    sparkMsg.maxChunkSizeFromSpark() = 0x19;
    sparkMsg.maxBlockSizeFromSpark() = 0x6A;

    SparkPresetControl::getInstance().setAmpParameters(ampName);
}

void SparkDataControl::readPresetChecksums() {
    SparkPresetControl &presetControl = SparkPresetControl::getInstance();
    checksums.clear();
    for (int i = 1; i <= PRESETS_PER_BANK; i++) {
        Preset preset = presetControl.getPreset(1, i);
        byte checksum = sparkMsg.getPresetChecksum(preset);
        checksums.push_back(checksum);
    }
}

void SparkDataControl::checkForUpdates() {

#ifdef PANELAN_PRESET_TRACE
    flushIngressTrace();
#endif
    expireResponseOwner();
    ByteVector queuedMessage;
    while (true) {
        // A lost BLE fragment invalidates any partial Spark response. Reset
        // from this controller task rather than from the NimBLE callback.
        if (ingressInvalidated_.exchange(false)) {
            persistentEventLog.record(PersistentEvent::IngressInvalidated, 0, true);
            clearQueuedMessages();
            sparkSsr.reset();
            invalidateResponseOwner();
            break;
        }
        if (responseLane_.active() && queryTelemetryId_) {
            const uint32_t at = firstNotificationAt_.load();
            if (at != UINT32_MAX) telemetry_.notification(queryTelemetryId_, at);
        }
#ifdef PANELAN_PRESET_TRACE
        uint32_t ingressId = 0, ingressAt = 0;
        if (!takeQueuedMessage(queuedMessage, ingressId, ingressAt)) {
#else
        if (!takeQueuedMessage(queuedMessage)) {
#endif
            break;
        }
#ifdef PANELAN_PRESET_TRACE
        Serial.printf("PRESET_TRACE t=%lu event=ingress_dequeue id=%lu seen=%lu len=%u\n",
                      (unsigned long)millis(), (unsigned long)ingressId,
                      (unsigned long)ingressAt, (unsigned)queuedMessage.size());
#endif
        if (ingressInvalidated_.exchange(false)) {
            persistentEventLog.record(PersistentEvent::IngressInvalidated, 0, true);
            clearQueuedMessages();
            sparkSsr.reset();
            invalidateResponseOwner();
            break;
        }
#ifdef PANELAN_PRESET_TRACE
        processSparkData(queuedMessage, ingressId);
#else
        processSparkData(queuedMessage);
#endif
    }

    SparkPresetControl::getInstance().checkForUpdates(operationMode_);
    serviceRetainedIntents();

    if (recordStartFlag) {
        if (looperControl_.currentBar() != 0) {
            if (sparkLooperCommand(SPK_LOOPER_CMD_REC)) {
                recordStartFlag = false;
            }
        }
    }

    // A final preset ACK can arrive while another multipart command has taken
    // ownership. Retain this follow-up until the owner is free and only update
    // active preset state after the BLE send succeeds.
    if (customPresetNumberChangeReady_ && !currentCommand.hasRemaining()) {
        currentMsg = sparkMsg.changeHardwarePreset(nextMessageNum, 128);
        if (triggerCommand(currentMsg)) {
            customPresetNumberChangePending = false;
            customPresetNumberChangeReady_ = false;
            SparkPresetControl::getInstance().updateActiveWithPendingPreset();
        }
    }

    if (statusObject.isLooperSettingUpdated()) {
        looperControl_.setLooperSetting(statusObject.currentLooperSetting());
        statusObject.resetLooperSettingUpdateFlag();
    }

    const LooperSetting &looperSetting = looperControl_.looperSetting();
    if (looperSetting.changePending) {
        if (updateLooperSettings()) {
            looperControl_.resetChangePending();
        }
    }

    if (operationMode_ == SPARK_MODE_AMP) {

        // Read incoming (serial) Bluetooth data, if available
        while (bleControl && bleControl->byteAvailable()) {
            byte inputByte = bleControl->readByte();
            currentBTMsg.push_back(inputByte);
            int msgSize = currentBTMsg.size();
            if (msgSize > 0) {
                if (currentBTMsg[msgSize - 1] == 0xF7) {
                    // DEBUG_PRINTF("Free Heap size: %d\n", ESP.getFreeHeap()); DEBUG_PRINTF("Max free Heap block: %d\n",
                    //		ESP.getMaxAllocHeap());

                    DEBUG_PRINTLN("Received a message");
                    DEBUG_PRINTVECTOR(currentBTMsg);
                    DEBUG_PRINTLN();
                    // heap_caps_check_integrity_all(true) ;
                    processSparkData(currentBTMsg);

                    currentBTMsg.clear();
                }
            }
        }
    }
}

void SparkDataControl::serviceBackgroundQueries(bool foregroundReady) {
#if defined(ENABLE_BATTERY_STATUS_INDICATOR) && BATTERY_TYPE == BATTERY_TYPE_AMP
    if (operationMode_ == SPARK_MODE_KEYBOARD) return;
    const bool connected = operationMode_ == SPARK_MODE_AMP ? isAppConnected() : isAmpConnected();
    ampBatteryPoll_.service(millis(), connected, foregroundReady, lastAmpBatteryUpdate,
        []() -> AmpBatteryPoll::Transport {
            return {lastSubmissionStatus_, currentCommand.hasRemaining(),
                    static_cast<int>(currentCommand.remainingCount()), responseLane_.active(),
#ifdef PANELAN_PRESET_TRACE
                    responseLane_.traceMessageNumber(), responseLane_.traceSubcommand(), false};
#else
                    0, 0, false};
#endif
        },
        []() {
            currentMsg = sparkMsg.getAmpStatus(nextMessageNum);
            return triggerCommand(currentMsg);
        },
        [](AmpBatteryPoll::Event event, const AmpBatteryPoll::Transport &transport) {
#ifdef PANELAN_PRESET_TRACE
            const char *name = event == AmpBatteryPoll::Event::Due ? "battery_poll_due" :
                               event == AmpBatteryPoll::Event::Sent ? "battery_poll_sent" : "battery_poll_deferred";
            const char *status = transport.submission == SparkSubmission::Sent ? "Sent" :
                                 transport.submission == SparkSubmission::Busy ? "Busy" : "Failed";
            Serial.printf("PRESET_TRACE t=%lu event=%s lastSubmissionStatus=%s currentCommandHasRemaining=%u currentCommandRemainingParts=%d responseLaneActive=%u ownerMsg=%u ownerSub=%02X%s\n",
                          (unsigned long)millis(), name, status, transport.remaining,
                          transport.remainingParts, transport.laneActive,
                          transport.ownerMessage, transport.ownerSubcommand,
                          event == AmpBatteryPoll::Event::Deferred && transport.controllerWorkPending
                              ? " reason=controller_work_pending" : "");
#else
            (void)event; (void)transport;
#endif
        });
#else
    (void)foregroundReady;
#endif
}

void SparkDataControl::processSparkData(ByteVector &blk
#ifdef PANELAN_PRESET_TRACE
                                        , uint32_t ingressId
#endif
                                        ) {
#ifdef PANELAN_PRESET_TRACE
    sparkSsr.setTraceIngress(ingressId);
#endif

    /*DEBUG_PRINT("Received data: ");
    DEBUG_PRINTVECTOR(blk);
    DEBUG_PRINTLN();
    */
    // Check if incoming message requires sending an acknowledgment
    handleSendingAck(blk);

    MessageProcessStatus retCode = sparkSsr.processBlock(blk);
    bool completed = false;
    const bool hasMessage = retCode != MSG_PROCESS_RES_INCOMPLETE;
    while (retCode != MSG_PROCESS_RES_INCOMPLETE) {
        if (retCode == MSG_PROCESS_RES_REQUEST && operationMode_ == SPARK_MODE_AMP) {
            handleAmpModeRequest();
        }
        if (retCode == MSG_PROCESS_RES_COMPLETE) {
            completed = true;
            const auto &parsed = sparkSsr.lastMessage();
            const uint8_t responseNumber = statusObject.lastMessageNum();
            const uint8_t responseCmd = parsed.empty() ? 0 : parsed.back().cmd;
            const uint8_t responseSubcmd = parsed.empty() ? 0 : parsed.back().subcmd;
            handleAppModeResponse();
            // State processing and correlation gates see every complete message,
            // including unsolicited ones, before transport ownership is released.
#ifdef PANELAN_PRESET_TRACE
            const bool laneActive = responseLane_.active();
            const uint8_t laneMsg = responseLane_.traceMessageNumber();
            const uint8_t laneSub = responseLane_.traceSubcommand();
#endif
            const bool released = responseLane_.complete(responseNumber, responseCmd, responseSubcmd);
#ifdef PANELAN_PRESET_TRACE
            if ((responseCmd == 0x03 && responseSubcmd == 0x01) ||
                (laneActive && laneSub == 0x01)) {
                Serial.printf("PRESET_TRACE t=%lu event=response_lane_complete msg=%u cmd=%02X sub=%02X active_before=%u owner_msg=%u owner_sub=%02X matched=%u released=%u\n",
                              (unsigned long)millis(), responseNumber, responseCmd, responseSubcmd,
                              laneActive, laneMsg, laneSub, released, released);
            }
#endif
            if (released) {
                telemetry_.response(queryTelemetryId_, millis());
                queryTelemetryId_ = 0;
                notificationArmed_.store(false);
            }
        }
        handleIncomingAck();
        retCode = sparkSsr.nextMessage();
    }
    if (!hasMessage) handleIncomingAck();
    if (completed) serviceRetainedIntents();
}

bool SparkDataControl::processAction() {

    if (operationMode_ == SPARK_MODE_AMP) {
        return true;
    }
    return isAmpConnected();
}

bool SparkDataControl::getCurrentPresetFromSpark(uint8_t *messageNumber) {
    int hwPreset = -1;
    // SparkMessage encodes zero as sequence number one on the wire. Return
    // that canonical wire value so callers can correlate the response.
    const uint8_t requestMessageNumber = nextMessageNum == 0 ? 0x01 : nextMessageNum;
    currentMsg = sparkMsg.getCurrentPreset(requestMessageNumber, hwPreset);
    Serial.println("Getting current preset from Spark");

    const bool sent = triggerCommand(currentMsg);
    if (sent && messageNumber) {
        *messageNumber = requestMessageNumber;
    }
    return sent;
}

bool SparkDataControl::switchPreset(int pre, bool isInitial) {

    return SparkPresetControl::getInstance().switchPreset(pre, isInitial);
}

bool SparkDataControl::changeHWPreset(int preset, uint8_t *messageNumber) {

    const uint8_t issuedMessageNumber = nextMessageNum == 0 ? 0x01 : nextMessageNum;
    currentMsg = sparkMsg.changeHardwarePreset(issuedMessageNumber, preset);
    const bool sent = triggerCommand(currentMsg);
    if (sent && messageNumber) *messageNumber = issuedMessageNumber;
    return sent;
}

bool SparkDataControl::changePreset(Preset preset) {
    currentMsg = sparkMsg.changePreset(preset, DIR_TO_SPARK, nextMessageNum);
    if (triggerCommand(currentMsg)) {
        customPresetNumberChangePending = true;
        customPresetNumberChangeReady_ = false;
        return true;
    }
    return false;
}

bool SparkDataControl::switchEffectOnOff(const string &fxName, bool enable, uint8_t *messageNumber) {

    currentMsg = sparkMsg.turnEffectOnOff(nextMessageNum, fxName, enable);

    const uint8_t issuedMessageNumber = nextMessageNum == 0 ? 0x01 : nextMessageNum;
    const bool sent = triggerCommand(currentMsg);
    if (sent) SparkPresetControl::getInstance().switchFXOnOff(fxName, enable);
    if (sent && messageNumber != nullptr) {
        *messageNumber = issuedMessageNumber;
    }
    return sent;
}

bool SparkDataControl::toggleEffect(int fxIdentifier) {

    Preset activePreset = SparkPresetControl::getInstance().activePreset();
    if (!processAction() || operationMode_ == SPARK_MODE_AMP) {
        Serial.println("Not connected to Spark Amp or in AMP mode, doing nothing.");
        return false;
    }
    if (activePreset.isEmpty) {
        return false;
    }
    string fxName = activePreset.pedals[fxIdentifier].name;
    bool fxIsOn = activePreset.pedals[fxIdentifier].isOn;

    return switchEffectOnOff(fxName, fxIsOn ? false : true);
}

bool SparkDataControl::getAmpName() {
    currentMsg = sparkMsg.getAmpName(nextMessageNum);
    DEBUG_PRINTLN("Getting amp name from Spark");

    return triggerCommand(currentMsg);
}

bool SparkDataControl::getCurrentPresetNum(uint8_t *messageNumber) {
    const uint8_t issuedMessageNumber = nextMessageNum == 0 ? 0x01 : nextMessageNum;
    currentMsg = sparkMsg.getCurrentPresetNum(nextMessageNum);
    DEBUG_PRINTLN("Getting current preset num from Spark");

    const bool sent = triggerCommand(currentMsg);
    if (sent && messageNumber) *messageNumber = issuedMessageNumber;
    return sent;
}

bool SparkDataControl::getSerialNumber() {
    currentMsg = sparkMsg.getSerialNumber(nextMessageNum);
    DEBUG_PRINTLN("Getting serial number from Spark");

    return triggerCommand(currentMsg);
}

void SparkDataControl::requestSerialNumber() {
    retainedIntents_.request(SparkRetainedIntents::Serial);
    serviceRetainedIntents();
}

void SparkDataControl::requestCurrentPresetRefresh() {
    retainedIntents_.request(SparkRetainedIntents::CurrentPreset);
    // Only the controller loop dispatches this: callers may be inside the
    // response handler, before its current owner has been released.
}

SparkSubmission SparkDataControl::lastSubmissionStatus() { return lastSubmissionStatus_; }

#ifdef PANELAN_PRESET_TRACE
SparkDataControl::PresetTransportTrace SparkDataControl::presetTransportTrace() {
    return {lastSubmissionStatus_, currentCommand.hasRemaining(),
            static_cast<int>(currentCommand.remainingCount()), responseLane_.active(),
            responseLane_.traceMessageNumber(), responseLane_.traceSubcommand(),
            controllerFullPresetMessageNumber_};
}
#endif

bool SparkDataControl::responseQueryPending(uint8_t messageNumber, uint8_t subcmd) {
    expireResponseOwner();
    return responseLane_.owns(messageNumber, subcmd);
}

void SparkDataControl::invalidateResponseOwner() {
#ifdef PANELAN_PRESET_TRACE
    const bool ownedController = responseLane_.owns(controllerFullPresetMessageNumber_, 0x01);
    const bool presetOwner = responseLane_.active() && responseLane_.traceSubcommand() == 0x01;
    if (ownedController) {
        Serial.printf("PRESET_TRACE t=%lu event=controller_full_revoke path=owner_invalidate prior=%u owner_msg=%u owner_sub=%02X\n",
                      (unsigned long)millis(), controllerFullPresetMessageNumber_,
                      responseLane_.traceMessageNumber(), responseLane_.traceSubcommand());
    } else if (presetOwner || controllerFullPresetMessageNumber_ != 0) {
        Serial.printf("PRESET_TRACE t=%lu event=response_owner_invalidate expected=%u owner_active=%u owner_msg=%u owner_sub=%02X\n",
                      (unsigned long)millis(), controllerFullPresetMessageNumber_, responseLane_.active(),
                      responseLane_.traceMessageNumber(), responseLane_.traceSubcommand());
    }
#endif
    telemetry_.ingressInvalidation(queryTelemetryId_);
    queryTelemetryId_ = 0;
    notificationArmed_.store(false);
    if (responseLane_.owns(controllerFullPresetMessageNumber_, 0x01))
        controllerFullPresetMessageNumber_ = 0;
    responseLane_.reset();
}

void SparkDataControl::expireResponseOwner() {
    const bool ownedController = responseLane_.owns(controllerFullPresetMessageNumber_, 0x01);
#ifdef PANELAN_PRESET_TRACE
    const bool laneActive = responseLane_.active();
    const uint8_t laneMsg = responseLane_.traceMessageNumber();
    const uint8_t laneSub = responseLane_.traceSubcommand();
#endif
    if (responseLane_.expire(millis())) {
#ifdef PANELAN_PRESET_TRACE
        if (ownedController) {
            Serial.printf("PRESET_TRACE t=%lu event=controller_full_revoke path=owner_expire prior=%u owner_msg=%u owner_sub=%02X\n",
                          (unsigned long)millis(), controllerFullPresetMessageNumber_, laneMsg, laneSub);
        } else if ((laneActive && laneSub == 0x01) || controllerFullPresetMessageNumber_ != 0) {
            Serial.printf("PRESET_TRACE t=%lu event=response_owner_expire expected=%u owner_msg=%u owner_sub=%02X\n",
                          (unsigned long)millis(), controllerFullPresetMessageNumber_, laneMsg, laneSub);
        }
#endif
        telemetry_.end(queryTelemetryId_, SparkTransportTelemetry::Reason::Timeout);
        queryTelemetryId_ = 0;
        notificationArmed_.store(false);
        if (ownedController) controllerFullPresetMessageNumber_ = 0;
    }
}

bool SparkDataControl::getFirmwareVersion() {
    currentMsg = sparkMsg.getFirmwareVersion(nextMessageNum);
    DEBUG_PRINTLN("Getting firmware version from Spark");

    return triggerCommand(currentMsg);
}

bool SparkDataControl::getHWChecksums() {
    DEBUG_PRINTLN("Getting checksums from Spark");
    if (sparkAmpName == AMP_NAME_SPARK_2) {
        currentMsg = sparkMsg.getHWChecksumsExtended(nextMessageNum);
    } else {
        currentMsg = sparkMsg.getHwChecksums(nextMessageNum);
    }
    return triggerCommand(currentMsg);
}

bool SparkDataControl::getCurrentPreset(int num) {
    currentMsg = sparkMsg.getCurrentPreset(nextMessageNum, num);
    DEBUG_PRINTLN("Getting preset information from Spark");

    return triggerCommand(currentMsg);
}

bool SparkDataControl::triggerCommand(vector<CmdData> &msg) {
    // The first part must actually be written before a caller may treat true
    // as an issued command. Never replace another command's unsent parts.
    expireResponseOwner();
    if (currentCommand.hasRemaining() ||
        (!msg.empty() && msg.front().cmd == 0x02 && responseLane_.active())) {
        if (!msg.empty()) {
            const unsigned kind = msg.front().cmd == 0x02 ? 0 : 1;
            if (!busySeen_[kind]) { busySeen_[kind] = true; busySince_[kind] = millis(); }
        }
        lastSubmissionStatus_ = SparkSubmission::Busy;
        return false;
    }
    if (msg.empty() || !responseLane_.supported(msg.front().cmd, msg.front().subcmd)) {
        lastSubmissionStatus_ = SparkSubmission::Failed;
        return false;
    }
    // Spark encodes zero as wire sequence one. A command built with zero is
    // therefore sequence one, so advance directly to two and avoid reusing
    // one for the following command.
    nextMessageNum = nextNormalSparkMessageNumber(nextMessageNum);
    // sparkSsr.clearMessageBuffer();
    DEBUG_PRINTLN("Sending message via BT.");
    const bool query = msg.front().cmd == 0x02;
    const unsigned kind = query ? 0 : 1;
    const uint32_t id = telemetry_.begin(query ? 1 : 2, busySeen_[kind] ? busySince_[kind] : millis());
    busySeen_[kind] = false;
    writeTelemetryId_ = id;
    if (query) {
        firstNotificationAt_.store(UINT32_MAX);
        notificationQueryMessageNumber_ = msg.front().msgNum;
        notificationArmed_.store(true);
    }
    if (!currentCommand.start(msg, writeRequest)) {
        if (query) { notificationArmed_.store(false); notificationQueryMessageNumber_ = 0; }
        writeTelemetryId_ = 0;
        lastSubmissionStatus_ = SparkSubmission::Failed;
        return false;
    }
    writeTelemetryId_ = 0;
    if (query) queryTelemetryId_ = id;
    else lastMutationTelemetryId_ = id;
    responseLane_.acquire(msg.front().cmd, msg.front().subcmd, msg.front().msgNum, millis());
    if (query && responseLane_.active()) {
        // A callback can enqueue a reply during the synchronous BLE write.
        // This timestamp was cleared before that write, not inherited from a
        // notification of the previous query.
        const uint32_t at = firstNotificationAt_.load();
        if (at != UINT32_MAX) telemetry_.notification(id, at);
    }
    lastSubmissionStatus_ = SparkSubmission::Sent;
    return true;
    // sparkSsr.clearMessageBuffer();
}

void SparkDataControl::serviceRetainedIntents() {
    if (operationMode_ != SPARK_MODE_APP || !isAmpConnected()) return;
    retainedIntents_.service([](SparkRetainedIntents::Kind kind) {
        switch (kind) {
        case SparkRetainedIntents::AmpName: return getAmpName();
        case SparkRetainedIntents::Serial: return getSerialNumber();
        case SparkRetainedIntents::Checksums: return getHWChecksums();
        case SparkRetainedIntents::CurrentPreset: return getCurrentPresetFromSpark();
        case SparkRetainedIntents::LooperConfig:
            currentMsg = sparkMsg.getLooperConfig(nextMessageNum);
            return triggerCommand(currentMsg);
        case SparkRetainedIntents::LooperStatus:
            currentMsg = sparkMsg.getLooperStatus(nextMessageNum);
            return triggerCommand(currentMsg);
        case SparkRetainedIntents::LooperRecordStatus:
            currentMsg = sparkMsg.getLooperRecordStatus(nextMessageNum);
            return triggerCommand(currentMsg);
        default: return false;
        }
    });
}

void SparkDataControl::requestLooperSync() {
    retainedIntents_.request(SparkRetainedIntents::LooperConfig);
    retainedIntents_.request(SparkRetainedIntents::LooperStatus);
    serviceRetainedIntents();
}

void SparkDataControl::requestLooperStatus() {
    retainedIntents_.request(SparkRetainedIntents::LooperStatus);
    serviceRetainedIntents();
}

void SparkDataControl::requestLooperRecordStatus() {
    retainedIntents_.request(SparkRetainedIntents::LooperRecordStatus);
    serviceRetainedIntents();
}

bool SparkDataControl::writeRequest(const CmdData &request) {
    ByteVector block = request.data;
    const uint32_t id = writeTelemetryId_;
    telemetry_.start(id, millis());
    size_t chunks = 0;
    const bool sent = sendMessageToBT(block, &chunks);
    telemetry_.write(id, millis(), static_cast<uint16_t>(chunks), sent, currentCommand.writingLastPart());
    if (!sent) return false;
    AckData currRequest;
    currRequest.cmd = request.cmd;
    currRequest.subcmd = request.subcmd;
    currRequest.detail = request.detail;
    currRequest.msgNum = request.msgNum;
    pendingLooperAcks.push_back(currRequest);
    return true;
}

void SparkDataControl::handleSendingAck(const ByteVector &blk) {
    bool ackNeeded;
    byte seq, subCmd;

    // Check if ack needed. In positive case the sequence number and command
    // are also returned to send back to requester
    tie(ackNeeded, seq, subCmd) = sparkSsr.needsAck(blk);
    if (ackNeeded) {
        DEBUG_PRINTLN("ACK required");
        if (operationMode_ == SPARK_MODE_AMP) {
            ackMsg = sparkMsg.sendAck(seq, subCmd, DIR_FROM_SPARK);
        } else {
            ackMsg = sparkMsg.sendAck(seq, subCmd, DIR_TO_SPARK);
        }

        DEBUG_PRINTLN("Sending acknowledgment");
        if (operationMode_ == SPARK_MODE_APP) {
            // Protocol ACKs bypass the ordinary multipart owner and looper
            // bookkeeping. Retain the legacy cursor advance, but send the
            // wire sequence supplied by sendAck(incoming seq, ...).
            writeSparkProtocolAck(ackMsg, nextMessageNum, [](const CmdData &ack) {
                ByteVector block = ack.data;
                return sendMessageToBT(block);
            });
        } else if (operationMode_ == SPARK_MODE_AMP) {
            bleControl->notifyClients(ackMsg);
        }
    }
}

void SparkDataControl::handleAmpModeRequest() {

    vector<CmdData> msg;
    vector<CmdData> currentMessage = sparkSsr.lastMessage();
    byte currentMessageNum = statusObject.lastMessageNum();
    byte subCmd_ = currentMessage.back().subcmd;
    SparkPresetControl &presetControl = SparkPresetControl::getInstance();

    Preset preset;

    MessageType lastMessageType = statusObject.lastMessageType();
    bool sendMessage = true;
    switch (lastMessageType) {

    case MSG_REQ_SERIAL:
        DEBUG_PRINTLN("Found request for serial number");
        msg = sparkMsg.sendSerialNumber(
            currentMessageNum);
        break;
    case MSG_REQ_FW_VER:
        DEBUG_PRINTLN("Found request for firmware version");
        msg = sparkMsg.sendFirmwareVersion(
            currentMessageNum);
        break;
    case MSG_REQ_PRESET_CHK:
        DEBUG_PRINTLN("Found request for hw checksum");
        msg = sparkMsg.sendHWChecksums(currentMessageNum, checksums);
        break;
    case MSG_REQ_CURR_PRESET_NUM:
        DEBUG_PRINTLN("Found request for hw preset number");
        msg = sparkMsg.sendHWPresetNumber(currentMessageNum);
        break;
    case MSG_REQ_CURR_PRESET:
        DEBUG_PRINTLN("Found request for current preset");
        preset = presetControl.activePreset();
        preset.presetNumber = 127;
        msg = sparkMsg.changePreset(preset, DIR_FROM_SPARK,
                                    currentMessageNum);
        break;
    case MSG_REQ_PRESET1:
        DEBUG_PRINTLN("Found request for preset 1");
        preset = presetControl.getPreset(1, 1);
        preset.presetNumber = 0;
        msg = sparkMsg.changePreset(preset, DIR_FROM_SPARK, currentMessageNum);
        break;
    case MSG_REQ_PRESET2:
        DEBUG_PRINTLN("Found request for preset 2");
        preset = presetControl.getPreset(1, 2);
        preset.presetNumber = 1;
        DEBUG_PRINTF("Preset NUMBER after init: %02X\n", preset.presetNumber);
        msg = sparkMsg.changePreset(preset, DIR_FROM_SPARK, currentMessageNum);
        break;
    case MSG_REQ_PRESET3:
        DEBUG_PRINTLN("Found request for preset 3");
        preset = presetControl.getPreset(1, 3);
        preset.presetNumber = 2;
        msg = sparkMsg.changePreset(preset, DIR_FROM_SPARK, currentMessageNum);
        break;
    case MSG_REQ_PRESET4:
        DEBUG_PRINTLN("Found request for preset 4");
        preset = presetControl.getPreset(1, 4);
        preset.presetNumber = 3;
        msg = sparkMsg.changePreset(preset, DIR_FROM_SPARK, currentMessageNum);
        break;
    case MSG_REQ_AMP_STATUS:
        DEBUG_PRINTLN("Found request for amp status");
        msg = sparkMsg.sendAmpStatus(currentMessageNum);
        break;
    case MSG_REQ_72:
        DEBUG_PRINTLN("Found request for 02 72");
        msg = sparkMsg.sendResponse72(currentMessageNum);
        break;
    default:
        DEBUG_PRINTF("Found invalid request: %d \n", lastMessageType);
        sendMessage = false;
        break;
    }
    if (sendMessage) {
        bleControl->notifyClients(msg);
    }
}

void SparkDataControl::handleAppModeResponse() {

    string msgStr = sparkSsr.getJson();
    MessageType lastMessageType = statusObject.lastMessageType();
    byte lastMessageNumber = statusObject.lastMessageNum();
    // DEBUG_PRINTF("Last message number: %s\n", SparkHelper::intToHex(lastMessageNumber).c_str());

    if (operationMode_ == SPARK_MODE_APP) {
        bool printMessage = false;

        if (lastMessageType == MSG_TYPE_AMP_NAME) {
            DEBUG_PRINTLN("Last message was amp name.");
            sparkAmpName = statusObject.ampName();
            setAmpParameters();
            retainedIntents_.request(SparkRetainedIntents::Checksums);
            printMessage = true;
            // The PanelLan profile uses this as the command-readiness gate.
            // The original source left it disabled, causing valid reconnects
            // to remain permanently "not ready".
            ampNameReceived_ = true;
        }

        if (lastMessageType == MSG_TYPE_AMP_SERIAL) {
            DEBUG_PRINTLN("Last message was serial number.");
            // The initial model response may have loaded files before the
            // serial was known. Invalidate that cache immediately, rather than
            // briefly exposing another identity's data while re-querying name.
            cancelHWPresetRead();
            SparkPresetControl::getInstance().setAmpParameters(sparkAmpName);
            // reading HW checksums for cache
            retainedIntents_.request(SparkRetainedIntents::AmpName);
            printMessage = true;
        }

        if (lastMessageType == MSG_TYPE_HWCHECKSUM) {
            printMessage = true;
            SparkPresetControl &presetControl = SparkPresetControl::getInstance();
            presetControl.validateChecksums(statusObject.hwChecksums());
            if (pendingHWPresetSlot_ && pendingHWPresetChecksums_ != presetControl.hardwareCacheGeneration())
                cancelHWPresetRead();
#if defined(PANELAN_LVGL_UI_MODE)
            // The PanelLan controller requests the current full preset after
            // it has observed the hardware-preset number. Do not replay a
            // filesystem selection or issue a competing full-preset query.
#else
            // try to load last selected preset from filesystem,
            // if not available, read current preset from amp
            if (!presetControl.readLastPresetFromFile()) {
                retainedIntents_.request(SparkRetainedIntents::CurrentPreset);
            };
#endif
        }

        if (lastMessageType == MSG_TYPE_HWPRESET) {
            DEBUG_PRINTLN("Received HW Preset response");

            int sparkPresetNumber = statusObject.currentPresetNumber();
            const auto &receivedMessage = sparkSsr.lastMessage().back();
            // Record the wire identity before any later response overwrites
            // SparkStatus. Store broadcasts too, for verification triggers.
            if (receivedMessage.cmd == 0x03 &&
                (receivedMessage.subcmd == 0x10 || receivedMessage.subcmd == 0x38))
                hardwareNumberEvents_.record({static_cast<uint8_t>(sparkPresetNumber),
                    receivedMessage.cmd, receivedMessage.subcmd, lastMessageNumber});

            // only change active presetNumber if new number is between 1 and max HW presets,
            // otherwise it is a custom preset number and can be ignored
            SparkPresetControl &presetControl = SparkPresetControl::getInstance();
            int numberOfHWPresets = presetControl.numberOfHWBanks() * PRESETS_PER_BANK;
            if (lastMessageNumber != specialMsgNum && sparkPresetNumber >= 1 && sparkPresetNumber <= numberOfHWPresets) {
                // check if this improves behavior, updating activePreset when new HW preset received
                SparkPresetControl::getInstance().updateFromSparkResponseHWPreset(sparkPresetNumber);
            } else {
                DEBUG_PRINTLN("Received custom preset number (128), ignoring number change");
            }
            printMessage = true;
        }

        if (lastMessageType == MSG_TYPE_PRESET) {
            DEBUG_PRINTLN("Last message was a preset change.");
            // This preset number is between 0 and 3!
            bool isSpecial = lastMessageNumber == specialMsgNum;
            // Cache fetches are neither active observations nor raw payload
            // diagnostics. Reject stale/mismatched replies before persisting.
            const Preset &received = statusObject.currentPreset();
            const auto hwChecksums = statusObject.hwChecksums();
            const int slot = received.presetNumber;
            SparkPresetControl &presets = SparkPresetControl::getInstance();
#ifdef PANELAN_PRESET_TRACE
            const uint8_t expectedPresetMsg = controllerFullPresetMessageNumber_;
            const bool gateMatch = expectedPresetMsg != 0 && lastMessageNumber == expectedPresetMsg;
            const uint32_t revisionBefore = fullPresetObservationRevision_;
            const bool ownerActive = responseLane_.active();
            const uint8_t ownerMsg = responseLane_.traceMessageNumber();
            const uint8_t ownerSub = responseLane_.traceSubcommand();
#endif
            const bool validCacheResponse = isSpecial && pendingHWPresetSlot_ != 0 &&
                isAmpConnected() && pendingHWPresetLink_ == linkGeneration_ &&
                !pendingHWPresetSerial_.empty() && pendingHWPresetSerial_ == statusObject.ampSerialNumber() &&
                pendingHWPresetChecksums_ == presets.hardwareCacheGeneration() &&
                presets.hardwareChecksumsReady() && slot == pendingHWPresetSlot_ - 1 &&
                slot < SparkPresetControl::getInstance().numberOfHWBanks() * PRESETS_PER_BANK &&
                slot < static_cast<int>(hwChecksums.size()) && received.checksum == hwChecksums[slot];
            if (validCacheResponse) {
                Serial.printf("HW name cache: accepted slot %d checksum=%02x name=%s\n",
                              slot + 1, received.checksum, received.name.c_str());
                cancelHWPresetRead();
                presets.updateFromSparkResponsePreset(true);
            } else if (!isSpecial
#if defined(PANELAN_LVGL_UI_MODE)
                       && controllerFullPresetMessageNumber_ != 0 &&
                       lastMessageNumber == controllerFullPresetMessageNumber_
#endif
                       ) {
                presets.updateFromSparkResponsePreset(false);
            } else {
                Serial.printf("HW name cache/full preset: rejected slot=%d pending=%u ready=%u checksum=%02x expected=%02x\n",
                              slot + 1, pendingHWPresetSlot_, presets.hardwareChecksumsReady(),
                              received.checksum,
                              slot >= 0 && slot < static_cast<int>(hwChecksums.size()) ? hwChecksums[slot] : 0);
            }
            const bool acceptedActive = !isSpecial
#if defined(PANELAN_LVGL_UI_MODE)
                && controllerFullPresetMessageNumber_ != 0 && lastMessageNumber == controllerFullPresetMessageNumber_
#endif
                ;
            if (!acceptedActive) statusObject.resetPresetUpdateFlag();
            if (acceptedActive) {
                printMessage = true;
                recordCompletedFullPreset();
                // This advances only after the full response has become the
                // active Spark-owned preset. Cached-background responses,
                // ACKs, and local pending mutations never advance it.
                fullPresetObservationMessageNumber_ = lastMessageNumber;
                ++fullPresetObservationRevision_;
#ifdef PANELAN_PRESET_TRACE
                Serial.printf("PRESET_TRACE t=%lu event=preset_observation_publish msg=%u slot=%d revision=%lu\n",
                              (unsigned long)millis(), lastMessageNumber, slot,
                              (unsigned long)fullPresetObservationRevision_);
#endif
#if defined(PANELAN_LVGL_UI_MODE)
#ifdef PANELAN_PRESET_TRACE
                Serial.printf("PRESET_TRACE t=%lu event=controller_full_revoke path=accepted_observation prior=%u\n",
                              (unsigned long)millis(), controllerFullPresetMessageNumber_);
#endif
                controllerFullPresetMessageNumber_ = 0;
#endif
            }
#ifdef PANELAN_PRESET_TRACE
            Serial.printf("PRESET_TRACE t=%lu event=preset_apply_gate msg=%u expected=%u match=%u owner_active=%u owner_msg=%u owner_sub=%02X slot=%d cache=%u acceptedActive=%u revision_before=%lu revision_after=%lu\n",
                          (unsigned long)millis(), lastMessageNumber, expectedPresetMsg, gateMatch,
                          ownerActive, ownerMsg, ownerSub, slot, validCacheResponse, acceptedActive,
                          (unsigned long)revisionBefore, (unsigned long)fullPresetObservationRevision_);
#endif
        }

        if (lastMessageType == MSG_TYPE_FX_ONOFF) {
            DEBUG_PRINTLN("Last message was a effect change.");
            Pedal receivedEffect = statusObject.currentEffect();
            SparkPresetControl::getInstance().toggleFX(receivedEffect);
            bool foundModel = false;
            for (auto &entry : fxModelObservationRevisions_) {
                if (entry.first == receivedEffect.name) {
                    ++entry.second;
                    foundModel = true;
                    break;
                }
            }
            if (!foundModel) {
                fxModelObservationRevisions_.push_back({receivedEffect.name, 1});
            }
            printMessage = true;
        }

        if (lastMessageType == MSG_TYPE_AMPSTATUS) {
            DEBUG_PRINTLN("Last message was amp status");
            int batteryLevel = SparkStatus::getInstance().ampBatteryLevel();
            Serial.printf("Battery level = %d\n", batteryLevel);
        }

        if (lastMessageType == MSG_TYPE_LOOPER_SETTING) {
            DEBUG_PRINTLN("New Looper setting received.");
            printMessage = true;
            ++looperSettingsObservationRevision_;
        }

        if (lastMessageType == MSG_TYPE_LOOPER_STATUS) {
            DEBUG_PRINTLN("New Looper status received (reading only number of loops)");
            int numOfLoops = statusObject.numberOfLoops();
            looperControl_.loopCount() = numOfLoops;
            if (numOfLoops > 0) {
                looperControl_.isRecAvailable() = true;
            }
            printMessage = true;
            ++looperStatusObservationRevision_;
        }

        if (lastMessageType == MSG_TYPE_LOOPER_COMMAND) {
            DEBUG_PRINTLN("New Looper command received.");
            updateLooperCommand(statusObject.lastLooperCommand());
            ++looperCommandObservationRevision_;
        }

        if (lastMessageType == MSG_TYPE_TAP_TEMPO) {
            DEBUG_PRINTLN("New BPM setting received.");
            printMessage = true;
        }

        if (lastMessageType == MSG_TYPE_MEASURE) {
            DEBUG_PRINTLN("Measure info received.");
            // float currentMeasure = sparkSsr.getMeasure();
            // looperControl_.setMeasure(currentMeasure);
        }

        if (lastMessageType == MSG_TYPE_TUNER_OUTPUT) {
            // Some Spark 2 sessions emit pitch data before a separate
            // TUNER_ON indication. It can establish an external session,
            // except for the short queued-packet tail after an explicit exit.
            if (subMode_ != SUB_MODE_TUNER &&
                static_cast<int32_t>(millis() - ignoreTunerOutputUntilMs_) >= 0) {
                Serial.println("External tuner output received; marking tuner active.");
                subMode_ = SUB_MODE_TUNER;
            }
        }

        if (lastMessageType == MSG_TYPE_TUNER_ON) {
            // Do not call switchSubMode here: this is an observation from the
            // amp, not a local request, and switchSubMode sends tuner commands.
            Serial.println("External tuner ON received; marking tuner active.");
            subMode_ = SUB_MODE_TUNER;
            SparkPresetControl::getInstance().updatePendingWithActive();
        }

        if (lastMessageType == MSG_TYPE_TUNER_OFF) {
            // As above, do not echo an amp transition back to the amp.
            Serial.println("External tuner OFF received; returning to preset mode.");
            subMode_ = SUB_MODE_PRESET;
            SparkPresetControl::getInstance().updatePendingWithActive();
        }

        if (lastMessageType == MSG_TYPE_INPUT_VOLUME) {
            DEBUG_PRINTLN("Input volume received.");
            printMessage = true;
        }

        if (msgStr.length() > 0 && printMessage) {
            Serial.println("Message processed:");
            Serial.println(msgStr.c_str());
        }
    }

    if (operationMode_ == SPARK_MODE_AMP) {
        if (lastMessageType == MSG_TYPE_PRESET) {

            SparkPresetControl::getInstance().updateFromSparkResponseAmpPreset(&msgStr[0]);
            statusObject.resetPresetUpdateFlag();
            statusObject.resetPresetNumberUpdateFlag();
        }
    }
    statusObject.resetLastMessageType();
}

void SparkDataControl::handleIncomingAck() {

    // if last Ack was for preset change (0x01 / 0x38) or effect switch (0x15),
    // confirm pending preset into active
    SparkPresetControl &presetControl = SparkPresetControl::getInstance();

    AckData lastAck = sparkSsr.getLastAckAndEmpty();
    if (lastAck.cmd == 0x05) { // 05 is intermediate ack, not last message
        DEBUG_PRINTLN("Received intermediate ACK");
        writeTelemetryId_ = lastMutationTelemetryId_;
        currentCommand.onIntermediateAck(lastAck, writeRequest);
        writeTelemetryId_ = 0;
    }
    if (lastAck.cmd == 0x04) {
        lastFinalAck_ = lastAck;
        ++finalAckRevision_;
        finalAckEvents_.record(lastAck);
        DEBUG_PRINTLN("Received final ACK");
        if (lastAck.subcmd == 0x01) {
            // only execute preset number change on last ack for preset change
            if (customPresetNumberChangePending) {
                customPresetNumberChangeReady_ = true;
            }
        }
        if (lastAck.subcmd == 0x38) {
            DEBUG_PRINTLN("Received ACK for 0x38 command");
#if !defined(PANELAN_LVGL_UI_MODE)
            // Controller mode must never apply/persist a potentially stale ACK.
            // HW number and correlated full-preset replies own active state.
            presetControl.updateFromSparkResponseACK();
            presetControl.writeCurrentPresetToFile();
#endif
            Serial.println("OK");
        }
        if (lastAck.subcmd == 0x15) {
            SparkPresetControl::getInstance().updateActiveWithPendingPreset();
            Serial.println("OK");
        }
        if (lastAck.subcmd == 0x75) {
            byte msgNum = lastAck.msgNum;
            byte looperCommand;
            for (auto it = pendingLooperAcks.begin(); it != pendingLooperAcks.end(); /*NOTE: no incrementation of the iterator here*/) {
                byte itMsgNum = (*it).msgNum;
                if (itMsgNum == msgNum) {
                    looperCommand = (*it).detail;
                    it = pendingLooperAcks.erase(it); // erase returns the next iterator
                } else {
                    ++it; // otherwise increment it by yourself
                }
            }
            updateLooperCommand(looperCommand);
            Serial.println(looperControl_.getLooperStatus().c_str());
        }
    }
}

void SparkDataControl::cancelHWPresetRead() {
    pendingHWPresetSlot_ = 0;
    pendingHWPresetSerial_.clear();
}

bool SparkDataControl::readHWPreset(int num) {

    // in case HW presets are missing from the cache, they can be requested
    SparkPresetControl &presets = SparkPresetControl::getInstance();
    if (pendingHWPresetSlot_ || !isAmpConnected() || !presets.hardwareChecksumsReady() ||
        statusObject.ampSerialNumber().empty() || num < 1 ||
        num > presets.numberOfHWBanks() * PRESETS_PER_BANK) return false;
    Serial.printf("Reading missing HW preset %d\n", num);
    pendingHWPresetSlot_ = num;
    pendingHWPresetSerial_ = statusObject.ampSerialNumber();
    pendingHWPresetLink_ = linkGeneration_;
    pendingHWPresetChecksums_ = presets.hardwareCacheGeneration();
    currentMsg = sparkMsg.getCurrentPreset(specialMsgNum, num);
    if (triggerCommand(currentMsg)) return true;
    cancelHWPresetRead();
    return false;
}

/////////////////////////////////////////////////////////
// BLUETOOTH RELATED
/////////////////////////////////////////////////////////

void SparkDataControl::startBLEServer() {
    bleControl->startServer();
}

bool SparkDataControl::checkBLEConnection() {
    if (bleControl->consumeReconnectRequest()) {
        resetStatus();
        if (!bleControl->isScanning()) {
            bleControl->startScan();
        }
    }
    if (bleControl->isAmpConnected()) {
        return true;
    }
    if (bleControl->isConnectionFound()) {
        if (bleControl->connectToServer()) {
            if (bleControl->subscribeToNotifications(&bleNotificationCallback)) {
                // The initial model query selects the correct Spark 2 / NEO
                // transport parameters.  It must run only after the GATT
                // notification channel is usable.
                retainedIntents_.request(SparkRetainedIntents::AmpName);
                serviceRetainedIntents();
                Serial.println("BLE connection to Spark established.");
                return true;
            }
            Serial.println("Spark notification setup failed; restarting scan");
            resetStatus();
            bleControl->startScan();
            return false;
        } else {
            Serial.println("Failed to connect, starting scan");
            resetStatus();
            bleControl->startScan();
            return false;
        }
    }
    return false;
}

void SparkDataControl::toggleBTMode() {

    if (operationMode_ == SPARK_MODE_AMP) {
        Serial.print("Switching Bluetooth mode to ");
        if (currentBTMode_ == BT_MODE_BLE) {
            Serial.println("Serial");
            currentBTMode_ = BT_MODE_SERIAL;
        } else if (currentBTMode_ == BT_MODE_SERIAL) {
            Serial.println("BLE");
            currentBTMode_ = BT_MODE_BLE;
        }
        // Save new mode to file
        File file = LittleFS.open(btModeFileName.c_str(), FILE_WRITE);
        file.print(currentBTMode_);
        file = LittleFS.open(sparkModeFileName.c_str(), FILE_WRITE);
        file.print(SPARK_MODE_AMP);
        file.close();
        Serial.println("Restarting in new BT mode");
        restartESP(false);
    }
}

bool SparkDataControl::isAmpConnected() {
    return bleControl->isAmpConnected();
}

uint32_t SparkDataControl::finalAckRevision() {
    return finalAckRevision_;
}

AckData SparkDataControl::lastFinalAck() {
    return lastFinalAck_;
}

bool SparkDataControl::nextFinalAck(uint32_t &cursor, AckData &ack) {
    return finalAckEvents_.next(cursor, ack);
}

uint32_t SparkDataControl::hardwareNumberRevision() {
    return hardwareNumberEvents_.revision();
}

bool SparkDataControl::nextHardwareNumber(uint32_t &cursor, uint8_t &number, uint8_t &cmd,
                                          uint8_t &subcmd, uint8_t &messageNumber) {
    HardwareNumberEvent event{};
    if (!hardwareNumberEvents_.next(cursor, event)) return false;
    number = event.number;
    cmd = event.cmd;
    subcmd = event.subcmd;
    messageNumber = event.messageNumber;
    return true;
}

uint32_t SparkDataControl::fxModelObservationRevision(const string &fxName) {
    for (const auto &entry : fxModelObservationRevisions_) {
        if (entry.first == fxName) {
            return entry.second;
        }
    }
    return 0;
}

uint32_t SparkDataControl::fullPresetObservationRevision() {
    return fullPresetObservationRevision_;
}

uint8_t SparkDataControl::fullPresetObservationMessageNumber() {
    return fullPresetObservationMessageNumber_;
}

bool SparkDataControl::firstQueryNotification(uint8_t messageNumber, uint32_t &atMs) {
    if (messageNumber == 0 || notificationQueryMessageNumber_ != messageNumber) return false;
    const uint32_t at = firstNotificationAt_.load();
    if (at == UINT32_MAX) return false;
    atMs = at;
    return true;
}

void SparkDataControl::expectControllerFullPreset(uint8_t messageNumber) {
#ifdef PANELAN_PRESET_TRACE
    const bool ownedController = responseLane_.owns(controllerFullPresetMessageNumber_, 0x01);
    if (messageNumber != 0) {
        Serial.printf("PRESET_TRACE t=%lu event=controller_full_expect path=expect prior=%u msg=%u owner_active=%u owner_msg=%u owner_sub=%02X owned=%u\n",
                      (unsigned long)millis(), controllerFullPresetMessageNumber_, messageNumber,
                      responseLane_.active(), responseLane_.traceMessageNumber(), responseLane_.traceSubcommand(),
                      ownedController);
    } else if (controllerFullPresetMessageNumber_ != 0) {
        Serial.printf("PRESET_TRACE t=%lu event=controller_full_revoke path=expect prior=%u owner_active=%u owner_msg=%u owner_sub=%02X owned=%u\n",
                      (unsigned long)millis(), controllerFullPresetMessageNumber_, responseLane_.active(),
                      responseLane_.traceMessageNumber(), responseLane_.traceSubcommand(), ownedController);
    }
#endif
    if (messageNumber == 0) {
        if (responseLane_.owns(controllerFullPresetMessageNumber_, 0x01)) {
            telemetry_.end(queryTelemetryId_, SparkTransportTelemetry::Reason::Revoked);
            queryTelemetryId_ = 0;
            notificationArmed_.store(false);
        }
        responseLane_.revokeControllerFullPreset(controllerFullPresetMessageNumber_);
    }
    controllerFullPresetMessageNumber_ = messageNumber;
}

uint32_t SparkDataControl::looperStatusObservationRevision() {
    return looperStatusObservationRevision_;
}

uint32_t SparkDataControl::looperSettingsObservationRevision() {
    return looperSettingsObservationRevision_;
}

uint32_t SparkDataControl::looperCommandObservationRevision() {
    return looperCommandObservationRevision_;
}

void SparkDataControl::recordBleDisconnect() { ++bleDisconnectCount_; }
void SparkDataControl::recordBleReconnect() { ++bleReconnectCount_; }
void SparkDataControl::recordIngressDropBusy() { ++ingressDropBusyCount_; persistentEventLog.record(PersistentEvent::IngressDropBusy, 0, true); }
void SparkDataControl::recordIngressDropFull() { ++ingressDropFullCount_; persistentEventLog.record(PersistentEvent::IngressDropFull, 0, true); }
void SparkDataControl::recordCompletedFullPreset() { ++completedFullPresetCount_; }
void SparkDataControl::recordControllerPresetSend() { ++controllerPresetSendCount_; presetTelemetryId_ = lastMutationTelemetryId_; telemetry_.pinSemantic(presetTelemetryId_, 0); }
void SparkDataControl::recordControllerPresetConfirm() { ++controllerPresetConfirmCount_; telemetry_.semantic(presetTelemetryId_, millis()); presetTelemetryId_ = 0; }
void SparkDataControl::recordControllerPresetFailure() { ++controllerPresetFailureCount_; telemetry_.end(presetTelemetryId_, SparkTransportTelemetry::Reason::Failed); presetTelemetryId_ = 0; }
void SparkDataControl::recordControllerFxSend() { ++controllerFxSendCount_; fxTelemetryId_ = lastMutationTelemetryId_; telemetry_.pinSemantic(fxTelemetryId_, 1); }
void SparkDataControl::recordControllerFxConfirm() { ++controllerFxConfirmCount_; telemetry_.semantic(fxTelemetryId_, millis()); fxTelemetryId_ = 0; }
void SparkDataControl::recordControllerFxFailure() { ++controllerFxFailureCount_; telemetry_.end(fxTelemetryId_, SparkTransportTelemetry::Reason::Failed); fxTelemetryId_ = 0; }
void SparkDataControl::recordTransportRetry() {
    // The replacement is the transaction being measured. The prior lane may
    // already have timed out and its id is deliberately no longer retained.
    if (responseLane_.active()) telemetry_.retry(queryTelemetryId_);
}

void SparkDataControl::printDiagnostics() {
    Serial.printf("diagnostics ble disconnect=%lu reconnect=%lu ingress busy=%lu full=%lu preset complete=%lu controller preset send=%lu confirm=%lu fail=%lu fx send=%lu confirm=%lu fail=%lu\n",
                  static_cast<unsigned long>(bleDisconnectCount_.load()),
                  static_cast<unsigned long>(bleReconnectCount_.load()),
                  static_cast<unsigned long>(ingressDropBusyCount_.load()),
                  static_cast<unsigned long>(ingressDropFullCount_.load()),
                  static_cast<unsigned long>(completedFullPresetCount_.load()),
                  static_cast<unsigned long>(controllerPresetSendCount_.load()),
                  static_cast<unsigned long>(controllerPresetConfirmCount_.load()),
                  static_cast<unsigned long>(controllerPresetFailureCount_.load()),
                  static_cast<unsigned long>(controllerFxSendCount_.load()),
                  static_cast<unsigned long>(controllerFxConfirmCount_.load()),
                  static_cast<unsigned long>(controllerFxFailureCount_.load()));
    Serial.printf("transport sent=%lu write_fail=%lu parsed=%lu semantic=%lu retry=%lu timeout=%lu invalidated=%lu ingress_hwm=%lu drops_busy=%lu drops_full=%lu\n",
                  (unsigned long)telemetry_.sent, (unsigned long)telemetry_.writeFailed,
                  (unsigned long)telemetry_.responses, (unsigned long)telemetry_.confirms,
                  (unsigned long)telemetry_.retries, (unsigned long)telemetry_.timeouts,
                  (unsigned long)telemetry_.invalidations, (unsigned long)ingressHighWater_.load(),
                  (unsigned long)ingressDropBusyCount_.load(), (unsigned long)ingressDropFullCount_.load());
    const SparkTransportTelemetry::Bucket *buckets[] = {&telemetry_.queueDelay, &telemetry_.firstResponse,
                                                         &telemetry_.completeResponse, &telemetry_.confirmation};
    const char *names[] = {"accept_to_write", "write_to_notify", "write_to_parse", "write_to_confirm"};
    for (unsigned i = 0; i < 4; ++i) {
        const auto &b = *buckets[i];
        Serial.printf("transport %s <100=%lu <500=%lu <2000=%lu <5000=%lu >=5000=%lu\n", names[i],
                      (unsigned long)b.count[0], (unsigned long)b.count[1], (unsigned long)b.count[2],
                      (unsigned long)b.count[3], (unsigned long)b.count[4]);
    }
    if (telemetry_.lastId()) {
        const auto &r = telemetry_.recent(0);
        Serial.printf("transport id=%lu kind=%u reason=%u queue=%lu start=%lu end=%lu chunks=%u notify=%lu parsed=%lu semantic=%lu retries=%u resets=%u flags=%u\n",
                      (unsigned long)r.id, r.kind, (unsigned)r.reason, (unsigned long)r.accepted,
                      (unsigned long)r.sendStart, (unsigned long)r.sendEnd, r.chunks,
                      (unsigned long)r.firstNotification, (unsigned long)r.parsed, (unsigned long)r.semantic,
                      r.retries, r.invalidations,
                      (r.started ? 1 : 0) | (r.written ? 2 : 0) | (r.notified ? 4 : 0) |
                      (r.completed ? 8 : 0) | (r.confirmed ? 16 : 0));
    }
}

bool SparkDataControl::isAppConnected() {
    return bleControl->isAppConnected();
}

void SparkDataControl::bleNotificationCallback(
    NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
    size_t length, bool isNotify) {

    // Triggered when data is received from Spark Amp in APP mode
    //  Transform data into ByteVetor and process
#ifdef PANELAN_PRESET_TRACE
    const uint32_t id = ++nextIngressId_;
    const uint32_t atMs = millis();
    recordIngressTrace(IngressTraceKind::Seen, id, atMs, length);
#endif
    ByteVector chunk(pData, pData + length);
    // DEBUG_PRINT("Incoming block: ");
    // DEBUG_PRINTVECTOR(chunk);
    // DEBUG_PRINTLN();
    //  DEBUG_PRINTF("Is notify: %s\n", isNotify ? "true" : "false");
    //   Add incoming data to message queue for processing
#ifdef PANELAN_PRESET_TRACE
    queueNotification(chunk, id, atMs);
#else
    queueMessage(chunk);
#endif
    // DEBUG_PRINTF("Seding back data via notify.");
    // vector<ByteVector> notifyVector = { chunk };
    // bleControl->writeBLE(notifyVector, false, false);
}

void SparkDataControl::queueMessage(ByteVector &blk) {
#ifdef PANELAN_PRESET_TRACE
    // Server/app writes use the same queue but are not BLE notifications.
    queueNotification(blk, 0, millis());
}

void SparkDataControl::queueNotification(ByteVector &blk, uint32_t id, uint32_t atMs) {
#endif
    if (blk.empty() || !msgQueueMutex) {
#ifdef PANELAN_PRESET_TRACE
        recordIngressTrace(IngressTraceKind::Skip, id, millis(), blk.size());
#endif
        return;
    }

    // NimBLE invokes this from a task, not an interrupt. A small bounded wait
    // avoids losing a one-shot Spark response while the controller loop is
    // popping/resetting the queue; it never waits through protocol parsing.
    if (xSemaphoreTake(msgQueueMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
#ifdef PANELAN_PRESET_TRACE
        recordIngressTrace(IngressTraceKind::DropBusy, id, millis(), blk.size());
#endif
        recordIngressDropBusy();
        ingressInvalidated_.store(true);
#ifndef PANELAN_PRESET_TRACE
        Serial.println("Dropping Spark notification: ingress queue busy");
#endif
        return;
    }

    const bool full = msgQueue.size() >= kMaxQueuedNotifications;
    if (full) {
        recordIngressDropFull();
        msgQueue = {};
        ingressInvalidated_.store(true);
#ifndef PANELAN_PRESET_TRACE
        Serial.println("Dropping Spark notification: ingress queue full");
#endif
    } else {
#ifdef PANELAN_PRESET_TRACE
        msgQueue.push({blk, id, atMs});
#else
        msgQueue.push(blk);
#endif
        if (notificationArmed_.load()) {
            uint32_t unset = UINT32_MAX;
            firstNotificationAt_.compare_exchange_strong(unset, millis());
        }
        const uint32_t depth = msgQueue.size();
        uint32_t high = ingressHighWater_.load();
        while (depth > high && !ingressHighWater_.compare_exchange_weak(high, depth)) {}
    }
    xSemaphoreGive(msgQueueMutex);
#ifdef PANELAN_PRESET_TRACE
    recordIngressTrace(full ? IngressTraceKind::DropFull : IngressTraceKind::Enqueue,
                       id, millis(), blk.size());
#endif
}

bool SparkDataControl::takeQueuedMessage(ByteVector &message
#ifdef PANELAN_PRESET_TRACE
                                         , uint32_t &id, uint32_t &atMs
#endif
                                         ) {
    if (!msgQueueMutex || xSemaphoreTake(msgQueueMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    const bool hasMessage = !msgQueue.empty();
    if (hasMessage) {
#ifdef PANELAN_PRESET_TRACE
        id = msgQueue.front().id;
        atMs = msgQueue.front().atMs;
        message = std::move(msgQueue.front().bytes);
#else
        message = std::move(msgQueue.front());
#endif
        msgQueue.pop();
    }
    xSemaphoreGive(msgQueueMutex);
    return hasMessage;
}

void SparkDataControl::clearQueuedMessages() {
    if (!msgQueueMutex || xSemaphoreTake(msgQueueMutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    msgQueue = {};
    xSemaphoreGive(msgQueueMutex);
}

bool SparkDataControl::sendMessageToBT(ByteVector &msg, size_t *chunks) {
    DEBUG_PRINTLN("Sending message via BT.");
    return bleControl->writeBLE(msg, withDelay, false, chunks);
}

/////////////////////////////////////////////////////////
// KEYBOARD RELATED
/////////////////////////////////////////////////////////

void SparkDataControl::sendButtonPressAsKeyboard(keyboardKeyDefinition k) {
    if (bleKeyboard.isConnected()) {

        Serial.printf("Sending button: %d - mod: %d - repeat: %d\n", k.key, k.modifier, k.repeat);
        if (k.modifier != 0)
            bleKeyboard.press(k.modifier);
        for (uint8_t i = 0; i <= k.repeat; i++) {
            bleKeyboard.write(k.key);
        }
        if (k.modifier != 0)
            bleKeyboard.release(k.modifier);
        lastKeyboardButtonPressed_ = k.keyUid;
        lastKeyboardButtonPressedString_ = k.display;
    } else {
        Serial.println("Keyboard not connected");
    }
}

void SparkDataControl::resetLastKeyboardButtonPressed() {
    lastKeyboardButtonPressed_ = 0;
    lastKeyboardButtonPressedString_ = "";
}

/////////////////////////////////////////////////////////
// LOOPER FUNCTIONS
/////////////////////////////////////////////////////////

/* Looper functions:
//
// 02 = RECORD count in (done)
// 04 = RECORD (done)
// 05 = STOPREC + PLAY(??)
// 06 = RETRY (followed by 02 and 04);
// 07 = RECORD finished??
// 08 = PLAY (done)
// 09 = STOP (done)
// 0b = RECORD (Dub) (done)
// 0c = STOP RECORD (done)
// 0d = UNDO (done)
// 0e = REDO (done)
// 0a = DELETE (done)
Looper commands end */

bool SparkDataControl::sparkLooperCommand(LooperCommand command) {

    currentMsg = sparkMsg.sparkLooperCommand(nextMessageNum, command);
    DEBUG_PRINTF("Spark Looper: %02x\n", command);

    return triggerCommand(currentMsg);
}

void SparkDataControl::tapTempoButton() {

    int bpm;
    unsigned long now = millis();
    unsigned long diff = now - lastTapButtonPressed_;
    DEBUG_PRINTF("Tap data: now = %u, lastTapButton = %u, diff = %u\n", now, lastTapButtonPressed_, diff);
    lastTapButtonPressed_ = now;

    if (diff > tapButtonThreshold_) {
        DEBUG_PRINTLN("Restarting tap calc");
        tapEntries.reset();
        bpm = 0;
    } else {
        tapEntries.add_element(diff);
        if (tapEntries.size() > 3) {
            int averageTime = tapEntries.averageValue();
            if (averageTime > 0) {
                bpm = 60000 / averageTime;
                bpm = min(bpm, 255);
                bpm = max(30, bpm);
                DEBUG_PRINTF("Tap tempo: %d\n", bpm);
                looperControl_.changeSettingBpm(bpm);
            }
        }
    }
}

bool SparkDataControl::switchTuner(bool on) {
    DEBUG_PRINTF("Switching Tuner %s\n", on ? "on" : "off");
    currentMsg = sparkMsg.switchTuner(nextMessageNum, on);
    return triggerCommand(currentMsg);
}

bool SparkDataControl::updateLooperSettings() {
    DEBUG_PRINTF("Updating looper settings: %s\n", looperControl_.looperSetting().getJson().c_str());
    currentMsg = sparkMsg.updateLooperSettings(nextMessageNum, looperControl_.looperSetting());
    return triggerCommand(currentMsg);
}

void SparkDataControl::startLooperTimer(void *args) {
    looperControl_.run(args);
}

void SparkDataControl::updateLooperCommand(byte lastCommand) {
    DEBUG_PRINTF("Last looper command: %02x\n", lastCommand);
    switch (lastCommand) {
    case 0x02:
        // Count in started
        break;
    case 0x04:
        // Record started
        looperControl_.isRecRunning() = true;
        break;
    case 0x05:
        // To be analyzed
        break;
    case 0x06:
        // Retry recording, maybe not required
        break;
    case 0x07:
        // Recording finished
        looperControl_.isRecRunning() = false;
        looperControl_.isRecAvailable() = true;
        break;
    case 0x08:
        // Play
        looperControl_.isPlaying() = true;
        break;
    case 0x09:
        // Stop
        looperControl_.isPlaying() = false;
        break;
    case 0x0A:
        // Delete
        looperControl_.resetStatus();
        break;
    case 0x0B:
        // Dub
        looperControl_.isRecRunning() = true;
        break;
    case 0x0C:
        // Stop Recording
        looperControl_.isRecRunning() = false;
        looperControl_.isRecAvailable() = true;
        break;
    case 0x0D:
        // UNDO
        looperControl_.canRedo() = true;
        break;
    case 0x0E:
        looperControl_.canRedo() = false;
        break;
    default:
        DEBUG_PRINTLN("Unknown looper command received.");
        break;
    }
    Serial.println(looperControl_.getLooperStatus().c_str());
}

bool SparkDataControl::sparkLooperStopAll() {
    bool stopReturn = sparkLooperStopPlaying();
    if (!stopReturn) return false;
    bool recStopReturn = sparkLooperStopRec();
    if (!recStopReturn) lastSubmissionStatus_ = SparkSubmission::Failed; // STOP already sent.
    return stopReturn && recStopReturn;
}

bool SparkDataControl::sparkLooperStopPlaying() {
    // looperControl_.isPlaying() = false;
    bool retValue = sparkLooperCommand(SPK_LOOPER_CMD_STOP);
    if (retValue) {
        looperControl_.stop();
        looperControl_.reset();
        retainedIntents_.request(SparkRetainedIntents::LooperStatus);
    }
    return retValue;
}

bool SparkDataControl::sparkLooperPlay() {
    // The local playback flag can outlive the last trustworthy Spark
    // observation. It may avoid restarting our timer, never suppress an
    // explicit native PLAY request or claim that request was sent.
    if (!sparkLooperCommand(SPK_LOOPER_CMD_PLAY)) return false;
    if (!(looperControl_.isPlaying())) looperControl_.start();
    return true;
}

bool SparkDataControl::sparkLooperRec() {
    bool countIn = looperControl_.looperSetting().click;
    if (countIn) {
        if (!sparkLooperCommand(SPK_LOOPER_CMD_COUNTIN)) return false;
        looperControl_.setCurrentBar(0);
    }
    looperControl_.start();
    recordStartFlag = true;
    return true;
}

bool SparkDataControl::sparkLooperDub() {
    bool retValue = sparkLooperCommand(SPK_LOOPER_CMD_DUB);
    if (!retValue) return false;
    // looperControl_.reset();
    retValue = retValue && sparkLooperCommand(SPK_LOOPER_CMD_PLAY);
    if (!retValue) lastSubmissionStatus_ = SparkSubmission::Failed; // DUB already sent; not safe to replay.
    if (retValue) {
        looperControl_.start();
        looperControl_.isPlaying() = true;
    }
    return retValue;
}

bool SparkDataControl::sparkLooperRetry() {
    if (!sparkLooperCommand(SPK_LOOPER_CMD_RETRY)) return false;
    looperControl_.reset();
    return sparkLooperRec();
}
// TODO: Get Looper status on startup and set flags accordingly

bool SparkDataControl::sparkLooperStopRec() {
    bool isRecAvailable = looperControl_.isRecAvailable();
    bool retVal = false;
    bool stopSent = false;
    if (isRecAvailable) {
        retVal = sparkLooperCommand(SPK_LOOPER_CMD_STOP_DUB);
        stopSent = retVal;
    } else {
        retVal = sparkLooperCommand(SPK_LOOPER_CMD_STOP_REC);
        stopSent = retVal;
        if (retVal && !sparkLooperCommand(SPK_LOOPER_CMD_REC_COMPLETE)) {
            retVal = false;
            lastSubmissionStatus_ = SparkSubmission::Failed; // STOP_REC already sent.
        }
        if (retVal) looperControl_.reset();
    }
    // A Busy first stop has not changed Spark: do not occupy the response
    // lane with a status query ahead of the queued controller retry.
    if (refreshLooperAfterStop(stopSent)) retainedIntents_.request(SparkRetainedIntents::LooperStatus);
    return retVal;
}

bool SparkDataControl::sparkLooperUndo() {
    if (looperControl_.canUndo()) {
        DEBUG_PRINTLN("UNDO possible");
        return sparkLooperCommand(SPK_LOOPER_CMD_UNDO);
    }
    return true;
}

bool SparkDataControl::sparkLooperRedo() {
    if (looperControl_.canRedo()) {
        DEBUG_PRINTLN("REDO possible");
        return sparkLooperCommand(SPK_LOOPER_CMD_REDO);
    }
    return true;
}

bool SparkDataControl::sparkLooperStopRecAndPlay() {
    // sparkLooperCommand(SPK_LOOPER_CMD_STOPREC);
    if (!sparkLooperStopRec()) return false;
    if (sparkLooperPlay()) return true;
    lastSubmissionStatus_ = SparkSubmission::Failed; // STOP_REC already sent.
    return false;
}

bool SparkDataControl::sparkLooperDeleteAll() {
    return sparkLooperCommand(SPK_LOOPER_CMD_DELETE);
}

bool SparkDataControl::sparkLooperPlayStop() {
    bool isRecRunning = looperControl_.isRecRunning();
    bool isPlaying = looperControl_.isPlaying();
    bool result = false;
    if (isRecRunning) {
        result = sparkLooperStopAll();
    } else if (isPlaying) {
        result = sparkLooperStopPlaying();
    } else {
        result = sparkLooperPlay();
    }
    return result;
}

bool SparkDataControl::sparkLooperRecDub() {
    bool isRecRunning = looperControl_.isRecRunning();
    bool isRecAvailable = looperControl_.isRecAvailable();
    int status = 2 * isRecAvailable + isRecRunning;
    bool result = false;

    switch (status) {
    case 0:
        // not running and not available
        result = sparkLooperRec();
        break;
    case 1:
        // not available but running
        result = sparkLooperStopRecAndPlay();
        break;
    case 2:
        // available, not running
        result = sparkLooperDub();
        break;
    case 3:
        // running and available
        result = sparkLooperStopRec();
        break;
    }
    return result;
}

bool SparkDataControl::sparkLooperUndoRedo() {
    // bool isRecRunning = looperControl_.isRecRunning();
    bool isRecAvailable = looperControl_.isRecAvailable();
    bool canRedo = looperControl_.canRedo();
    if (canRedo) {
        DEBUG_PRINTLN("REDO");
        return sparkLooperRedo();
    }
    if (isRecAvailable) {
        DEBUG_PRINTLN("UNDO");
        return sparkLooperUndo();
    }
    return false;
}

bool SparkDataControl::sparkLooperGetStatus() {
    bool retValue;
    currentMsg = sparkMsg.getLooperStatus(nextMessageNum);
    return triggerCommand(currentMsg);
}

bool SparkDataControl::sparkLooperGetConfig() {
    currentMsg = sparkMsg.getLooperConfig(nextMessageNum);
    return triggerCommand(currentMsg);
}

bool SparkDataControl::sparkLooperGetRecordStatus() {
    currentMsg = sparkMsg.getLooperRecordStatus(nextMessageNum);
    return triggerCommand(currentMsg);
}
