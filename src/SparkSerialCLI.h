#ifndef SPARKSERIALCLI_H_
#define SPARKSERIALCLI_H_

#include <Arduino.h>

class SparkDataControl;
class ControllerActions;

class SparkSerialCLI {
public:
    SparkSerialCLI(SparkDataControl *dataControl, ControllerActions *controllerActions = nullptr);

    void begin();
    void update();

private:
    SparkDataControl *sparkDC_;
    ControllerActions *controllerActions_;
    String inputBuffer_;

    void execute(String command);
    void printHelp();
    void printStatus();
    void printDiagnostics();
    void handlePreset(const String &args);
    void handleBank(const String &args);
    void handleEffect(const String &args);
    void handleTuner(const String &args);
    void handleTunerProbe(const String &args);
    void handleLooper(const String &args);
    int effectIndex(const String &name) const;
};

#endif /* SPARKSERIALCLI_H_ */
