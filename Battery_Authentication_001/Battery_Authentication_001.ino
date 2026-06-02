void setup() {
  // Start the serial communication
  Serial.begin(115200);
  
  // Wait a moment for the USB connection to stabilize
  delay(2000); 
  
  Serial.println("\n=================================");
  Serial.println("🚀 ESP32-S3 Zero is ALIVE!");
  Serial.println("=================================");
}

void loop() {
  Serial.println("System Running: Waiting for BLE commands...");
  delay(5000); // Pause for 5 seconds
}
