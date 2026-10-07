/*
 * SparkDataControl.cpp
 *
 *  Created on: 19.08.2021
 *      Author: stangreg
 */

#include "SparkStreamReader.h"
#include "SparkPresetChecksum.h"
#include <utility>

SparkStreamReader::SparkStreamReader() : message{}, unstructuredData{}, msgData{}, msgPos(0) {
#ifdef PANELAN_PRESET_TRACE
    const auto trace = +[](const SparkReceiveTraceEvent &event) {
        Serial.printf("PRESET_TRACE t=%lu event=%s reason=%s msg=%u cmd=%02X sub=%02X expected=%u received=%u first_id=%lu last_id=%lu partial_bytes=%u start=%u header=%u f7=%u frame_span_exact=%u\n",
                       static_cast<unsigned long>(millis()), event.event, event.reason,
                       event.message, event.command, event.subcommand,
                       static_cast<unsigned>(event.expected), static_cast<unsigned>(event.received),
                       static_cast<unsigned long>(event.firstIngressId),
                       static_cast<unsigned long>(event.lastIngressId),
                       static_cast<unsigned>(event.partialBytes), event.validStart,
                       event.headerValid, event.terminatorSeen, event.frameSpanExact);
    };
    frameReader_.setTrace(trace);
    assembly_.setTrace(trace);
#endif
}

string SparkStreamReader::getJson() {
    return sb.getJson();
}

void SparkStreamReader::setMessage(const vector<ByteVector> &msg_) {
    unstructuredData = msg_;
    message.clear();
}

byte SparkStreamReader::readByte() {
    if (msgPos < 0 || static_cast<size_t>(msgPos) >= msgData.size()) {
        parseValid_ = false;
        return 0;
    }
    return msgData[msgPos++];
}

string SparkStreamReader::readPrefixedString() {

    (void)readByte();
    // offset removed from string length byte to get real length
    int realStrLength = readByte() - 0xa0;
    string aStr = "";
    // reading string
    for (int i = 0; i < realStrLength; i++) {
        aStr += char(readByte());
    }
    return aStr;
}

string SparkStreamReader::readString() {
    byte aByte = readByte();
    int strLength;
    if (aByte == 0xd9) {
        aByte = readByte();
        strLength = aByte;
    } else if (aByte >= 0xa0) {
        strLength = aByte - 0xa0;
    } else {
        aByte = readByte();
        strLength = aByte - 0xa0;
    }

    string aStr = "";
    for (int i = 0; i < strLength; i++) {
        aStr += char(readByte());
    }
    return aStr;
}

// floats are special - bit 7 is actually stored in the format byte and not in the data
float SparkStreamReader::readFloat() {
    byte prefix = readByte(); // should be ca

    // using union struct to share memory for easy transformation of bytes to float
    union {
        float f;
        unsigned long ul;
    } u;

    byte a, b, c, d;
    a = readByte();
    b = readByte();
    c = readByte();
    d = readByte();
    u.ul = (a << 24) | (b << 16) | (c << 8) | d;
    float val = u.f;
    return val;
}

bool SparkStreamReader::readOnOff() {
    byte aByte = readByte();
    switch (aByte) {
    case 0xC3:
        return true;
        break;
    case 0xC2:
        return false;
        break;
    default:
        DEBUG_PRINTLN("Incorrect on/off state");
        return "?";
        break;
    }
}

void SparkStreamReader::readEffectParameter() {
    // Read object
    string effect = readPrefixedString();
    byte param = readByte();
    float val = readFloat();

    // Build string representations
    sb.startStr();
    sb.addStr("Effect", effect);
    sb.addSeparator();
    sb.addInt("Parameter", param);
    sb.addSeparator();
    sb.addFloat("Value", val);
    sb.endStr();

    // Set values
    statusObject.lastMessageType() = MSG_TYPE_FX_PARAM;
}

void SparkStreamReader::readEffect() {
    // Read object
    string effect1 = readPrefixedString();
    string effect2 = readPrefixedString();

    // Build string representations
    sb.startStr();
    sb.addStr("OldEffect", effect1);
    sb.addSeparator();
    sb.addNewline();
    sb.addStr("NewEffect", effect2);
    sb.endStr();

    // Set values
    statusObject.lastMessageType() = MSG_TYPE_FX_CHANGE;
}

void SparkStreamReader::readHardwarePreset() {
    // Read object
    readByte();
    byte presetNum = readByte() + 1;

    // Build string representations
    sb.startStr();
    sb.addInt("New HW Preset number", presetNum);
    sb.endStr();

    // Set values
    if (presetNum != statusObject.currentPresetNumber()) {
        statusObject.isPresetNumberUpdated() = true;
    }
    statusObject.currentPresetNumber() = presetNum;
    statusObject.lastMessageType() = MSG_TYPE_HWPRESET;
}

