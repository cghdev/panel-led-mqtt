#include <FS.h>                                   // File System
#include "SPIFFS.h"

#include <Arduino.h>                              // Arduino
#include <WiFiManager.h>                          // Configuración WiFi
#include <PubSubClient.h>                         // Cliente MQTT 0.8.2
#include <ArduinoJson.h>                          // Arduino JSON parser 6.15.1
#include <time.h>

#include <constants.h>                            // Cabecera de constantes
#include <utils.h>                                // Cabecera de utilidades
#include <mqtt.h>                                 // Cabecera MQTT

#include <MatrixHardware_ESP32_V0.h>                // This file contains multiple ESP32 hardware configurations, edit the file to define GPIOPINOUT (or add #define GPIOPINOUT with a hardcoded number before this #include)
#include <SmartMatrix.h>

#include <GifDecoder.h>


#define DISPLAY_TIME_SECONDS 10
#define NUMBER_FULL_CYCLES   100

#define USE_SMARTMATRIX         1
#define ENABLE_SCROLLING        1



#if (USE_SMARTMATRIX == 1)
/* SmartMatrix configuration and memory allocation */
#define COLOR_DEPTH 24                  // Choose the color depth used for storing pixels in the layers: 24 or 48 (24 is good for most sketches - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24)
const uint16_t kMatrixWidth = 64;       // Set to the width of your display, must be a multiple of 8
const uint16_t kMatrixHeight = 32;      // Set to the height of your display
const uint8_t kRefreshDepth = 36;       // Tradeoff of color quality vs refresh rate, max brightness, and RAM usage.  36 is typically good, drop down to 24 if you need to.  On Teensy, multiples of 3, up to 48: 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.  On ESP32: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save RAM, more to keep from dropping frames and automatically lowering refresh rate.  (This isn't used on ESP32, leave as default)
const uint8_t kPanelType = SM_PANELTYPE_HUB75_32ROW_MOD16SCAN;  // Choose the configuration that matches your panels.  See more details in MatrixCommonHUB75.h and the docs: https://github.com/pixelmatix/SmartMatrix/wiki
const uint32_t kMatrixOptions = (SM_HUB75_OPTIONS_NONE);        // see docs for options: https://github.com/pixelmatix/SmartMatrix/wiki
const uint8_t kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);
const uint8_t kScrollingLayerOptions = (SM_SCROLLING_OPTIONS_NONE);

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);
#if (ENABLE_SCROLLING == 1)
SMARTMATRIX_ALLOCATE_SCROLLING_LAYER(scrollingLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kScrollingLayerOptions);
#endif
#endif

/* template parameters are maxGifWidth, maxGifHeight, lzwMaxBits
 * 
 * lzwMaxBits is included for backwards compatibility reasons, but isn't used anymore
 */
GifDecoder<kMatrixWidth, kMatrixHeight, 12> decoder;
// range 0-255
int defaultBrightness = (10*255) / 100;

const rgb24 COLOR_BLACK = {
    0, 0, 0 };
void screenClearCallback(void) {
#if (USE_SMARTMATRIX == 1)
    backgroundLayer.fillScreen({0,0,0});
#endif
}

void updateScreenCallback(void) {
#if (USE_SMARTMATRIX == 1)
    backgroundLayer.swapBuffers();
#endif
}

void drawPixelCallback(int16_t x, int16_t y, uint8_t red, uint8_t green, uint8_t blue) {
#if (USE_SMARTMATRIX == 1)
    backgroundLayer.drawPixel(x, y, {red, green, blue});
#endif
}
#define configTIMER_TASK_STACK_DEPTH 4096

int defaultScrollOffset = 6;

// Variables de configuración para el AP WiFi
char host[] = "PX";
char separator[] = "_";
char SSID[18];
byte mac[6];
char macFull[6];

void configModeCallback (WiFiManager *myWiFiManager) {
    
    scrollingLayer.stop();
    scrollingLayer.setColor(WHITE);
    scrollingLayer.setMode(wrapForward);
    scrollingLayer.setSpeed(80);
    scrollingLayer.setFont(font6x10);
    scrollingLayer.start("Unable to connect. Access Point:", -1);

    // Show SSID
    backgroundLayer.setFont(font5x7);
    backgroundLayer.drawString(0, 21, LIGHT_BLUE, SSID);
    backgroundLayer.swapBuffers();
}

StaticJsonDocument<3500> document;

char mqtt_server[40];
char mqtt_port[8];
char mqtt_user[40];
char mqtt_password[40];

bool saveConfig = false;

void saveConfigCallback () {
    saveConfig = true;
    Serial.println("We need to save the config");
}

