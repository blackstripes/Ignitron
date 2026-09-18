#include "SparkSerialCLI.h"

#include "Config_Definitions.h"
#include "SparkDataControl.h"
#include "SparkPresetControl.h"
#include "SparkStatus.h"

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)
#include "PanelLanLVGLUI.h"
extern PanelLanLVGLUI panelLanDisplay;
#endif

SparkSerialCLI::SparkSerialCLI(SparkDataControl *dataControl)
    : sparkDC_(dataControl) {
}

void SparkSerialCLI::begin() {
    inputBuffer_.reserve(128);
    Serial.println();
    Serial.println("Ignitron headless serial CLI ready.");
    Serial.println("Type 'help' for commands.");
    Serial.print("> ");
}

void SparkSerialCLI::update() {
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            String command = inputBuffer_;
            inputBuffer_ = "";
            command.trim();
            if (command.length() > 0) {
                execute(command);
            }
            Serial.print("> ");
            continue;
        }

        if ((c == '\b' || c == 0x7F) && inputBuffer_.length() > 0) {
            inputBuffer_.remove(inputBuffer_.length() - 1);
            continue;
        }

        if (inputBuffer_.length() < 127) {
            inputBuffer_ += c;
        }
    }
}

void SparkSerialCLI::execute(String command) {
    command.trim();
    command.toLowerCase();

    int firstSpace = command.indexOf(' ');
    String verb = firstSpace < 0 ? command : command.substring(0, firstSpace);
    String args = firstSpace < 0 ? "" : command.substring(firstSpace + 1);
    args.trim();

    if (verb == "help" || verb == "?") {
        printHelp();
    } else if (verb == "status") {
        printStatus();
    } else if (verb == "preset") {
        handlePreset(args);
    } else if (verb == "bank") {
        handleBank(args);
    } else if (verb == "fx") {
        handleEffect(args);
    } else if (verb == "tuner") {
        handleTuner(args);
    } else if (verb == "loop" || verb == "looper") {
        handleLooper(args);
    } else if (verb == "tap") {
        sparkDC_->tapTempoButton();
        Serial.println("Tap registered.");
    } else if (verb == "amp") {
        if (!SparkDataControl::isAmpConnected()) {
            Serial.println("Spark amp is not connected.");
        } else {
            sparkDC_->getAmpName();
            sparkDC_->getSerialNumber();
            Serial.println("Requested amp identity. Run 'status' after the response arrives.");
        }
    } else if (verb == "refresh") {
        if (!SparkDataControl::isAmpConnected()) {
            Serial.println("Spark amp is not connected.");
        } else {
            sparkDC_->getCurrentPresetFromSpark();
            Serial.println("Requested current preset.");
        }
    } else if (verb == "screenshot") {
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)
        panelLanDisplay.writeScreenshot(Serial);
#else
        Serial.println("Screenshots are available only in the PanelLan LVGL target.");
#endif
    } else if (verb == "touch") {
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)
        int split = args.indexOf(' ');
        if (split < 0) {
            Serial.println("Usage: touch <x> <y>");
        } else {
            int x = args.substring(0, split).toInt();
            int y = args.substring(split + 1).toInt();
            if (x < 0 || x >= 320 || y < 0 || y >= 240) {
                Serial.println("Touch coordinates must be within 320x240.");
            } else {
                panelLanDisplay.injectTouch(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
                Serial.printf("Injected touch: %d, %d\n", x, y);
            }
        }
#else
        Serial.println("Touch injection is available only in the PanelLan LVGL target.");
#endif
    } else {
        Serial.println("Unknown command. Type 'help'.");
    }
}