void SparkStreamReader::readHWChecksums(byte subCmd) {

    vector<byte> checksums;
    sb.startStr();

    // determine number of HW presets based on amp type (subCmd)
    int numberOfPresets;
    if (subCmd == 0x2a) {
        numberOfPresets = 4;
    } else if (subCmd == 0x2b) {
        numberOfPresets = 8;
    }

    // Array prefix byte
    readByte();
    for (int i = 0; i < numberOfPresets; i++) {
        int sum = readInt();
        checksums.push_back(sum);
        sb.addStr("Checksum Preset " + to_string(i + 1), SparkHelper::intToHex(sum));
        if (i < numberOfPresets - 1) {
            sb.addSeparator();
        }
    }
    sb.endStr();

    statusObject.hwChecksums() = checksums;
    statusObject.lastMessageType() = MSG_TYPE_HWCHECKSUM;
}

void SparkStreamReader::readStoreHardwarePreset() {
    // Read object
    readByte();
    byte presetNum = readByte() + 1;

    // Build string representations
    sb.startStr();
    sb.addInt("NewStoredPreset", presetNum);
    sb.endStr();

    // Set values
    statusObject.lastMessageType() = MSG_TYPE_HWPRESET;
}

void SparkStreamReader::readEffectOnOff() {
    // Read object
    string effect = readPrefixedString();
    boolean isOn = readOnOff();

    statusObject.currentEffect().name = effect;
    statusObject.currentEffect().isOn = isOn;
    // Build string representations
    sb.startStr();
    sb.addStr("Effect", effect);
    sb.addSeparator();
    sb.addBool("IsOn", isOn);
    sb.endStr();

    // Set values
    statusObject.lastMessageType() = MSG_TYPE_FX_ONOFF;
    statusObject.isEffectUpdated() = true;
}

