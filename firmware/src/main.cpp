#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>

// OLED Setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// SPI and RFID Pins
#define RFID1_CS 5
#define RFID2_CS 4
#define SCK_PIN 14
#define MISO_PIN 12
#define MOSI_PIN 13

// Pin setup
#define POT_PIN      34   // Analog pin for potentiometer
#define BUZZER_PIN   26   // Passive buzzer
#define NEOPIXEL_PIN 27   // NeoPixel data pin
#define NUM_PIXELS    8   // 8 bit circular Neopixel
Adafruit_NeoPixel pixel(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// RFID Instances
MFRC522 rfid1(RFID1_CS, 0);
MFRC522 rfid2(RFID2_CS, 0);

// WiFi AP Credentials
const char *ssid = "ESP32_AP";
const char *password = "password123";

// MQTT Broker IP (Laptop server)
const char *mqttServer = "192.168.4.2";
const int mqttPort = 1883;
WiFiClient espClient;
PubSubClient client(espClient);

// Function Prototypes
void setupWiFiAP();
void connectToMQTT();
void mqtt_callback(char *topic, byte *payload, unsigned int length);
void taskRFID1(void *pvParameters);
void taskRFID2(void *pvParameters);
void getTagID(MFRC522 *rfid, char *tagID);
void showDisplay(const char* prefix, const char* message);
void handleAccess(void *pvParameters);
void indicateStatus(bool granted);
int getLocationFromPot();

// Struct for access response
struct AccessResponse {
  char uid[32];
  char name[64];
  bool allowed;
};


QueueHandle_t accessQueue;

void setup() {
  Serial.begin(115200);
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  pinMode(RFID1_CS, OUTPUT);
  pinMode(RFID2_CS, OUTPUT);
  digitalWrite(RFID1_CS, HIGH);
  digitalWrite(RFID2_CS, HIGH);
  rfid1.PCD_Init();
  rfid2.PCD_Init();

  pinMode(POT_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pixel.begin();
  pixel.clear();
  pixel.show();

  setupWiFiAP();
  client.setServer(mqttServer, mqttPort);
  connectToMQTT();

  accessQueue = xQueueCreate(5, sizeof(AccessResponse));

  xTaskCreatePinnedToCore(taskRFID1, "RFID_IN", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskRFID2, "RFID_OUT", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(handleAccess, "AccessHandler", 4096, NULL, 1, NULL, 1);

  display.clearDisplay();
  display.println("Initialized Successfully");
  display.display();
}

void loop() {
  client.loop();  // Keep MQTT connection alive
}

void setupWiFiAP() {
  Serial.println("Setting up ESP32 as Access Point...");
  WiFi.mode(WIFI_AP);
  // Configure static IP for ESP32 (gateway)
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  
  // Start the access point
  if (WiFi.softAP(ssid, password)) {
    Serial.println("Access Point started");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Failed to start Access Point");
    while (true);
  }
}

void connectToMQTT() {
  client.setCallback(mqtt_callback);
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect("ESP32Client")) {
      Serial.println("Connected to MQTT broker");
      client.subscribe("access/response");
    } else {
      Serial.print("Failed. rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 2 seconds...");
      delay(2000);
    }
  }
}

void mqtt_callback(char *topic, byte *payload, unsigned int length) {
  char message[100];
  memset(message, 0, sizeof(message));
  memcpy(message, payload, length);
  message[length] = '\0';

  if (strcmp(topic, "access/response") == 0) {
    char *uid = strtok(message, ",");
    char *status = strtok(NULL, ",");
    char *name = strtok(NULL, ",");

    if (uid && status && name) {
      AccessResponse res;
      strncpy(res.uid, uid, sizeof(res.uid));
      strncpy(res.name, name, sizeof(res.name));
      res.allowed = (strcmp(status, "ALLOWED") == 0);

      xQueueSend(accessQueue, &res, portMAX_DELAY);
    }
  }
}

void taskRFID1(void *pvParameters) {
  char tag[32];
  char payload[64];

  for (;;) {
    digitalWrite(RFID1_CS, LOW);
    if (rfid1.PICC_IsNewCardPresent() && rfid1.PICC_ReadCardSerial()) {
      getTagID(&rfid1, tag);
      if (strlen(tag) > 0) {
        int location = getLocationFromPot();
        snprintf(payload, sizeof(payload), "%s,%d", tag, location);
        client.publish("rfid/in", payload);
        
        char displayMsg[48];
        snprintf(displayMsg, sizeof(displayMsg), "%s @L%d", tag, location);
        showDisplay("IN", displayMsg);

        Serial.printf("IN Tag: %s Location: %d\n", tag, location);
      }
      rfid1.PICC_HaltA();
      delay(1500);
    }
    digitalWrite(RFID1_CS, HIGH);
    delay(200);
  }
}

void taskRFID2(void *pvParameters) {
  char tag[32];
  char payload[64];
  for (;;) {
    digitalWrite(RFID2_CS, LOW);
    if (rfid2.PICC_IsNewCardPresent() && rfid2.PICC_ReadCardSerial()) {
      getTagID(&rfid2, tag);
      if (strlen(tag) > 0) {
        int location = getLocationFromPot();
        snprintf(payload, sizeof(payload), "%s,%d", tag, location);
        client.publish("rfid/out", payload);
        
        char displayMsg[48];
        snprintf(displayMsg, sizeof(displayMsg), "%s @L%d", tag, location);
        showDisplay("OUT", displayMsg);

        Serial.printf("OUT Tag: %s Location: %d\n", tag, location);
      }
      rfid2.PICC_HaltA();
      delay(1500);
    }
    digitalWrite(RFID2_CS, HIGH);
    delay(200);
  }
}

void getTagID(MFRC522 *rfid, char *tagID) {
  tagID[0] = '\0';
  if (rfid->uid.size > 0) {
    for (byte i = 0; i < rfid->uid.size; i++) {
      char byteStr[3];
      snprintf(byteStr, sizeof(byteStr), "%02X", rfid->uid.uidByte[i]);
      strncat(tagID, byteStr, 2);
    }
  }
}

void showDisplay(const char* prefix, const char* message) {
  display.clearDisplay();
  display.setCursor(0, 20);
  display.print("UID ");
  display.print(prefix);
  display.print(": ");
  display.println(message);
  display.display();
}

int getLocationFromPot() {
  int analogVal = analogRead(POT_PIN);
  int location = map(analogVal, 0, 4095, 1, 5);
  // Clamp to valid range
  if (location < 1) location = 1;
  if (location > 5) location = 5;
  return location;
}

void handleAccess(void *pvParameters) {
  AccessResponse res;
  for (;;) {
    if (xQueueReceive(accessQueue, &res, portMAX_DELAY) == pdTRUE) {

      Serial.println("\nAutheticating ...");
      Serial.println("Access Response Received:");
      Serial.print("UID: ");
      Serial.println(res.uid);
      Serial.print("Name: ");
      Serial.println(res.name);
      Serial.print("Access: ");
      Serial.println(res.allowed ? "ALLOWED" : "DENIED");

      indicateStatus(res.allowed);

      char displayMsg[100];
      if (res.allowed) {
        snprintf(displayMsg, sizeof(displayMsg), "\nALLOWED\nWelcome %s", res.name);
        showDisplay("OK", displayMsg);
      } else {
        snprintf(displayMsg, sizeof(displayMsg), "\nALLOWED\nWelcome %s", res.name);
        showDisplay("X", displayMsg);
      }
    }
  }
}

void indicateStatus(bool granted) {
  uint32_t color = granted ? pixel.Color(0, 255, 0) : pixel.Color(255, 0, 0);
  for (int i = 0; i < pixel.numPixels(); i++) {
    pixel.setPixelColor(i, color);
  }
  pixel.show();
  tone(BUZZER_PIN, granted ? 1000 : 200, granted ? 200 : 1000);
  delay(500);
  pixel.clear();
  pixel.show();
}
