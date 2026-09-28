/*
  VEHICULAR IoT TRACKER - WiFi + Firebase version
  ESP32 DevKitC V1 + NEO-6M GPS + Buzzer (tamper alarm)
  Location is pushed to Firebase Realtime Database over WiFi.
  (No SIM800L / GSM module needed for this prototype version.)

  WIRING:
  --------------------------------------------------
  GPS NEO-6M   : TX -> ESP32 GPIO16 (RX2)   |  RX -> ESP32 GPIO17 (TX2)
  GPS VCC      : ESP32 3.3V
  Buzzer       : GPIO25 -> 1k resistor -> BC547 base -> buzzer switched to battery
  All GND pins : common ground

  LIBRARIES NEEDED (Arduino IDE > Library Manager):
  - TinyGPSPlus (by Mikal Hart)
  - (WiFi.h and HTTPClient.h come built-in with the ESP32 board package)

  FIREBASE SETUP (do this before uploading):
  1. Go to https://console.firebase.google.com -> Create a project
  2. Build > Realtime Database -> Create Database -> start in TEST MODE
     (test mode = open read/write, fine for a prototype/demo, NOT for production)
  3. Copy your database URL, looks like: https://your-project-id-default-rtdb.firebaseio.com
  4. Paste it into FIREBASE_HOST below, keeping the "/location.json" ending
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>

// ---------- USER SETTINGS ----------
const char* WIFI_SSID     = "YOUR_WIFI_OR_HOTSPOT_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Replace with your actual Firebase Realtime Database URL + "/location.json"
const char* FIREBASE_HOST = "https://your-project-id-default-rtdb.firebaseio.com/location.json";

const unsigned long LOCATION_INTERVAL = 15000;   // push location every 15 sec (demo-friendly)
const float HOME_LAT = 12.9716;                  // set to your "safe parking" latitude
const float HOME_LON = 77.5946;                  // set to your "safe parking" longitude
const float GEOFENCE_RADIUS_METERS = 200.0;      // alert if vehicle moves beyond this

// ---------- PIN SETUP ----------
#define GPS_RX_PIN   16   // ESP32 GPIO16 <- GPS TX
#define GPS_TX_PIN   17   // ESP32 GPIO17 -> GPS RX
#define BUZZER_PIN   25

HardwareSerial gpsSerial(2);   // UART2 for GPS
TinyGPSPlus gps;

unsigned long lastPush = 0;
bool geofenceAlertSent = false;

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  connectToWiFi();

  Serial.println("Setup complete. Waiting for GPS fix...");
}

// ---------- MAIN LOOP ----------
void loop() {
  // Continuously feed GPS data to the parser
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Reconnect WiFi automatically if it drops
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  // Every LOCATION_INTERVAL, push current location to Firebase
  if (millis() - lastPush >= LOCATION_INTERVAL) {
    lastPush = millis();
    if (gps.location.isValid()) {
      pushLocationToFirebase();
      checkGeofence();
    } else {
      Serial.println("Waiting for valid GPS fix... (try near a window/outdoors)");
    }
  }
}

// ---------- CONNECT TO WIFI ----------
void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected. IP address: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed. Will retry in loop().");
  }
}

// ---------- PUSH LOCATION TO FIREBASE ----------
void pushLocationToFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  float lat = gps.location.lat();
  float lon = gps.location.lng();
  float spd = gps.speed.isValid() ? gps.speed.kmph() : 0;

  HTTPClient http;
  http.begin(FIREBASE_HOST);
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"lat\":" + String(lat, 6) + ",";
  payload += "\"lng\":" + String(lon, 6) + ",";
  payload += "\"speed_kmph\":" + String(spd, 1) + ",";
  payload += "\"timestamp\":" + String(millis());
  payload += "}";

  int httpResponseCode = http.PUT(payload);   // PUT overwrites the /location.json object each time

  if (httpResponseCode > 0) {
    Serial.println("Firebase updated: " + payload);
  } else {
    Serial.println("Firebase push failed, error: " + String(httpResponseCode));
  }

  http.end();
}

// ---------- GEOFENCE CHECK ----------
void checkGeofence() {
  float distance = distanceBetween(gps.location.lat(), gps.location.lng(), HOME_LAT, HOME_LON);

  if (distance > GEOFENCE_RADIUS_METERS) {
    if (!geofenceAlertSent) {
      Serial.println("Geofence breached! Distance: " + String(distance) + " m");
      triggerBuzzer(3);   // 3 short beeps
      geofenceAlertSent = true;
    }
  } else {
    geofenceAlertSent = false;   // reset once back inside the safe zone
  }
}

// ---------- DISTANCE CALCULATION (Haversine formula) ----------
float distanceBetween(float lat1, float lon1, float lat2, float lon2) {
  const float R = 6371000; // Earth radius in meters
  float dLat = radians(lat2 - lat1);
  float dLon = radians(lon2 - lon1);
  float a = sin(dLat / 2) * sin(dLat / 2) +
            cos(radians(lat1)) * cos(radians(lat2)) *
            sin(dLon / 2) * sin(dLon / 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

// ---------- BUZZER CONTROL ----------
void triggerBuzzer(int beeps) {
  for (int i = 0; i < beeps; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
    delay(300);
  }
}