void SparkStreamReader::readPreset() {
    // Read object (Preset main data)
    // DEBUG_PRINTF("Free memory before reading preset: %d\n", xPortGetFreeHeapSize());

    /*DEBUG_PRINTLN("Parsing message:");
    DEBUG_PRINTVECTOR(msgData);
    DEBUG_PRINTLN();
    */

    Preset currentPreset;

    readByte();
    byte preset = readByte();
    // DEBUG_PRINTF("Read PresetNumber: %d\n", preset);
    currentPreset.presetNumber = preset;
    string uuid = readString();
    // DEBUG_PRINTF("Read UUID: %s\n", uuid.c_str());
    currentPreset.uuid = uuid;
    string name = readString();
    // DEBUG_PRINTF("Read Name: %s\n", name.c_str());
    currentPreset.name = name;
    string version = readString();
    // DEBUG_PRINTF("Read Version: %s\n", version.c_str());
    currentPreset.version = version;
    string descr = readString();
    // DEBUG_PRINTF("Read Description: %s\n", descr.c_str());
    currentPreset.description = descr;
    string icon = readString();
    // DEBUG_PRINTF("Read Icon: %s\n", icon.c_str());
    currentPreset.icon = icon;
    float bpm = readFloat();
    // DEBUG_PRINTF("Read BPM: %f\n", bpm);
    currentPreset.bpm = bpm;
    // Build string representations
    // DEBUG_PRINTF("Free memory before adds: %d\n", xPortGetFreeHeapSize());
    sb.startStr();
    sb.addInt("PresetNumber", preset);
    sb.addSeparator();
    sb.addStr("UUID", uuid);
    sb.addSeparator();
    sb.addNewline();
    sb.addStr("Name", name);
    sb.addSeparator();
    sb.addStr("Version", version);
    sb.addSeparator();
    sb.addStr("Description", descr);
    sb.addSeparator();
    sb.addStr("Icon", icon);
    sb.addSeparator();
    sb.addFloat("BPM", bpm, "python");
    sb.addSeparator();
    sb.addNewline();
    // DEBUG_PRINTF("Free memory after adds: %d\n", xPortGetFreeHeapSize());
    //  Read Pedal data (including string representations)

    // !!! number of pedals not used currently, assumed constant as 7 !!!
    // int num_effects = readByte() - 0x90;
    // DEBUG_PRINTF("Read Number of effects: %d\n", num_effects);
    sb.addPython("\"Pedals\": [");
    sb.addNewline();
    currentPreset.pedals = {};
    int numberOfPedals = currentPreset.numberOfPedals;
    for (int i = 0; i < numberOfPedals; i++) { // Fixed to 7, but could maybe also be derived from num_effects?
        Pedal currentPedal = {};
        // DEBUG_PRINTF("Reading Pedal %d:\n", i);
        string eStr = readString();
        // DEBUG_PRINTF("  Pedal name: %s\n", eStr.c_str());
        currentPedal.name = eStr;
        boolean eOnOff = readOnOff();
        // DEBUG_PRINTF("  Pedal state: %s\n", eOnOff);
        currentPedal.isOn = eOnOff;
        sb.addPython("{");
        sb.addStr("Name", eStr);
        sb.addSeparator();
        sb.addBool("IsOn", eOnOff);
        sb.addSeparator();
        int numOfParameters = readByte() - char(0x90);
        // DEBUG_PRINTF("  Number of Parameters: %d\n", numOfParameters);
        // DEBUG_PRINTF("Free memory before parameters: %d\n", xPortGetFreeHeapSize());
        sb.addPython("\"Parameters\":[");
        // Read parameters of current pedal
        currentPedal.parameters = {};
        for (int p = 0; p < numOfParameters; p++) {
            Parameter currentParameter = {};
            // DEBUG_PRINTF("  Reading parameter %d:\n", p);
            byte num = readByte();
            // DEBUG_PRINTF("    Parameter ID: %d\n", SparkHelper::intToHex(num));
            byte spec = readByte();
            // DEBUG_PRINTF("    Parameter Special: %s\n",
            //	SparkHelper::intToHex(spec));
            // DEBUG_PRINTF("Free memory before reading float: %d\n", xPortGetFreeHeapSize());
            float val = readFloat();
            // DEBUG_PRINTF("    Parameter Value: %f\n", val);
            currentParameter.number = num;
            currentParameter.special = spec;
            currentParameter.value = val;
            // sb.addInt("Parameter", num, "python");
            // sb.addStr("Special", SparkHelper::intToHex(spec), "python");
            // sb.addFloat("Value", val, "python");
            sb.addFloatPure(val, "python");
            if (p < numOfParameters - 1) {
                sb.addSeparator();
            }
            currentPedal.parameters.push_back(currentParameter);
            // DEBUG_PRINTF("Free memory after reading preset: %d\n", xPortGetFreeHeapSize());
        }

        sb.addPython("]");
        // deleteIndent();
        sb.addPython("}");
        if (i < numberOfPedals - 1) {
            sb.addSeparator();
            sb.addNewline();
        }
        currentPreset.pedals.push_back(currentPedal);
    }
    sb.addPython("],");
    sb.addNewline();
    byte chksum = 0;
    if (!parseValid_ || !readSparkPresetChecksum(msgData.data(), msgData.size(), msgPos, chksum)) {
        statusObject.resetLastMessageType();
        Serial.println("Discarding preset: invalid payload checksum or unsupported/truncated tail");
#ifdef PANELAN_PRESET_TRACE
        const size_t bodyEnd = msgPos < 0 ? 0 : static_cast<size_t>(msgPos);
        const size_t remaining = bodyEnd <= msgData.size() ? msgData.size() - bodyEnd : 0;
        uint8_t sum = 0;
        if (msgData.size() > 2)
            for (size_t i = 2; i + 1 < msgData.size(); ++i)
                sum = static_cast<uint8_t>(sum + msgData[i]);
        const bool tailShape = remaining == 1 ||
            (remaining == 11 && bodyEnd + 5 < msgData.size() &&
             msgData[bodyEnd] == 0xCA && msgData[bodyEnd + 5] == 0xCA);
        const bool checksumMatches = !msgData.empty() && sum == msgData.back();
        Serial.printf("PRESET_TRACE t=%lu event=preset_parse_reject msg=%u cmd=%02X sub=%02X bytes=%u body=%u remaining=%u parsed=%u shape=%u checksum=%u frames=%u\n",
                       static_cast<unsigned long>(millis()), statusObject.lastMessageNum(),
                       message.empty() ? 0 : message.back().cmd, message.empty() ? 0 : message.back().subcmd,
                      static_cast<unsigned>(msgData.size()), static_cast<unsigned>(bodyEnd),
                      static_cast<unsigned>(remaining), parseValid_, tailShape, checksumMatches,
                      static_cast<unsigned>(response.size()));
        for (size_t i = 0; i < response.size(); ++i) {
            const ByteVector &frame = response[i];
            if (frame.size() < 9) continue;
            Serial.printf("PRESET_TRACE t=%lu event=preset_reject_frame i=%u len=%u seq=%u cmd=%02X sub=%02X chunks=%u part=%u\n",
                          static_cast<unsigned long>(millis()), static_cast<unsigned>(i),
                          static_cast<unsigned>(frame.size()), frame[2], frame[4], frame[5],
                          frame[7], frame[8]);
        }
#endif
        return;
    }
    msgPos = msgData.size();
    currentPreset.checksum = chksum;
    sb.addStr("Checksum", SparkHelper::intToHex(chksum));
    sb.addNewline();
    sb.endStr();
    currentPreset.text = sb.getText();
    currentPreset.raw = sb.getRaw();
    currentPreset.json = sb.getJson();
    currentPreset.isEmpty = false;

    if (!parseValid_) {
        DEBUG_PRINTLN("Discarding truncated preset response.");
        return;
    }

    statusObject.currentPreset() = currentPreset;
    statusObject.isPresetUpdated() = true;
    statusObject.lastMessageType() = MSG_TYPE_PRESET;
#ifdef PANELAN_PRESET_TRACE
    Serial.printf("PRESET_TRACE t=%lu event=preset_parse_complete msg=%u cmd=%02X sub=%02X slot=%d frames=%u bytes=%u\n",
                  (unsigned long)millis(), statusObject.lastMessageNum(),
                  message.empty() ? 0 : message.back().cmd, message.empty() ? 0 : message.back().subcmd,
                  currentPreset.presetNumber, (unsigned)response.size(), (unsigned)msgData.size());
#endif
}

