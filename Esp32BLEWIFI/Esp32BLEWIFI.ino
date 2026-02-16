#include <WiFi.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

Preferences preferences;

const String installerPIN = "123456";  // Installer PIN
bool allowWiFiConfig = false;

// BLE UUIDs
#define SERVICE_UUID "00001234-0000-1000-8000-00805f9b34fb"
#define PIN_UUID     "0000abcd-0000-1000-8000-00805f9b34fb"
#define WIFI_UUID    "0000ef01-0000-1000-8000-00805f9b34fb"

BLECharacteristic *pinChar;
BLECharacteristic *wifiChar;

//////////////////////////
// BLE Callbacks
//////////////////////////
//////////////////////////
// BLE Callbacks
//////////////////////////
class PINCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String value = pCharacteristic->getValue();   // Arduino String from BLE
    if (value.length() > 0) {
      if (value == installerPIN) {
        allowWiFiConfig = true;
        Serial.println("PIN verified! Ready for Wi-Fi credentials.");
      } else {
        Serial.println("Incorrect PIN!");
      }
    }
  }
};

class WiFiCredsCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String value = pCharacteristic->getValue();  // Arduino String from BLE
    if (allowWiFiConfig && value.length() > 0) {
      int sep = value.indexOf("|");
      if (sep > 0) {
        String ssid = value.substring(0, sep);
        String pass = value.substring(sep + 1);

        preferences.putString("ssid", ssid); 
        preferences.putString("pass", pass);
        Serial.println("Wi-Fi credentials saved!");
        delay(500);
        ESP.restart();
      }
    }
  }
};

//////////////////////////
// Start BLE Setup
//////////////////////////
void startBLESetup() {
  BLEDevice::init("ESP32_Config");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pinChar = pService->createCharacteristic(
    PIN_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pinChar->setCallbacks(new PINCallbacks());

  wifiChar = pService->createCharacteristic(
    WIFI_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  wifiChar->setCallbacks(new WiFiCredsCallbacks());
  wifiChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->start();
  Serial.println("BLE setup started. Waiting for installer...");
}

//////////////////////////
// Connect to Wi-Fi
//////////////////////////
bool connectWiFi() {
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");

  if (ssid != "" && pass != "") {
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.print("Connecting to Wi-Fi");
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
      delay(500);
      Serial.print(".");
      retries++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Wi-Fi connected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return true;
    } else {
      Serial.println("Wi-Fi connection failed.");
      return false;
    }
  }
  return false;
}

//////////////////////////
// Setup
//////////////////////////
void setup() {
  Serial.begin(115200);
  preferences.begin("wifiCreds", false);

  // Check if Wi-Fi credentials exist
  if (!connectWiFi()) {
    Serial.println("No valid Wi-Fi credentials. Starting BLE provisioning...");
    startBLESetup();
  } else {
    // Connected to Wi-Fi
    // TODO: Start sending data to server here
    Serial.println("Ready for server communication...");
  }
}

//////////////////////////
// Loop
//////////////////////////
void loop() {
  // Placeholder for sending data to server periodically
  if (WiFi.status() == WL_CONNECTED) {
    // Example:
    // sendDataToServer(sensorValue);
    // delay(1000);
  }

  delay(100);
}
