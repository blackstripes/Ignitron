#ifndef SPARKSERIALCLI_H_
#define SPARKSERIALCLI_H_

#include <Arduino.h>

class SparkDataControl;

class SparkSerialCLI {
public:
    explicit SparkSerialCLI(SparkDataControl *dataControl);

    void begin();
    void update();

private:
    SparkDataControl *sparkDC_;
    String inputBuffer_;

    void execute(String command);
    void printHelp();
    void printStatus();
    void handlePreset(const String &args);
    void handleBank(const String &args);
    void handleEffect(const String &args);
    void handleTuner(const String &args);
    void handleLooper(const String &args);
    int effectIndex(const String &name) const;
};

#endif /* SPARKSERIALCLI_H_ */
