/*
Author: Joe Fox <joe@codenoise.com>
Copyright CodeNoise, Inc 2024 All rights reserved
See LICENSE file for applicable license.

*/
#define US_KEYBOARD 1

#include <Arduino.h>
#include "BLEDevice.h"
#include "BLEHIDDevice.h"
#include "HIDTypes.h"
#include "HIDKeyboardTypes.h"


// Change the below values if desired
#define DIT_PIN 32
#define DAH_PIN 33
#define DEVICE_NAME "MorseKey"

uint8_t prevKeyMask = 0;
uint8_t keyMask = 0;

uint8_t ditChar = keymap['.'].usage;
uint8_t dahChar = keymap['-'].usage;
uint8_t keysPressed[][2] = {
  {0,0},
  {ditChar,0},
  {dahChar,0},
  {ditChar,dahChar}
};

BLEHIDDevice* hid;
BLECharacteristic* input;
BLECharacteristic* output;

// Message (report) sent when a key is pressed or released
struct InputReport {
    uint8_t modifiers;	     // bitmask: CTRL = 1, SHIFT = 2, ALT = 4
    uint8_t reserved;        // must be 0
    uint8_t pressedKeys[6];  // up to six concurrently pressed keys
};

// Message (report) received when an LED's state changed
struct OutputReport {
    uint8_t leds;            // bitmask: num lock = 1, caps lock = 2, scroll lock = 4, compose = 8, kana = 16
};

// Forward declarations
void bluetoothTask(void*);
void typeText(const char* text);


bool isBleConnected = false;


void setup() {
    Serial.begin(115200);

    // configure pin for button
    pinMode(DIT_PIN, INPUT_PULLUP);
    pinMode(DAH_PIN, INPUT_PULLUP);

    // start Bluetooth task
    xTaskCreate(bluetoothTask, "bluetooth", 20000, NULL, 5, NULL);
}


void loop() {
    if (isBleConnected) {
      keyMask = digitalRead(DIT_PIN) == LOW ? keyMask | 1  : keyMask & ~1;
      keyMask = digitalRead(DAH_PIN) == LOW ? keyMask | 2  : keyMask & ~2;

      if (keyMask != prevKeyMask) {
        Serial.printf("Mask changed: %d -> %d\n", prevKeyMask, keyMask);

        InputReport report = {
            .modifiers = 0,
            .reserved = 0,
            .pressedKeys = {
                keysPressed[keyMask][0],
                keysPressed[keyMask][1],
                0,
                0,
                0,
                0
            }
        };

        // send the input report
        input->setValue((uint8_t*)&report, sizeof(report));
        input->notify();
        prevKeyMask = keyMask;
      }
    }

    delay(5);
}

/*
 * Callbacks related to BLE connection
 */
class BleKeyboardCallbacks : public BLEServerCallbacks {

    void onConnect(BLEServer* server) {
        isBleConnected = true;

        // Allow notifications for characteristics
        BLE2902* cccDesc = (BLE2902*)input->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
        cccDesc->setNotifications(true);

        Serial.println("Client has connected");
    }

    void onDisconnect(BLEServer* server) {
        isBleConnected = false;

        // Disallow notifications for characteristics
        BLE2902* cccDesc = (BLE2902*)input->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
        cccDesc->setNotifications(false);

        Serial.println("Client has disconnected");
    }
};


/*
 * Called when the client (computer, smart phone) wants to turn on or off
 * the LEDs in the keyboard.
 *
 * bit 0 - NUM LOCK
 * bit 1 - CAPS LOCK
 * bit 2 - SCROLL LOCK
 */
 class OutputCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) {
        OutputReport* report = (OutputReport*) characteristic->getData();
        Serial.print("LED state: ");
        Serial.print((int) report->leds);
        Serial.println();
    }
};


void bluetoothTask(void*) {

    // initialize the device
    BLEDevice::init(DEVICE_NAME);
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new BleKeyboardCallbacks());

    // create an HID device
    hid = new BLEHIDDevice(server);
    input = hid->inputReport(1); // report ID
    output = hid->outputReport(1); // report ID
    output->setCallbacks(new OutputCallbacks());

    // set manufacturer name
    hid->manufacturer()->setValue("FoxFire Hobbies");
    // set USB vendor and product ID
    hid->pnp(0x02, 0xe502, 0xa111, 0x0210);
    // information about HID device: device is not localized, device can be connected
    hid->hidInfo(0x00, 0x02);

    // Security: device requires bonding
    BLESecurity* security = new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_BOND);

    // set report map
    hid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
    hid->startServices();

    // set battery level to 100%
    hid->setBatteryLevel(100);

    // advertise the services
    BLEAdvertising* advertising = server->getAdvertising();
    advertising->setAppearance(HID_KEYBOARD);
    advertising->addServiceUUID(hid->hidService()->getUUID());
    advertising->addServiceUUID(hid->deviceInfo()->getUUID());
    advertising->addServiceUUID(hid->batteryService()->getUUID());
    advertising->start();

    Serial.println("BLE ready");
    delay(portMAX_DELAY);
};