void SparkStreamReader::readLooperSettings() {

    DEBUG_PRINT("Reading looper settings:");
    DEBUG_PRINTVECTOR(msgData);
    DEBUG_PRINTLN();

    int bpm = readByte();
    // if the first byte is 0xCC, this is a prefix and the real BPM are in the next byte
    // CC is prefixed if the bpm is exceeding 128.
    if (bpm == 0xCC) {
        bpm = readByte();
    }
    int countByte = readByte();
    string countStr = countByte == 0x04 ? "straight" : "shuffle";
    int bars = readByte();
    bool freeIndicator = readOnOff();
    bool click = readOnOff();
    bool unknownOnOff = readOnOff();
    unsigned int maxDuration = readInt16();

    // Build string representations
    sb.startStr();
    sb.addInt("BPM", bpm);
    sb.addSeparator();
    sb.addStr("Count", countStr);
    sb.addSeparator();
    sb.addInt("Bars", bars);
    sb.addSeparator();
    sb.addBool("Free", freeIndicator);
    sb.addSeparator();
    sb.addBool("Click", click);
    sb.addSeparator();
    sb.addBool("Unknown switch", unknownOnOff);
    sb.addSeparator();
    sb.addInt("Max duration", maxDuration);
    sb.endStr();

    LooperSetting &looperSetting = statusObject.currentLooperSetting();
    looperSetting.bpm = bpm;
    looperSetting.countStr = countStr;
    looperSetting.count = countByte;
    looperSetting.bars = bars;
    looperSetting.freeIndicator = freeIndicator;
    looperSetting.click = click;
    looperSetting.unknownOnOff = unknownOnOff;
    looperSetting.maxDuration = maxDuration;

    looperSetting.json = sb.getJson();
    looperSetting.text = sb.getText();
    looperSetting.raw = sb.getRaw();

    statusObject.isLooperSettingUpdated() = true;
    statusObject.lastMessageType() = MSG_TYPE_LOOPER_SETTING;
}

void SparkStreamReader::readLooperCommand() {

    statusObject.lastLooperCommand() = readByte();
    DEBUG_PRINT("Received looper command: ");
    DEBUG_PRINTVECTOR(msgData);
    DEBUG_PRINTLN();
    statusObject.lastMessageType() = MSG_TYPE_LOOPER_COMMAND;
}

void SparkStreamReader::readLooperStatus() {
    // 4C0404004242
    int bpm = readByte();
    byte count = readByte();
    byte bars = readByte();
    int numberOfLoops = readByte();
    statusObject.numberOfLoops() = numberOfLoops;
    bool unknownOnOff1 = readByte();
    bool unknownOnOff2 = readByte();

    sb.startStr();
    sb.addInt("BPM", bpm);
    sb.addSeparator();
    sb.addInt("Count", count);
    sb.addSeparator();
    sb.addInt("Bars", bars);
    sb.addSeparator();
    sb.addInt("Loops", numberOfLoops);
    sb.addSeparator();
    sb.addStr("Unknown OnOff1", SparkHelper::intToHex(unknownOnOff1));
    sb.addSeparator();
    sb.addStr("Unknown OnOff2", SparkHelper::intToHex(unknownOnOff2));
    sb.endStr();

    statusObject.lastMessageType() = MSG_TYPE_LOOPER_STATUS;
}

void SparkStreamReader::readTapTempo() {
    float bpm = readFloat();

    sb.startStr();
    sb.addFloat("BPM", bpm, "python");
    sb.endStr();
    statusObject.lastMessageType() = MSG_TYPE_TAP_TEMPO;
}

void SparkStreamReader::readMeasure() {
    float measure = readFloat();
    statusObject.measure() = measure;

    sb.startStr();
    sb.addFloat("Measure", measure, "python");
    sb.endStr();
    statusObject.lastMessageType() = MSG_TYPE_MEASURE;
}

void SparkStreamReader::readTuner() {
    byte note = readByte();
    float offset = readFloat();
    statusObject.note() = note;
    statusObject.noteOffset() = offset;
    // Record the observation only after both values have been successfully
    // parsed. This is a receive generation, never a local tuner command.
    statusObject.recordTunerSample(millis());

    sb.startStr();
    sb.addInt("Note", note);
    sb.addFloat("Offset", offset, "python");
    sb.endStr();
    statusObject.lastMessageType() = MSG_TYPE_TUNER_OUTPUT;
}

