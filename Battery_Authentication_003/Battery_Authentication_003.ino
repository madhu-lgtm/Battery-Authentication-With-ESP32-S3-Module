#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Preferences.h>

Preferences preferences;
String authorized_macs = "";

// Smart Timer Variables
unsigned long bootTime = 0;
const unsigned long ADMIN_TIMEOUT = 60000; // 60 seconds
bool isAdminMode = true;
bool deviceConnected = false; // <--- NEW: Tracks if a phone is connected!

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ---------------------------------------------------------
// 📡 BLUETOOTH CONNECTION TRACKER (NEW)
// ---------------------------------------------------------
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("📱 Phone Connected! (Timeout timer PAUSED indefinitely)");
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("📱 Phone Disconnected! Triggering immediate Flight Mode...");
        // Force the timer to expire immediately so it goes to Flight Mode
        bootTime = millis() - ADMIN_TIMEOUT; 
    }
};

// ---------------------------------------------------------
// 📡 BLUETOOTH RECEIVER LOGIC
// ---------------------------------------------------------
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String rxValue = pCharacteristic->getValue().c_str();
        if (rxValue.length() > 0) {
            rxValue.trim();
            rxValue.toUpperCase();
            
            if (rxValue.startsWith("+")) {
                String mac = rxValue.substring(1);
                if (authorized_macs.indexOf(mac) == -1) {
                    authorized_macs += mac + ",";
                    preferences.putString("mac_list", authorized_macs);
                    Serial.println("✅ ADDED: " + mac);
                } else {
                    Serial.println("⚠️ MAC already in fleet!");
                }
            }
            else if (rxValue.startsWith("-")) {
                String mac = rxValue.substring(1) + ",";
                if (authorized_macs.indexOf(mac) != -1) {
                    authorized_macs.replace(mac, "");
                    preferences.putString("mac_list", authorized_macs);
                    Serial.println("🗑️ REMOVED: " + mac);
                } else {
                    Serial.println("⚠️ MAC not found.");
                }
            } else {
                Serial.println("❌ Invalid Command. Use +MAC or -MAC");
            }
            Serial.println("🔋 Current Fleet: " + authorized_macs);
        }
    }
};

// ---------------------------------------------------------
// ⚙️ MAIN SETUP
// ---------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(2000); 
    
    preferences.begin("marut_app", false);
    authorized_macs = preferences.getString("mac_list", "D0:D8:9C:8A:89:B4,DE:44:73:F5:43:94,");
    preferences.putString("mac_list", authorized_macs); 
    
    Serial.println("\n" + String("=").substring(0, 40));
    Serial.println("🚀 ESP32-S3 Admin Mode Starting...");
    Serial.println("🔋 Initial Fleet: " + authorized_macs);
    Serial.println(String("=").substring(0, 40));

    BLEDevice::init("Marut_Config");
    BLEServer *pServer = BLEDevice::createServer();
    
    // Attach our new connection tracker!
    pServer->setCallbacks(new MyServerCallbacks());
    
    BLEService *pService = pServer->createService(SERVICE_UUID);
    BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                           CHARACTERISTIC_UUID,
                                           BLECharacteristic::PROPERTY_READ |
                                           BLECharacteristic::PROPERTY_WRITE
                                         );

    pCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
    
    bootTime = millis();
    Serial.println("🌐 BLE Server Live! Connect via nRF Connect app now.");
    Serial.println("⏳ 60-second timer started...");
}

// ---------------------------------------------------------
// 🔄 MAIN LOOP
// ---------------------------------------------------------
void loop() {
    // Check if 60 seconds are up, BUT ONLY if no phone is connected!
    if (isAdminMode && !deviceConnected && (millis() - bootTime > ADMIN_TIMEOUT)) {
        Serial.println("\n⏰ Shutting down Admin Server...");
        
        BLEDevice::deinit(true);
        isAdminMode = false;
        
        Serial.println("🔒 Switched to Flight Scanner Mode.");
        Serial.println("   (Next step: We will build the scanning logic here!)");
    }
    
    if (!isAdminMode) {
        delay(1000);
    }
}