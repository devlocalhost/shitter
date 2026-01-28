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
bool wasConnected = false;

// used for non blocking webTakePhotoEveryX
bool repeatActive = false;
int repeatInterval = 0;
unsigned long lastShoot = 0;

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

// take a photo with a delay (timer)
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

// takes a photo every x seconds
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
        webServer.send(200, "text/plain", "Routes:\n /\t\t\t: this\n /shoot\t\t\t: take a single photo\n /takePhotoWithDelay\t: take a photo after x seconds\n /takePhotoEveryX\t: take a photo every x seconds\n /cancel\t\t: stop takePhotoEveryX\n\nPhoto can also be taken by pressing\nthe BOOT button on board.\n");
    });

    webServer.on("/shoot", []() {
        sendVolumeKey();
        webServer.send(200, "text/plain", "sendVolumeKey | OK\n");
    });

    webServer.on("/takePhotoWithDelay", []() {
        takePhotoWithDelay(webServer.arg("delay").toInt());
        webServer.send(200, "text/plain", "takePhotoWithDelay | OK\n");
    });

    webServer.on("/takePhotoEveryX", []() {
        repeatInterval = webServer.arg("repeatEvery").toInt();
        repeatActive = true;
        lastShoot = millis();
            
        webServer.send(200, "text/plain", "takePhotoEveryX | START\n");

        // code above looks confusing as shit right?
        // well, the magic is happening in the loop
        // function at the bottom
    });

    // this is use(ful)d only for takePhotoEveryX
    webServer.on("/cancel", []() {
        repeatActive = false;
        webServer.send(200, "text/plain", "takePhotoEveryX | STOP\n");
    });

    // webServer.on("/", []() {
    // });
    
    webServer.begin();
}

void loop() {
    checkConnectionState();
    webServer.handleClient();

    // very important part for takePhotoEveryX and the route
    if (repeatActive && millis() - lastShoot >= (unsigned long)repeatInterval * 1000) {
        sendVolumeKey();
        lastShoot = millis();
    }

    if (digitalRead(0) == LOW) {
        sendVolumeKey(); // send vol key if boot button is pressed
        
        while (digitalRead(0) == LOW); // debounce thing
        delay(50); 
    }
}