void setup() {
  decoder.setScreenClearCallback(screenClearCallback);
  decoder.setUpdateScreenCallback(updateScreenCallback);
  decoder.setDrawPixelCallback(drawPixelCallback);

  Serial.begin(115200);
  delay(1000);

  Serial.println("Mounting FS...");

    if (SPIFFS.begin(true)) {
        if (SPIFFS.exists("/config.json")) {
        Serial.println("FS mounted");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
            size_t size = configFile.size();
            std::unique_ptr<char[]> buf(new char[size]);

            configFile.readBytes(buf.get(), size);
            DynamicJsonDocument doc(256);
            DeserializationError error = deserializeJson(doc, buf.get());
            if (error) {
                Serial.printf("There was an error reading the config.json file\n");
                return;
            }
            JsonObject json = doc.as<JsonObject>();

            serializeJson(json, Serial);
            if (!json.isNull()) {

            strcpy(mqtt_server, json["mqtt_server"]);
            strcpy(mqtt_port, json["mqtt_port"]);
            strcpy(mqtt_user, json["mqtt_user"]);
            strcpy(mqtt_password, json["mqtt_password"]);

            } else {
            Serial.println("Something happened");
            }
        }
        }
    } else {
        Serial.println("Couldn't mount the FS");
    } 

    WiFiManagerParameter custom_mqtt_server("server", "MQTT server", mqtt_server, 40);
    WiFiManagerParameter custom_mqtt_port("port", "MQTT server", mqtt_server, 8);
    WiFiManagerParameter custom_mqtt_user("user", "MQTT user", mqtt_user, 40);
    WiFiManagerParameter custom_mqtt_pass("pass", "MQTT password", mqtt_password, 40);

    WiFiManager wifiManager;
    WiFi.macAddress(mac);
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    char baseMacChr[7] = {0};
    sprintf(macFull, "%02X%02X%02X", baseMac[3], baseMac[4], baseMac[5]);
    // Serial.println(baseMacChr);

    strcat(SSID, host);
    strcat(SSID, separator);
    strcat(SSID, macFull);

    matrix.addLayer(&backgroundLayer); 
    matrix.addLayer(&scrollingLayer); 
    // matrix.addLayer(&indexedLayer);
    matrix.begin(28000);
    matrix.setBrightness(defaultBrightness);
    scrollingLayer.setOffsetFromTop(defaultScrollOffset);
    backgroundLayer.enableColorCorrection(true);


    scrollingLayer.setColor(WHITE);
    scrollingLayer.setMode(wrapForward);
    scrollingLayer.setSpeed(60);
    scrollingLayer.setFont(font6x10);
    scrollingLayer.start("Connecting to WiFi...", -1);

    Serial.println("Connecting to WiFi...");
    WiFi.setHostname(SSID);
    WiFi.setAutoReconnect(true);

    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setTimeout(180);
    wifiManager.setCleanConnect(true);
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_mqtt_user);
    wifiManager.addParameter(&custom_mqtt_pass);

    if (!wifiManager.autoConnect(SSID, "password")) {
        delay(3000);
    }
    scrollingLayer.stop();

    client.setServer(mqtt_server, atoi(mqtt_port));
    client.connect(MQTT_CLIENT, mqtt_user, mqtt_password);
    client.setBufferSize(30000);
    client.setCallback(mqttCallback);

    if (client.connected()) {
        Serial.println("Connected to MQTT (main loop)");
        client.publish(STATUS_TOPIC, (const char *)"up");
        client.subscribe(STATUS_TOPIC);
        client.subscribe(APPLET_TOPIC);
        client.subscribe(BRIGHTNESS_TOPIC);

    }

    if (saveConfig) {
        strcpy(mqtt_server, custom_mqtt_server.getValue());
        strcpy(mqtt_port, custom_mqtt_port.getValue());
        strcpy(mqtt_user, custom_mqtt_user.getValue());
        strcpy(mqtt_password, custom_mqtt_pass.getValue());

        DynamicJsonDocument doc(256);
        JsonObject json = doc.to<JsonObject>();

        json["mqtt_server"] = mqtt_server;
        json["mqtt_port"]   = mqtt_port;
        json["mqtt_user"]   = mqtt_user;
        json["mqtt_password"]   = mqtt_password;

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile) {
        Serial.println("There was an error opening the file to write");
        }

        serializeJson(json, configFile);
        configFile.close();
        saveConfig = false;
    }

}

void loop() {
    // Try to reconnect to wifi if connection lost
    while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0,0,0,0)) {
        WiFi.reconnect();
        Serial.println(F("Trying to reconnect to WiFi"));
        delay(10000);
    }

    client.loop();

    if (!client.connected()) {
        Serial.println("Not connected to MQTT. Trying to reconnect.");
        mqttReconnect(mqtt_user, mqtt_password);
    }

    if (brightness > 0) {
        matrix.setBrightness((brightness*255) / 100);
        brightness = -1;
    }
    if (currentMode == WELCOME) {
        Serial.println("Welcome!");
        scrollingLayer.setColor(WHITE);
        scrollingLayer.setMode(wrapForward);
        scrollingLayer.setSpeed(60);
        scrollingLayer.setFont(font6x10);
        scrollingLayer.start("Welcome! Waiting for applets...", -1);
        currentMode = NONE;
    }
    if (currentMode == APPLET) {
        // Serial.println("Mode changed to applet");
        scrollingLayer.stop();
        static uint32_t lastFrameDisplayTime = 0;
        static unsigned int currentFrameDelay = 0;

        unsigned long now = millis();

        if((millis() - lastFrameDisplayTime) > currentFrameDelay) {
            if (newapplet) { // MOVE TO mqtt.cpp ??
                decoder.startDecoding((uint8_t *)appletdecoded, outputLength);
                newapplet = false;
            }
            decoder.decodeFrame(false);

            lastFrameDisplayTime = now;
            currentFrameDelay = decoder.getFrameDelay_ms();
        }
    }
    // Serial.println("End of loop");
    // delay(1000);
}