void SparkSerialCLI::printHelp() {
    Serial.println("Commands:");
    Serial.println("  status");
    Serial.println("  amp                     Request amp identity");
    Serial.println("  refresh                 Request current preset");
    Serial.println("  screenshot              Stream a PPM screenshot over USB serial");
    Serial.println("  touch <x> <y>           Inject one LVGL touch press/release (development)");
    Serial.println("  preset <1-4>");
    Serial.println("  bank up|down");
    Serial.println("  fx <gate|comp|drive|mod|delay|reverb> <toggle|on|off>");
    Serial.println("  tuner on|off");
    Serial.println("  tuner probe on|off      Native tuner diagnostic; observe serial events/audio");
    Serial.println("  tap");
    Serial.println("  loop rec|dub|recdub|play|stop|playstop|undo|redo|undoredo|clear");
    Serial.println("  loop status|config");
    Serial.println("  help");
}

void SparkSerialCLI::printStatus() {
    SparkPresetControl &presetControl = SparkPresetControl::getInstance();
    SparkStatus &status = SparkStatus::getInstance();
    const Preset &preset = presetControl.activePreset();

    Serial.println("--- Ignitron status ---");
    Serial.printf("Spark connected: %s\n", SparkDataControl::isAmpConnected() ? "yes" : "no");

    const string ampName = status.ampName();
    const string ampSerial = status.ampSerialNumber();
    Serial.printf("Amp: %s\n", ampName.empty() ? "(unknown)" : ampName.c_str());
    Serial.printf("Serial: %s\n", ampSerial.empty() ? "(unknown)" : ampSerial.c_str());

    Serial.printf("Mode: %d  Submode: %d\n", sparkDC_->operationMode(), sparkDC_->subMode());
    Serial.printf("Bank: %d  Preset: %d\n", presetControl.activeBank(), presetControl.activePresetNum());
    Serial.printf("Preset name: %s\n", preset.name.empty() ? "(unknown)" : preset.name.c_str());
    Serial.printf("Looper loops: %d\n", status.numberOfLoops());

    if (sparkDC_->subMode() == SUB_MODE_TUNER) {
        Serial.printf("Tuner: %s  %+d cents\n", status.noteString().c_str(), status.noteOffsetCents());
    }
}

void SparkSerialCLI::handlePreset(const String &args) {
    int presetNumber = args.toInt();
    if (presetNumber < 1 || presetNumber > 4) {
        Serial.println("Usage: preset <1-4>");
        return;
    }

    if (!SparkDataControl::isAmpConnected()) {
        Serial.println("Spark amp is not connected.");
        return;
    }

    if (SparkPresetControl::getInstance().processPresetSelect(presetNumber)) {
        Serial.printf("Preset %d selected.\n", presetNumber);
    } else {
        Serial.printf("Preset %d selection was not completed.\n", presetNumber);
    }
}

void SparkSerialCLI::handleBank(const String &args) {
    SparkPresetControl &presetControl = SparkPresetControl::getInstance();
    if (args == "up") {
        presetControl.increaseBank();
        Serial.printf("Pending bank: %d\n", presetControl.pendingBank());
    } else if (args == "down") {
        presetControl.decreaseBank();
        Serial.printf("Pending bank: %d\n", presetControl.pendingBank());
    } else {
        Serial.println("Usage: bank up|down");
    }
}

int SparkSerialCLI::effectIndex(const String &name) const {
    if (name == "gate" || name == "noisegate" || name == "noise")
        return INDEX_FX_NOISEGATE;
    if (name == "comp" || name == "compressor" || name == "wah")
        return INDEX_FX_COMP;
    if (name == "drive" || name == "od")
        return INDEX_FX_DRIVE;
    if (name == "mod" || name == "modulation")
        return INDEX_FX_MOD;
    if (name == "delay")
        return INDEX_FX_DELAY;
    if (name == "reverb" || name == "verb")
        return INDEX_FX_REVERB;
    return INDEX_FX_INVALID;
}

