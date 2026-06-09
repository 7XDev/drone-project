#include <WiFi.h>
#include <esp_wifi.h>

// Access Point credentials
const char* ssid     = "dronewifi";
const char* password = "12345678";

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32-CAM Access Point ===");

  // ===== AP CONFIGURATION =====

  // Step 1: Initialize WiFi in AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  // Step 2: Disable WiFi power save
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Step 3: Set max TX power
  esp_wifi_set_max_tx_power(78);

  // Step 4: Force lowest data rate for maximum range (1 Mbps DSSS)
  esp_wifi_config_80211_tx_rate(WIFI_IF_AP, WIFI_PHY_RATE_1M_L);

  // Step 5: Restart AP with all settings applied
  WiFi.softAP(ssid, password);

  // Step 6: Print info
  IPAddress IP = WiFi.softAPIP();
  Serial.println("===========================================");
  Serial.println("AP Configuration:");
  Serial.print("  SSID:      "); Serial.println(ssid);
  Serial.print("  Password:  "); Serial.println(password);
  Serial.print("  IP:        "); Serial.println(IP);
  Serial.print("  Channel:   "); Serial.println(WiFi.channel());
  Serial.println("===========================================");
  Serial.println("AP ACTIVE - No LEDs");
  Serial.println("===========================================");
}

void loop() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 10000) {
    lastCheck = millis();
    Serial.printf("Stations connected: %d\n", WiFi.softAPgetStationNum());
  }
  delay(100);
}