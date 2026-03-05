#include <WiFi.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

Preferences preferences;

const String installerPIN = "123456";
bool allowWiFiConfig = false;

// ─── BOOT Button ──────────────────────────────────────────────────────────────
#define BOOT_BUTTON_PIN  0       // GPIO0 = built-in BOOT button
#define HOLD_DURATION_MS 3000   // Hold 3 seconds to forget Wi-Fi

// ─── BLE UUIDs ────────────────────────────────────────────────────────────────
#define SERVICE_UUID    "00001234-0000-1000-8000-00805f9b34fb"
#define PIN_UUID        "0000abcd-0000-1000-8000-00805f9b34fb"
#define WIFI_UUID       "0000ef01-0000-1000-8000-00805f9b34fb"
#define SCAN_UUID       "0000ef02-0000-1000-8000-00805f9b34fb"
#define SSID_LIST_UUID  "0000ef03-0000-1000-8000-00805f9b34fb"
#define RESPONSE_UUID   "0000ef05-0000-1000-8000-00805f9b34fb"

BLECharacteristic *pinChar;
BLECharacteristic *wifiChar;
BLECharacteristic *scanChar;
BLECharacteristic *ssidListChar;
BLECharacteristic *responseChar;

//////////////////////////
// Helpers
//////////////////////////
void sendResponse(const char* msg) {
  responseChar->setValue(msg);
  responseChar->notify();
  Serial.printf("[BLE] Response sent: %s\n", msg);
}

//////////////////////////
// Forget Wi-Fi
//////////////////////////
void forgetWiFiCredentials() {
  Serial.println("Forgetting Wi-Fi credentials...");
  preferences.remove("ssid");
  preferences.remove("pass");
  Serial.println("Credentials cleared. Restarting into BLE provisioning mode...");
  delay(500);
  ESP.restart();
}

//////////////////////////
// Boot Button
// Hold GPIO0 for 3 seconds to forget Wi-Fi and restart into BLE mode
//////////////////////////
void checkBootButton() {
  static unsigned long pressStart = 0;
  static bool pressing = false;

  bool held = (digitalRead(BOOT_BUTTON_PIN) == LOW);

  if (held && !pressing) {
    pressStart = millis();
    pressing = true;
    Serial.println("BOOT button pressed. Hold 3s to forget Wi-Fi...");
  }

  if (pressing && !held) {
    pressing = false;
    Serial.println("BOOT button released early. Cancelled.");
  }

  if (pressing && held) {
    unsigned long duration = millis() - pressStart;
    if (duration > 0 && duration % 1000 < 20) {
      Serial.printf("Holding... %lu s / 3s\n", duration / 1000 + 1);
    }
    if (duration >= HOLD_DURATION_MS) {
      pressing = false;
      forgetWiFiCredentials();
    }
  }
}

//////////////////////////
// Wi-Fi Scanner
// Returns pipe-delimited string: "SSID1|SSID2|SSID3|..."
//////////////////////////
String scanWiFiNetworks() {
  Serial.println("Scanning for Wi-Fi networks...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks\n", n);

  String result = "";
  for (int i = 0; i < n; i++) {
    if (i > 0) result += "|";
    result += WiFi.SSID(i);
  }

  WiFi.scanDelete();
  return result;
}

//////////////////////////
// BLE Callbacks
//////////////////////////

// 1. PIN verification → notifies PIN_OK or PIN_FAIL
class PINCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String value = pCharacteristic->getValue();
    if (value.length() > 0) {
      if (value == installerPIN) {
        allowWiFiConfig = true;
        sendResponse("PIN_OK");
        Serial.println("PIN verified!");
      } else {
        sendResponse("PIN_FAIL");
        Serial.println("Incorrect PIN.");
      }
    }
  }
};

// 2. Scan trigger → scans and notifies SSID list
class ScanCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String value = pCharacteristic->getValue();
    if (allowWiFiConfig && value.length() > 0) {
      String ssidList = scanWiFiNetworks();
      Serial.println("Sending SSID list: " + ssidList);

      if (ssidList.length() > 512) {
        ssidList = ssidList.substring(0, 512);
      }

      ssidListChar->setValue(ssidList.c_str());
      ssidListChar->notify();
    }
  }
};

// 3. Wi-Fi credentials → saves and notifies WIFI_SAVED before restart
class WiFiCredsCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String value = pCharacteristic->getValue();
    if (allowWiFiConfig && value.length() > 0) {
      int sep = value.indexOf("|");
      if (sep > 0) {
        String ssid = value.substring(0, sep);
        String pass = value.substring(sep + 1);

        preferences.putString("ssid", ssid);
        preferences.putString("pass", pass);
        Serial.println("Wi-Fi credentials saved: SSID=" + ssid);

        sendResponse("WIFI_SAVED");
        delay(800); // Give BLE time to deliver notify before restart
        ESP.restart();
      } else {
        Serial.println("Invalid format. Expected: SSID|password");
      }
    }
  }
};

//////////////////////////
// BLE Setup
//////////////////////////
void startBLESetup() {
  BLEDevice::init("ESP32_Config");
  BLEServer *pServer = BLEDevice::createServer();

  // 32 handles for 5 characteristics + descriptors
  BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID), 32);

  // PIN (write)
  pinChar = pService->createCharacteristic(
    PIN_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pinChar->setCallbacks(new PINCallbacks());

  // Scan trigger (write)
  scanChar = pService->createCharacteristic(
    SCAN_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  scanChar->setCallbacks(new ScanCallbacks());

  // SSID list (read + notify)
  ssidListChar = pService->createCharacteristic(
    SSID_LIST_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  ssidListChar->addDescriptor(new BLE2902());
  ssidListChar->setValue("");

  // Wi-Fi credentials (write)
  wifiChar = pService->createCharacteristic(
    WIFI_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  wifiChar->setCallbacks(new WiFiCredsCallbacks());

  // Response / status notifications (read + notify)
  responseChar = pService->createCharacteristic(
    RESPONSE_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  responseChar->addDescriptor(new BLE2902());
  responseChar->setValue("");

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->start();

  Serial.println("BLE provisioning started. Waiting for installer...");
}

//////////////////////////
// Wi-Fi Connection
//////////////////////////
bool connectWiFi() {
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");

  if (ssid == "" || pass == "") return false;

  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("Connecting to Wi-Fi: " + ssid);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi connected! IP: " + WiFi.localIP().toString());
    return true;
  }

  Serial.println("Wi-Fi connection failed.");
  return false;
}

//////////////////////////
// Setup
//////////////////////////
void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP); // GPIO0 active LOW

  preferences.begin("wifiCreds", false);

  if (!connectWiFi()) {
    Serial.println("No valid Wi-Fi credentials. Starting BLE provisioning...");
    startBLESetup();
  } else {
    Serial.println("Ready. Hold BOOT button 3s at any time to forget Wi-Fi.");
  }
}

//////////////////////////
// Loop
//////////////////////////
void loop() {
  // Always poll BOOT button regardless of WiFi/BLE state
  checkBootButton();

  if (WiFi.status() == WL_CONNECTED) {
    // TODO: send data to server
    // sendDataToServer(sensorValue);
  }

  delay(20); // 20ms for responsive button detection
}
