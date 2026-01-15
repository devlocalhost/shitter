#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN 38
#define NUM_LEDS 1

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

NimBLEServer* bleServer;
NimBLEHIDDevice* hid;
NimBLECharacteristic* input;

WebServer webServer(80);
IPAddress apIP;

static const uint8_t VOLUME_UP = 0xE9;
bool wasConnected = false;

void setLED(uint8_t r, uint8_t g, uint8_t b) {
    strip.setPixelColor(0, strip.Color(r, g, b));
    strip.show();
}

void pulseLED(uint8_t r, uint8_t g, uint8_t b, int ms = 50) {
    setLED(r, g, b);
    delay(ms);
    setLED(0, 0, 0);
}

void flushSerial() {
    while (Serial.available()) Serial.read();
}

void checkConnectionState() {
    bool isConnected = (bleServer->getConnectedCount() > 0);

    if (isConnected && !wasConnected) {
        for (int i = 0; i < 3; i++) {
            pulseLED(0, 0, 255, 100);
            delay(125);
        }
        wasConnected = true;
    }
    else if (!isConnected && wasConnected) {
        for (int i = 0; i < 3; i++) {
            pulseLED(255, 0, 0, 100);
            delay(125);
        }
        wasConnected = false;
        NimBLEDevice::getAdvertising()->start();
    }
}

void waitForSerialInput() {
    while (!Serial.available()) {
        checkConnectionState();
        delay(50);
    }
}

void sendVolumeKey() {
    if (!bleServer->getConnectedCount()) {
        pulseLED(255, 0, 0, 100);
        return;
    }

    uint16_t msg = VOLUME_UP;
    input->setValue((uint8_t*)&msg, 2);
    input->notify();

    delay(100);

    msg = 0;
    input->setValue((uint8_t*)&msg, 2);
    input->notify();

    pulseLED(255, 255, 255, 400);
    delay(300);
    pulseLED(255, 255, 255, 50);
}

void clearScreen() {
    Serial.write(27);
    Serial.print("[2J");
    Serial.write(27);
    Serial.print("[H");
}

void printMenu() {
    clearScreen();

    Serial.println("Commands:");
    Serial.println("       d: photo with delay");
    Serial.println("       x: photo every x seconds");
    Serial.println("   enter: take photo now");
}

void takePhotoWithDelay() {
    Serial.println("Enter delay in seconds:");
    waitForSerialInput();

    int delaySec = Serial.parseInt();
    flushSerial();

    if (delaySec < 1) {
        Serial.println("Invalid value");
        return;
    }

    Serial.printf("Taking photo in %d seconds...\n", delaySec);

    unsigned long start = millis();
    while (millis() - start < (unsigned long)delaySec * 1000) {
        checkConnectionState();
        delay(50);
    }

    sendVolumeKey();
}

void takePhotoEveryX() {
    Serial.println("Enter delay in seconds:");
    waitForSerialInput();

    int shootDelay = Serial.parseInt();
    flushSerial();

    if (shootDelay < 1) {
        Serial.println("Invalid value");
        return;
    }

    Serial.printf("Taking photos every %d seconds. Enter 'e' to stop\n", shootDelay);

    unsigned long last = 0;
    while (true) {
        checkConnectionState();

        if (Serial.available()) {
            char c = Serial.read();
            flushSerial();
            if (c == 'e' || c == 'E') break;
        }

        if (millis() - last >= (unsigned long)shootDelay * 1000) {
            sendVolumeKey();
            last = millis();
        }

        delay(50);
    }
}

void handleShoot() {
    sendVolumeKey();
    webServer.send(200, "text/plain", "OK");
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    strip.begin();
    strip.setBrightness(60);
    strip.show();

    NimBLEDevice::init("shitter");
    bleServer = NimBLEDevice::createServer();

    hid = new NimBLEHIDDevice(bleServer);
    input = hid->getInputReport(1);

    hid->setManufacturer("shittr");
    hid->setPnp(0x02, 0xe502, 0xa111, 0x0210);
    hid->setHidInfo(0x00, 0x01);

    const uint8_t reportMap[] = {
        0x05, 0x0C,
        0x09, 0x01,
        0xA1, 0x01,
        0x85, 0x01,
        0x19, 0x00,
        0x2A, 0xFF, 0x00,
        0x15, 0x00,
        0x26, 0xFF, 0x03,
        0x75, 0x10,
        0x95, 0x01,
        0x81, 0x00,
        0xC0
    };

    hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));
    hid->startServices();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);
    adv->addServiceUUID(hid->getHidService()->getUUID());
    adv->start();

    pulseLED(255, 0, 0, 100);
    delay(125);
    pulseLED(0, 255, 0, 100);
    delay(125);
    pulseLED(0, 0, 255, 100);

    WiFi.softAP("shitteremote");
    apIP = WiFi.softAPIP();

    webServer.on("/shoot", handleShoot);
    webServer.begin();

    printMenu();
}

void loop() {
    checkConnectionState();
    webServer.handleClient();

    if (!Serial.available()) {
        delay(10);
        return;
    }

    char cmd = Serial.read();
    flushSerial();

    switch (cmd) {
        case 'd':
            takePhotoWithDelay();
            break;

        case 'x':
            takePhotoEveryX();
            break;

        case '\n':
        case '\r':
            sendVolumeKey();
            break;

        default:
            Serial.println("Unknown command");
            break;
    }

    printMenu();
}