void SparkStreamReader::readTunerOnOff() {
    // Read object
    boolean isOn = readOnOff();

    // Build string representations
    sb.startStr();
    sb.addBool("Tuner mode", isOn);
    sb.endStr();

    // Set values
    if (isOn) {
        statusObject.lastMessageType() = MSG_TYPE_TUNER_ON;
    } else {
        statusObject.lastMessageType() = MSG_TYPE_TUNER_OFF;
    }
}

void SparkStreamReader::readPresetRequest() {
    int type = readByte();
    if (type == 1) {
        statusObject.lastMessageType() = MSG_REQ_CURR_PRESET;
    } else {
        int presetNum = readByte();
        DEBUG_PRINTF("Request for preset %d\n", presetNum + 1);
        switch (presetNum) {
        case 0:
            statusObject.lastMessageType() = MSG_REQ_PRESET1;
            break;
        case 1:
            statusObject.lastMessageType() = MSG_REQ_PRESET2;
            break;
        case 2:
            statusObject.lastMessageType() = MSG_REQ_PRESET3;
            break;
        case 3:
            statusObject.lastMessageType() = MSG_REQ_PRESET4;
            break;
        default:
            DEBUG_PRINTF("Unknown preset number request: %d\n", presetNum);
            break;
        }
    }
}

void SparkStreamReader::readAmpStatus() {

    // 0a ?
    readByte();
    // 01 (always 01?)
    readByte();
    // 01 (powered(?))
    bool isBatteryPowered = readByte() == 0x01 ? true : false;
    // 00 (discharging) 01 (constant power) 02 (charging) 03 (full charged)
    int chargingStatus = readByte();
    // CD 0f CD (4045) (Battery level)
    // CD 06 73 (1651) (?)
    // 20  (?)
    // Default value when power cable is connected
    int batteryLevel = readInt();
    // Unknown UINT16/int and last byte
    readInt();
    readInt();

    SparkStatus &statusObject = SparkStatus::getInstance();
    statusObject.isAmpBatteryPowered() = isBatteryPowered;
    statusObject.ampBatteryLevel() = (BatteryLevel)batteryLevel;
    statusObject.ampBatteryChargingStatus() = (BatteryChargingStatus)chargingStatus;
    statusObject.lastMessageType() = MSG_TYPE_AMPSTATUS;
}

void SparkStreamReader::readSerialNumber() {
    string serialNumber = readString();
    // Serial number seems to come with additional F7 character at the end
    serialNumber = serialNumber.substr(0, serialNumber.length() - 1);

    // Build string representations
    sb.startStr();
    sb.addStr("Serial Number", serialNumber);
    sb.endStr();

    // Set values
    statusObject.lastMessageType() = MSG_TYPE_AMP_SERIAL;
    statusObject.ampSerialNumber() = serialNumber;
}

void SparkStreamReader::readInputVolume() {
    // Read object
    float volume = readFloat();
    // Build string representations
    sb.startStr();
    sb.addFloat("Input Volume", volume);
    sb.endStr();

    // Set values
    statusObject.lastMessageType() = MSG_TYPE_INPUT_VOLUME;
    statusObject.inputVolume() = volume;
    statusObject.isVolumeChanged() = true;
}

boolean SparkStreamReader::structureData(bool processHeader) {
    message.clear();
    vector<ByteVector> strippedFrames;
    if (processHeader) {
        for (const auto &block : unstructuredData) {
            if (block.size() < 16) return false;
            if (block.size() != block[6]) {
                DEBUG_PRINTF("Data is of size %d and reports %d\n", (int)block.size(), block[6]);
            }
            strippedFrames.emplace_back(block.begin() + 16, block.end());
        }
    }
    const auto &frames = processHeader ? strippedFrames : unstructuredData;
    // Assembly delivers complete checksum-valid frames, not a byte stream.
    // In particular, the sequence byte may itself be F7.
    for (const auto &frame : frames) {
        if (frame.size() < 7 || frame[0] != 0xF0 || frame[1] != 0x01 || frame.back() != 0xF7) {
            message.clear();
            return false;
        }
    }

    ByteVector concatData;
    for (const auto &frame : frames) {
        byte thisCmd = frame[4];
        byte thisSubCmd = frame[5];
        ByteVector data7bit = {};
        data7bit.assign(frame.begin() + 6, frame.end() - 1);

        // DEBUG_PRINTLN("Converted to 8bit");
        CmdData currData;
        currData.cmd = thisCmd;
        currData.subcmd = thisSubCmd;
        currData.data = convertDataTo8bit(data7bit);

        if ((thisCmd == 0x01 || thisCmd == 0x03) && thisSubCmd == 0x01 &&
            SparkReceiveAssembly::presetShape(frame, currData.data) == 2) {
            concatData.insert(concatData.end(), currData.data.begin() + 3, currData.data.end());
            if (currData.data[1] == currData.data[0] - 1) {
                currData.data = concatData;
                message.push_back(currData);
                concatData.clear();
            }
        } else {
            message.push_back(currData);
        }
    } // for all frames

    if (message.empty()) return false;
    statusObject.lastMessageNum() = frames.back()[2];
    return true;
}

