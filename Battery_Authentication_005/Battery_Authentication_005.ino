#include <NimBLEDevice.h>
#include <Preferences.h>

// ==========================================
// ⚙️ TUNING PARAMETERS (ADJUST THESE)
// ==========================================
const int RAPID_SCAN_ATTEMPTS = 5; 
const int RAPID_SCAN_TIMEOUT = 2000;  // NIMBLE V2 FIX: This is now in MILLISECONDS! (2000 = 2 seconds)
const int CYCLE_PAUSE_TIME = 2000;    // Time (in milliseconds) to pause between full cycles
const int MAX_FAILED_CYCLES = 3;      // Consecutive failed cycles needed to cut SBUS

const unsigned long ADMIN_TIMEOUT = 60000; // 60-second fallback boot window
// ==========================================

// ==========================================
// 🔌 HARDWARE SETUP Pins
// ==========================================
const int SBUS_SWITCH_PIN = 4; // Pin driving the CD4042BCN / CD4052 chip
const int LED_PIN = 2;         // Warning LED Pin
// ==========================================

Preferences preferences;
String authorized_macs = "";

unsigned long bootTime = 0;
bool isAdminMode = true;
bool deviceConnected = false;
int failed_cycles = 0;

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ---------------------------------------------------------
// 📡 NIMBLE V2 ADMIN CALLBACKS (FIXED SIGNATURES)
// ---------------------------------------------------------
class MyServerCallbacks: public NimBLEServerCallbacks {
    // FIX: NimBLE V2 requires the NimBLEConnInfo parameter
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
        Serial.println("📱 Phone Connected! (Timeout timer PAUSED indefinitely)");
    }
    
    // FIX: NimBLE V2 requires connInfo and reason parameters
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
        Serial.println("📱 Phone Disconnected! Triggering immediate Flight Mode...");
        bootTime = millis() - ADMIN_TIMEOUT; 
    }
};

class MyCallbacks: public NimBLECharacteristicCallbacks {
    // FIX: NimBLE V2 requires the NimBLEConnInfo parameter to receive text
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {
        String rxValue = pCharacteristic->getValue().c_str();
        if (rxValue.length() > 0) {
            rxValue.trim();
            rxValue.toUpperCase();
            
            // Expected format: +14S:AA:BB:CC:DD:EE:FF
            if (rxValue.startsWith("+")) {
                String entry = rxValue.substring(1);
                if (authorized_macs.indexOf(entry) == -1) {
                    authorized_macs += entry + ",";
                    preferences.putString("mac_list", authorized_macs);
                    Serial.println("✅ ADDED TO FLEET: " + entry);
                } else {
                    Serial.println("⚠️ Battery already exists in fleet!");
                }
            }
            // Expected format: -14S:AA:BB:CC:DD:EE:FF
            else if (rxValue.startsWith("-")) {
                String entry = rxValue.substring(1) + ",";
                if (authorized_macs.indexOf(entry) != -1) {
                    authorized_macs.replace(entry, "");
                    preferences.putString("mac_list", authorized_macs);
                    Serial.println("🗑️ REMOVED FROM FLEET: " + entry);
                } else {
                    Serial.println("⚠️ Battery not found.");
                }
            } else {
                Serial.println("❌ Invalid Command. Format: +14S:MAC or +6S:MAC");
            }
            Serial.println("🔋 Current Fleet Config: " + authorized_macs);
        }
    }
};

// ---------------------------------------------------------
// 🚨 LED WARNING PATTERN
// ---------------------------------------------------------
void triggerLedWarning(int failures) {
    if (failures == 1) {
        digitalWrite(LED_PIN, HIGH); delay(150); digitalWrite(LED_PIN, LOW);
    } 
    else if (failures == 2) {
        for (int i = 0; i < 2; i++) {
            digitalWrite(LED_PIN, HIGH); delay(150); digitalWrite(LED_PIN, LOW); delay(150);
        }
    } 
    else if (failures >= MAX_FAILED_CYCLES) {
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_PIN, HIGH); delay(150); digitalWrite(LED_PIN, LOW); delay(150);
        }
        digitalWrite(LED_PIN, HIGH); 
        Serial.println("💡 LED: Solid RED (Critical System Failure)");
    }
}

// ---------------------------------------------------------
// ⚙️ INITIALIZATION
// ---------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(2000); 
    
    pinMode(SBUS_SWITCH_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(SBUS_SWITCH_PIN, LOW); // Lock SBUS on boot
    digitalWrite(LED_PIN, LOW);
    
    preferences.begin("marut_app", false);
    authorized_macs = preferences.getString("mac_list", "14S:D0:D8:9C:8A:89:B4,6S:DE:44:73:F5:43:94,6S:EB:54:0E:3F:68:1A,");
    preferences.putString("mac_list", authorized_macs); 
    
    Serial.println("\n========================================");
    Serial.println("🛠️ PHASE 1: BOOTING ADMIN PORTAL (60s Window)");
    Serial.println("🔋 Registered Fleet: " + authorized_macs);
    Serial.println("========================================");

    NimBLEDevice::init("Marut_Config");
    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks()); // <--- Now fully connected to NimBLE V2
    
    NimBLEService *pService = pServer->createService(SERVICE_UUID);
    NimBLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                           CHARACTERISTIC_UUID,
                                           NIMBLE_PROPERTY::READ |
                                           NIMBLE_PROPERTY::WRITE
                                         );

    pCharacteristic->setCallbacks(new MyCallbacks()); // <--- Now fully capable of receiving text
    pService->start();
    
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName("Marut_Config"); 
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();
    
    bootTime = millis();
    Serial.println("🌐 BLE Server Live! Connect via nRF Connect app now.");
    Serial.println("⏳ 60-second timer started...");
}

