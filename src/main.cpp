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

static const uint8_t VOLUME_UP_KEY = 0xE9;
static const uint8_t VOLUME_DOWN_KEY = 0xEA;

int shootingInterval = 0;
unsigned long lastShotTimestamp = 0;
bool shootingPhoto = false;

bool wasConnected = false;
char message[200];

// sets the rgb led
void setLED(uint8_t r, uint8_t g, uint8_t b) {
    strip.setPixelColor(0, strip.Color(r, g, b));
    strip.show();
}

// pulses (turns on for x ms then off) the rgb led
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

// send the key
void sendVolumeKey() {
    if (!bleServer->getConnectedCount()) {
        pulseLED(255, 0, 0, 100);
        return;
    }

    uint16_t msg = VOLUME_DOWN_KEY;
    input->setValue((uint8_t*)&msg, 2);
    input->notify();

    delay(50);

    msg = 0;
    input->setValue((uint8_t*)&msg, 2);
    input->notify();

    // blink the rgb led, feedback basically
    // pulseLED(255, 255, 255, 400);
    // delay(300);
    pulseLED(255, 255, 255, 50);
}

void setup() {
    pinMode(0, INPUT_PULLUP); // this is the boot button, gpio0
    
    strip.begin();
    strip.setBrightness(60);
    strip.show();

    NimBLEDevice::init("shitter"); // name
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

    // more feedback
    pulseLED(255, 0, 0, 100);
    delay(125);
    pulseLED(0, 255, 0, 100);
    delay(125);
    pulseLED(0, 0, 255, 100);

    // set up hotspot
    WiFi.softAP("shitteremote");

    webServer.on("/", []() {
        webServer.send(200, "text/plain", "                take a shot: /shoot\ntake a shot after x seconds: /shoot/after\ntake a shot every x seconds: /shoot/every\n     stop above task if ran: /shoot\n");
    });

    webServer.on("/shoot", []() {
        sendVolumeKey();
        webServer.send(200, "text/plain", "[200] /shoot:\n  ok\n");
    });

    webServer.on("/shoot/after", []() {
        int interval = webServer.arg("interval").toInt();

        if (interval < 1) {
            snprintf(message, sizeof(message), "[400] /shoot/after:\n  invalid interval value: '%d'\n", interval);
            webServer.send(400, "text/plain", message);
            return;
        }
        
        snprintf(message, sizeof(message), "[200] /shoot/after:\n  taking shot in %d seconds\n", interval);
        webServer.send(200, "text/plain", message);
    
        delay(interval * 1000);
        sendVolumeKey();
    });

    webServer.on("/shoot/every", []() {
        int interval = webServer.arg("interval").toInt();

        if (interval < 1) {
            snprintf(message, sizeof(message), "[400] /shoot/every:\n  invalid interval value: '%d'\n", interval);
            webServer.send(400, "text/plain", message);
            return;
        }
        
        snprintf(message, sizeof(message), "[200] /shoot/every:\n  taking shots every %d seconds\n  call /shoot/repeat/cancel\n  route to stop\n", interval);
        webServer.send(200, "text/plain", message);

        shootingPhoto = true;
        shootingInterval = interval;
        lastShotTimestamp = millis();
    });

    webServer.on("/shoot/every/cancel", []() {
        shootingPhoto = false;
        shootingInterval = 0;
        lastShotTimestamp = 0;
        
        webServer.send(200, "text/plain", "[200] /shoot/repeat/cancel:\n  stopping shots\n");
    });

    // webServer.on("/", []() {
    // });
    
    webServer.begin();
}

void loop() {
    checkConnectionState();
    webServer.handleClient();

    if (shootingPhoto) {
        unsigned long now = millis();

        if (now - lastShotTimestamp >= shootingInterval * 1000) {
            sendVolumeKey();
            lastShotTimestamp = now;
        }
    }

    if (digitalRead(0) == LOW) {
        sendVolumeKey(); // send vol key if boot button is pressed
        
        while (digitalRead(0) == LOW); // debounce thing
        delay(50); 
    }
}