void SparkStreamReader::setInterpreter(const ByteVector &_msg) {
    msgData = _msg;
    msgPos = 0;
    parseValid_ = true;
}

int SparkStreamReader::runInterpreter(byte _cmd, byte _subCmd) {
    // Message from APP to AMP
    if (_cmd == 0x01) {
        switch (_subCmd) {
        case 0x01:
            DEBUG_PRINTLN("01 01 - Reading preset");
            readPreset();
            break;
        case 0x04:
            DEBUG_PRINTLN("01 04 - Reading effect param");
            readEffectParameter();
            break;
        case 0x06:
            DEBUG_PRINTLN("01 06 - Reading effect");
            readEffect();
            break;
        case 0x15:
            DEBUG_PRINTLN("01 15 - Reading effect on/off");
            readEffectOnOff();
            break;
        case 0x38:
            DEBUG_PRINTLN("01 38 - Change to different preset");
            readHardwarePreset();
            break;
        default:
            DEBUG_PRINTF("%02x %02x - not handled: ", _cmd, _subCmd);
            DEBUG_PRINTVECTOR(msgData);
            DEBUG_PRINTLN();
            break;
        }

    }
    // Request to AMP
    else if (_cmd == 0x02) {
        DEBUG_PRINTLN("Reading request from Amp");
        switch (_subCmd) {
        case 0x23:
            DEBUG_PRINTLN("Found request for serial number");
            statusObject.lastMessageType() = MSG_REQ_SERIAL;
            break;
        case 0x2F:
            DEBUG_PRINTLN("Found request for firmware version");
            statusObject.lastMessageType() = MSG_REQ_FW_VER;
            break;
        case 0x2A:
            DEBUG_PRINTLN("Found request for hw checksum");
            statusObject.lastMessageType() = MSG_REQ_PRESET_CHK;
            break;
        case 0x10:
            DEBUG_PRINTLN("Found request for hw preset number");
            statusObject.lastMessageType() = MSG_REQ_CURR_PRESET_NUM;
            break;
        case 0x01:
            DEBUG_PRINTLN("Found request for current preset");
            readPresetRequest();
            break;
        case 0x71:
            DEBUG_PRINTLN("Found request for 02 71");
            statusObject.lastMessageType() = MSG_REQ_AMP_STATUS;
            break;
        case 0x72:
            DEBUG_PRINTLN("Found request for 02 72");
            statusObject.lastMessageType() = MSG_REQ_72;
            break;
        default:
            DEBUG_PRINTF("Found invalid request: %02x \n", _subCmd);
            statusObject.lastMessageType() = MSG_REQ_INVALID;
            break;
        }
    }
    // Message from AMP to APP
    else if (_cmd == 0x03) {
        switch (_subCmd) {
        case 0x01:
            DEBUG_PRINTLN("03 01 - Reading preset");
            readPreset();
            break;
        case 0x2a:
            DEBUG_PRINTLN("03 2A - Reading HW checksums");
            readHWChecksums(0x2a);
            break;
        case 0x2b:
            DEBUG_PRINTLN("03 2B - Reading HW checksums");
            readHWChecksums(0x2b);
            break;
        case 0x06:
            DEBUG_PRINTLN("03 06 - Reading effect");
            readEffect();
            break;
        case 0x11:
            DEBUG_PRINTLN("03 11 - Reading amp name");
            readAmpName();
            break;
        case 0x15:
            DEBUG_PRINTLN("03 15 - Reading effect on/off");
            readEffectOnOff();
            break;
        case 0x23:
            DEBUG_PRINTLN("03 23 - Reading serial number");
            readSerialNumber();
            break;
        case 0x27:
            DEBUG_PRINTLN("03 27 - Storing HW preset");
            readStoreHardwarePreset();
            break;
        case 0x37:
            DEBUG_PRINTLN("03 37 - Reading effect param");
            readEffectParameter();
            break;
        case 0x38:
        case 0x10:
            DEBUG_PRINTLN("03 38/10 - Reading HW preset");
            readHardwarePreset();
            break;
        case 0x63:
            DEBUG_PRINTLN("03 63 - Reading Tap Tempo");
            readTapTempo();
            break;
        case 0x64:
            DEBUG_PRINTLN("03 64 - Reading Tuner Output");
            readTuner();
            break;
        case 0x65:
            DEBUG_PRINTLN("03 65 - Tuner On/Off");
            readTunerOnOff();
            break;
        case 0x6B:
            DEBUG_PRINTLN("03 6B - Reading Input Volume");
            readInputVolume();
            break;
        case 0x71:
            DEBUG_PRINTLN("03 71 - Reading Amp Status");
            readAmpStatus();
            break;
        case 0x75:
            DEBUG_PRINTLN("03 75 - Reading Looper Record Status");
            readLooperCommand();
            break;
        case 0x76:
            DEBUG_PRINTLN("03 76 - Reading Looper settings");
            readLooperSettings();
            break;
        case 0x77:
            DEBUG_PRINTLN("03 77 - Reading current measure");
            readMeasure();
            break;
        case 0x78:
            DEBUG_PRINTLN("03 78 - Reading current Looper status");
            readLooperStatus();
            break;
        default:
            DEBUG_PRINTF("%02x %02x - not handled: ", _cmd, _subCmd);
            DEBUG_PRINTVECTOR(msgData);
            DEBUG_PRINTLN();
            break;
        }
    }
    // Acknowledgement
    else if (_cmd == 0x04 || _cmd == 0x05) {
        DEBUG_PRINT("ACK number ");
        byte lastMessageNum = statusObject.lastMessageNum();
        DEBUG_PRINTLN(lastMessageNum);
        byte detail = 0x00; // detail is only used for Acks received from Amp
        AckData ack;
        ack.msgNum = lastMessageNum;
        ack.cmd = _cmd;
        ack.subcmd = _subCmd;
        statusObject.acknowledgments().push_back(ack);
        DEBUG_PRINTF("Acknowledgment for command %02x %02x\n", _cmd, _subCmd);
    } else {
        // unprocessed command (likely the initial ones sent from the app
        DEBUG_PRINTF("Unprocessed: %02x, %02x - ", _cmd,
                     _subCmd);
        DEBUG_PRINTVECTOR(msgData);
        DEBUG_PRINTLN();
    }

    return 1;
}