// ---------------------------------------------------------
// 🔄 CORE LOOP & FLIGHT RUNTIME
// ---------------------------------------------------------
void loop() {
    // 1. Handle Admin Mode Timeout Closure (Will correctly PAUSE if deviceConnected is true)
    if (isAdminMode && !deviceConnected && (millis() - bootTime > ADMIN_TIMEOUT)) {
        Serial.println("\n⏰ Boot window closed. Transitioning to Flight Security Scanner...");
        NimBLEDevice::deinit(true); 
        isAdminMode = false;
        
        NimBLEDevice::init("");
        Serial.println("🔒 Switched to Flight Scanner Mode. Monitoring Power System...");
    }

    // 2. PHASE 2: ACTIVE FLIGHT SECURITY SCANNING
    if (!isAdminMode) {
        Serial.println("\n==================================================");
        Serial.println("🚀 STARTING NEW RAPID SCAN CYCLE");
        Serial.println("==================================================");

        int found14SCount = 0;
        int found6SCount = 0;
        String detectedThisCycle = ""; 

        NimBLEScan* pBLEScan = NimBLEDevice::getScan();
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99);

        // Execute the rapid scanning phase
        for (int i = 1; i <= RAPID_SCAN_ATTEMPTS; i++) {
            Serial.printf(" [Rapid Scan %d/%d] Scanning airwaves...\n", i, RAPID_SCAN_ATTEMPTS);
            
            pBLEScan->start(RAPID_SCAN_TIMEOUT, false); 
            
            // Wait for the scan to physically complete in NimBLE V2
            while(pBLEScan->isScanning()) {
                delay(10);
            }

            NimBLEScanResults foundDevices = pBLEScan->getResults(); 
            int deviceCount = foundDevices.getCount();
            
            for (int d = 0; d < deviceCount; d++) {
                const NimBLEAdvertisedDevice* device = foundDevices.getDevice(d); 
                String currentMac = device->getAddress().toString().c_str();
                currentMac.toUpperCase();

                // Check if we haven't processed this MAC yet in this specific cycle
                if (detectedThisCycle.indexOf(currentMac) == -1) {
                    String match14S = "14S:" + currentMac;
                    String match6S = "6S:" + currentMac;

                    // Evaluate against Authorized 14S criteria
                    if (authorized_macs.indexOf(match14S) != -1) {
                        found14SCount++;
                        detectedThisCycle += currentMac + ",";
                        Serial.println("   🔋 Found Authorized 14S Power System: [" + currentMac + "]");
                    }
                    // Evaluate against Authorized 6S criteria
                    else if (authorized_macs.indexOf(match6S) != -1) {
                        found6SCount++;
                        detectedThisCycle += currentMac + ",";
                        Serial.println("   🔋 Found Authorized 6S Module: [" + currentMac + "]");
                    }
                }
            }
            
            pBLEScan->clearResults(); // Crucial step to eliminate ESP32 memory leaks

            // Optimization: Break out early if valid configuration limits are met
            if (found14SCount >= 1 || found6SCount >= 2) {
                break;
            }
            delay(500); 
        }

        // --- CYCLE EVALUATION METRICS ---
        if (found14SCount >= 1 || found6SCount >= 2) {
            Serial.println("\n✅ SUCCESS: Hardware verification complete.");
            failed_cycles = 0;
            digitalWrite(LED_PIN, LOW); // Clear warning LED
            
            if (digitalRead(SBUS_SWITCH_PIN) == LOW) {
                Serial.println("🔓 HARDWARE UNLOCKED: Connecting RC Receiver line to Flight Controller.");
            }
            digitalWrite(SBUS_SWITCH_PIN, HIGH); // Force Select Line High (Pass RC Signal)
        } 
        else {
            failed_cycles++;
            Serial.printf("\n⚠️ WARNING: Power system validation failed! (Consecutive failures: %d/%d)\n", failed_cycles, MAX_FAILED_CYCLES);
            
            triggerLedWarning(failed_cycles);

            // --- THE CRITICAL HARDWARE ISOLATION INTERACTION ---
            if (failed_cycles >= MAX_FAILED_CYCLES) {
                Serial.println("\n❌ CRITICAL: Unverified power payload signature detected!");
                Serial.println("🛑 TRIGGERING HARDWARE SBUS DISCONNECT 🛑");
                digitalWrite(SBUS_SWITCH_PIN, LOW); // Dump line to ground to invoke FC Failsafe
            }
        }

        // Cycle Pacing Intermission
        delay(CYCLE_PAUSE_TIME);
    }
}