void SparkSerialCLI::handleEffect(const String &args) {
    int split = args.indexOf(' ');
    if (split < 0) {
        Serial.println("Usage: fx <gate|comp|drive|mod|delay|reverb> <toggle|on|off>");
        return;
    }

    String effectName = args.substring(0, split);
    String action = args.substring(split + 1);
    effectName.trim();
    action.trim();

    int index = effectIndex(effectName);
    if (index == INDEX_FX_INVALID) {
        Serial.println("Unknown effect slot.");
        return;
    }

    if (!SparkDataControl::isAmpConnected()) {
        Serial.println("Spark amp is not connected.");
        return;
    }

    const Preset &preset = SparkPresetControl::getInstance().activePreset();
    if (preset.isEmpty || preset.pedals.size() <= static_cast<size_t>(index)) {
        Serial.println("Current preset data is not available yet. Try 'refresh'.");
        return;
    }

    bool ok = false;
    if (action == "toggle") {
        ok = SparkDataControl::toggleEffect(index);
    } else if (action == "on" || action == "off") {
        const string &actualEffectName = preset.pedals[index].name;
        if (actualEffectName.empty()) {
            Serial.println("Effect name is unavailable for this preset.");
            return;
        }
        ok = SparkDataControl::switchEffectOnOff(actualEffectName, action == "on");
    } else {
        Serial.println("Action must be toggle, on, or off.");
        return;
    }

    Serial.println(ok ? "Effect command sent." : "Effect command failed.");
}

void SparkSerialCLI::handleTuner(const String &args) {
    const int separator = args.indexOf(' ');
    const String tunerVerb = separator < 0 ? args : args.substring(0, separator);
    if (tunerVerb == "probe") {
        String probeArgs = separator < 0 ? "" : args.substring(separator + 1);
        probeArgs.trim();
        handleTunerProbe(probeArgs);
        return;
    }

    if (!SparkDataControl::isAmpConnected()) {
        Serial.println("Spark amp is not connected.");
        return;
    }

    if (args == "on") {
        sparkDC_->switchSubMode(SUB_MODE_TUNER);
        Serial.println("Tuner enabled.");
    } else if (args == "off") {
        sparkDC_->switchSubMode(SUB_MODE_PRESET);
        Serial.println("Tuner disabled.");
    } else {
        Serial.println("Usage: tuner on|off");
    }
}

void SparkSerialCLI::handleTunerProbe(const String &args) {
    if (args != "on" && args != "off") {
        Serial.println("Usage: tuner probe on|off");
        return;
    }

    if (!SparkDataControl::isAmpConnected()) {
        Serial.println("Spark amp is not connected.");
        return;
    }

    const bool enable = args == "on";
    const bool sent = SparkDataControl::switchTuner(enable);
    if (sent) {
        Serial.printf("Native tuner diagnostic command sent (%s). Observe incoming tuner events and amp audio; Ignitron submode was not changed.\n",
                      enable ? "on" : "off");
    } else {
        Serial.println("Native tuner diagnostic command failed to send.");
    }
}

void SparkSerialCLI::handleLooper(const String &args) {
    if (!SparkDataControl::isAmpConnected()) {
        Serial.println("Spark amp is not connected.");
        return;
    }

    bool ok = false;
    if (args == "rec")
        ok = sparkDC_->sparkLooperRec();
    else if (args == "dub")
        ok = sparkDC_->sparkLooperDub();
    else if (args == "recdub")
        ok = sparkDC_->sparkLooperRecDub();
    else if (args == "play")
        ok = sparkDC_->sparkLooperPlay();
    else if (args == "stop")
        ok = sparkDC_->sparkLooperStopPlaying();
    else if (args == "playstop")
        ok = sparkDC_->sparkLooperPlayStop();
    else if (args == "undo")
        ok = sparkDC_->sparkLooperUndo();
    else if (args == "redo")
        ok = sparkDC_->sparkLooperRedo();
    else if (args == "undoredo")
        ok = sparkDC_->sparkLooperUndoRedo();
    else if (args == "clear" || args == "delete")
        ok = sparkDC_->sparkLooperDeleteAll();
    else if (args == "status")
        ok = sparkDC_->sparkLooperGetStatus();
    else if (args == "config")
        ok = sparkDC_->sparkLooperGetConfig();
    else {
        Serial.println("Usage: loop rec|dub|recdub|play|stop|playstop|undo|redo|undoredo|clear|status|config");
        return;
    }

    Serial.println(ok ? "Looper command sent." : "Looper command failed.");
}
