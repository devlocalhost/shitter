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
bool repeatActive = false;
int repeatInterval = 0;
unsigned long lastShoot = 0;

void setLED(uint8_t r, uint8_t g, uint8_t b) {
    strip.setPixelColor(0, strip.Color(r, g, b));
    strip.show();
}

void pulseLED(uint8_t r, uint8_t g, uint8_t b, int ms = 50) {
    setLED(r, g, b);
    delay(ms);
    setLED(0, 0, 0);
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

void takePhotoWithDelay(int delaySec = 0) {
    if (delaySec < 1) {
        return; // invalid value
    }

    unsigned long start = millis();

    while (millis() - start < (unsigned long)delaySec * 1000) {
        checkConnectionState();
        delay(50);
    }

    sendVolumeKey();
}

void takePhotoEveryX(int shootDelay = 0) {
    if (shootDelay < 1) {
        return; // invalid value
    }
    
    unsigned long last = 0;
    while (true) {
        checkConnectionState();

        if (millis() - last >= (unsigned long)shootDelay * 1000) {
            sendVolumeKey();
            last = millis();
        }

        delay(50);
    }
}

void webSendVolumeKey() {
    sendVolumeKey();
    webServer.send(200, "text/plain", "webSendVolumeKey | OK");
}

void webTakePhotoWithDelay() {
    if (webServer.hasArg("delay")) {
        takePhotoWithDelay(webServer.arg("delay").toInt());
    }

    webServer.send(200, "text/plain", "webTakePhotoWithDelay | OK");
}

void setup() {
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

    webServer.on("/shoot", []() {
        if (webServer.hasArg("delay")) {
            takePhotoWithDelay(webServer.arg("delay").toInt()); // TODO: check if value is valid
            webServer.send(200, "text/plain", "webTakePhotoWithDelay | OK");
        } else if (webServer.hasArg("repeatEvery")) {
            repeatInterval = webServer.arg("repeatEvery").toInt();
            
            if (repeatInterval > 0) {
                repeatActive = true;
                lastShoot = millis();
                
                webServer.send(200, "text/plain", "webTakePhotoEveryX | START");
            } else {
                webServer.send(400, "text/plain", "webTakePhotoEveryX | Invalid value");
            }
        } else {
            sendVolumeKey();
            webServer.send(200, "text/plain", "webSendVolumeKey | OK");
        }
    });
    webServer.on("/cancel", []() {
        repeatActive = false;
        webServer.send(200, "text/plain", "cancel | STOP");
    });
    
    webServer.begin();
}

void loop() {
    checkConnectionState();
    webServer.handleClient();

    if (repeatActive && millis() - lastShoot >= (unsigned long)repeatInterval * 1000) {
        sendVolumeKey();
        lastShoot = millis();
    }
}
