/*
 * SparkBLEKeyboard.cpp
 *
 *  Created on: 27.12.2021
 *      Author: steffen
 */

#include "SparkBLEKeyboard.h"

SparkBLEKeyboard::SparkBLEKeyboard() {
}

SparkBLEKeyboard::~SparkBLEKeyboard() {
}

void SparkBLEKeyboard::end() {
	BLEServer *pServer = BLEDevice::getServer();
	if (pServer == nullptr) {
		Serial.println("Keyboard server is not initialized; skipping stop.");
		return;
	}
	for (int i = 0; i < pServer->getConnectedCount(); i++) {
		pServer->disconnect(pServer->getPeerInfo(i).getConnHandle());
	}
	Serial.println("Stopping advertising keyboard");
	pServer->stopAdvertising();
}

void SparkBLEKeyboard::start() {
	BLEServer *pServer = BLEDevice::getServer();
	if (pServer == nullptr) {
		Serial.println("Keyboard server is not initialized; skipping start.");
		return;
	}
	Serial.println("Starting advertising keyboard");
	pServer->startAdvertising();

}
