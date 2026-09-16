#define CONFIG_LITTLEFS_SPIFFS_COMPAT

#include <Arduino.h>
#include <NimBLEDevice.h> // github NimBLE
#include <SPI.h>
#include <Wire.h>
#include <string>

#include "src/SparkButtonHandler.h"
#include "src/SparkDataControl.h"
#include "src/SparkDisplayControl.h"
#include "src/SparkLEDControl.h"
#include "src/SparkPresetControl.h"
#ifdef HEADLESS_SERIAL_MODE
#include "src/SparkSerialCLI.h"
#endif

using namespace std;

// Device Info Definitions
const string DEVICE_NAME = "Ignitron";

// Control classes
SparkDataControl spark_dc;
SparkButtonHandler spark_bh;
SparkLEDControl spark_led;
SparkDisplayControl sparkDisplay;
SparkPresetControl &presetControl = SparkPresetControl::getInstance();
#ifdef HEADLESS_SERIAL_MODE
SparkSerialCLI serialCLI(&spark_dc);
#endif

unsigned long lastInitialPresetTimestamp = 0;
unsigned long currentTimestamp = 0;
int initialRequestInterval = 3000;

// Check for initial boot
bool isInitBoot;
OperationMode operationMode = SPARK_MODE_APP;

/////////////////////////////////////////////////////////
//
// INIT AND RUN
//
/////////////////////////////////////////////////////////

void setup() {

    Serial.begin(115200);
    while (!Serial)
        ;

    Serial.println("Initializing");
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount failed");
        return;
    }

#ifdef HEADLESS_SERIAL_MODE
    // Headless builds are always direct controllers (APP mode). Do not depend on
    // button state during boot because no buttons are required for the prototype.
    operationMode = SPARK_MODE_APP;
#else
    spark_bh.setDataControl(&spark_dc);
    operationMode = spark_bh.checkBootOperationMode();
#endif

    // Setting operation mode before initializing
    operationMode = spark_dc.init(operationMode);
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
    serialCLI.begin();
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
}

void loop() {

    // Methods to call only in APP mode
    if (operationMode == SPARK_MODE_APP) {
#ifdef HEADLESS_SERIAL_MODE
        // Keep the serial console responsive while BLE scans/connects. The stock
        // firmware stays in a blocking loop here because its buttons/display are
        // its only user interface.
        if (!(spark_dc.checkBLEConnection())) {
            serialCLI.update();
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
        if (spark_dc.isInitBoot()) {
            spark_dc.getSerialNumber();
            spark_dc.isInitBoot() = false;
        }
    }

    // Check if presets have been updated (not needed in Keyboard mode)
    if (operationMode != SPARK_MODE_KEYBOARD) {
        spark_dc.checkForUpdates();
    }

#ifdef HEADLESS_SERIAL_MODE
    serialCLI.update();
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