tuple<bool, byte, byte> SparkStreamReader::needsAck(const ByteVector &blk) {

    if (blk.size() < 22) { // Block is too short, does not need acknowledgement
        return tuple<bool, byte, byte>(false, 0, 0);
    }
    byte direction[2] = {blk[4], blk[5]};
    byte lastMessageNum = blk[18];
    statusObject.lastMessageNum() = lastMessageNum;
    byte cmd = blk[20];
    byte subCmd = blk[21];

    byte msgToSpark[2] = {0x53, 0xFE};
    int msgToSparkCompare = memcmp(direction, msgToSpark, sizeof(direction));
    if (msgToSparkCompare == 0) {
        if (cmd == 0x01 && subCmd != 0x04) {
            // the app sent a message that needs a response
            return tuple<bool, byte, byte>(true, lastMessageNum, subCmd);
        }
    }

    return tuple<bool, byte, byte>(false, 0, 0);
}

AckData SparkStreamReader::getLastAckAndEmpty() {
    AckData lastAck;
    vector<AckData> acknowledgments = statusObject.acknowledgments();
    if (acknowledgments.size() > 0) {
        lastAck = acknowledgments.back();
        statusObject.resetAcknowledgments();
    }
    return lastAck;
}

MessageProcessStatus SparkStreamReader::processBlock(ByteVector &blk) {
#ifdef PANELAN_PRESET_TRACE
    Serial.printf("PRESET_TRACE t=%lu event=process_block id=%lu len=%u\n",
                  (unsigned long)millis(), (unsigned long)traceIngressId_, (unsigned)blk.size());
#endif

    /*
        DEBUG_PRINTLN("Processing block");
        DEBUG_PRINTVECTOR(blk);
        DEBUG_PRINTLN();
    */
    // Process:
    // 1. Remove 01FE header if present
    // 2. Build command (response) vector by splitting blocks into F001...F7 blocks

    // Remove 01FE header
    if (blk.size() >= 2 && blk[0] == 0x01 && blk[1] == 0xFE && blk.size() > 16) {
        // Block starts with 01FE and is long enough
        // Read meta data of block
        // Cut off header after extracting information
        blk.assign(blk.begin() + 16, blk.end());
    }
    // FROM HERE NO HEADER IS PRESENT ANYMORE and blk should start with F001 (after preprocessing)

    // Cut blk into chunks and append to response
    for (const auto &frame : frameReader_.accept(blk
#ifdef PANELAN_PRESET_TRACE
                                                    , traceIngressId_
#endif
                                                    )) {
#ifdef PANELAN_PRESET_TRACE
        // Header fields are safe only on checksum-valid complete wire frames.
        // A multipart prefix is meaningful only for a preset with valid shape.
        const bool preset = (frame[4] == 0x01 || frame[4] == 0x03) && frame[5] == 0x01;
        uint8_t count = 0, part = 0;
        if (preset) {
            const auto data = SparkReceiveAssembly::decoded(frame);
            if (SparkReceiveAssembly::presetShape(frame, data) == 2) {
                count = data[0];
                part = data[1];
            }
        }
        Serial.printf("PRESET_TRACE t=%lu event=frame_complete id=%lu len=%u msg=%u cmd=%02X sub=%02X count=%u part=%u\n",
                      (unsigned long)millis(), (unsigned long)traceIngressId_, (unsigned)frame.size(),
                      frame[2], frame[4], frame[5], count, part);
#endif
        SparkReceiveAssembly::Frames complete;
        const bool assembled = assembly_.accept(frame, complete);
#ifdef PANELAN_PRESET_TRACE
        Serial.printf("PRESET_TRACE t=%lu event=assembly_result id=%lu msg=%u cmd=%02X sub=%02X count=%u part=%u complete=%u frames=%u\n",
                      (unsigned long)millis(), (unsigned long)traceIngressId_, frame[2], frame[4], frame[5],
                      count, part, assembled, (unsigned)complete.size());
#endif
        if (!assembled) continue;
        pendingMessages_.push_back(std::move(complete));
    }

    return nextMessage();
}

MessageProcessStatus SparkStreamReader::nextMessage() {
    if (pendingMessages_.empty()) return MSG_PROCESS_RES_INCOMPLETE;
    response = std::move(pendingMessages_.front());
    pendingMessages_.pop_front();
    const bool request = !response.empty() && response.back().size() >= 7 && response.back()[4] == 0x02;
    setMessage(response);
    const bool parsed = !readMessage(false).empty();
    response.clear();
    if (!parsed) {
        message.clear();
        statusObject.lastMessageNum() = 0;
#ifdef PANELAN_PRESET_TRACE
        Serial.printf("PRESET_TRACE t=%lu event=stream_message_reject\n", (unsigned long)millis());
#endif
        return MSG_PROCESS_RES_REJECT;
    }
    return request ? MSG_PROCESS_RES_REQUEST : MSG_PROCESS_RES_COMPLETE;
}

void SparkStreamReader::interpretData() {
    for (auto msgData : message) {
        int thisCmd = msgData.cmd;
        int thisSubCmd = msgData.subcmd;
        ByteVector thisData = msgData.data;

        setInterpreter(thisData);
        runInterpreter(thisCmd, thisSubCmd);
    }
    // message.clear();
}

vector<CmdData> SparkStreamReader::readMessage(bool processHeader) {
    if (structureData(processHeader)) {
        interpretData();
    }
    return message;
}

bool SparkStreamReader::isValidBlockWithoutHeader(const ByteVector &blk) {

    // Checks done:
    // 1. Block has a length of at least 3
    // 2. Block starts with F0 01
    // 3. Block ends with F7

    if (blk.size() < 3)
        return false;
    if (blk[0] != 0xF0 || blk[1] != 0x01)
        return false;
    if (blk.back() != 0xF7)
        return false;

    return true;
}

int SparkStreamReader::readInt() {

    int result = 0;
    byte first = readByte();
    byte major = 0;
    byte minor = 0;
    // If int value is greater than 128 it is prefixed with 0xCC
    switch (first) {
    case 0xCC:
    case 0xD0:
        result = readByte();
        break;
    case 0xCD:
        major = readByte();
        minor = readByte();
        result = (major << 8 | minor);
        break;
    default:
        result = first;
        break;
    }

    return result;
}

unsigned int SparkStreamReader::readInt16() {
    // Read the following two bytes as INT
    // INT is prefixed with 0xCD
    byte prefix = readByte();
    byte major = readByte();
    byte minor = readByte();
    unsigned int result = (major << 8 | minor);
    return result;
}

void SparkStreamReader::clearMessageBuffer() {
    DEBUG_PRINTLN("Clearing response buffer.");
    response.clear();
    pendingMessages_.clear();
    frameReader_.reset();
    assembly_.reset();
}

void SparkStreamReader::reset() {
    response.clear();
    pendingMessages_.clear();
    frameReader_.reset();
    assembly_.reset();
    unstructuredData.clear();
    message.clear();
    msgData.clear();
    msgPos = 0;
    parseValid_ = true;
}

ByteVector SparkStreamReader::convertDataTo8bit(ByteVector input) {
    int chunkLength = input.size();
    // DEBUG_PRINT("Chunk_len:");
    // DEBUG_PRINTLN(chunkLength);
    int numOfSequences = int((chunkLength + 7) / 8);
    ByteVector data8bit = {};

    for (int thisSequence = 0; thisSequence < numOfSequences; thisSequence++) {
        int seqLength = min(8, chunkLength - (thisSequence * 8));
        ByteVector seq = {};
        byte bit8 = input[thisSequence * 8];
        for (int ind = 0; ind < seqLength - 1; ind++) {
            byte dat = input[thisSequence * 8 + ind + 1];
            if ((bit8 & (1 << ind)) == (1 << ind)) {
                dat |= 0x80;
            }
            seq.push_back(dat);
        }
        for (auto by : seq) {
            data8bit.push_back(by);
        }
    }
    return data8bit;
}

void SparkStreamReader::readAmpName() {
    string ampName = readPrefixedString();

    // Build string representations
    sb.startStr();
    sb.addStr("Amp Name", ampName);
    sb.endStr();

    // Set values
    statusObject.lastMessageType() = MSG_TYPE_AMP_NAME;
    statusObject.ampName() = ampName;
}
