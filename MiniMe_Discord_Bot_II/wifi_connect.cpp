#include "minime.h"
#include "esp_wifi.h"

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // modem sleep breaks ArduinoOTA (port 3232)
  esp_wifi_set_ps(WIFI_PS_NONE); // IDF: no Wi-Fi power save (fewer WS blips)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  showTransient("WiFi", "Connecting...");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000UL) {
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED) {
    showTransient("WiFi", "Timeout");
    // fall through; ensureWifiForGateway() retries in loop()
    return;
  }
  // Re-assert after associate (some stacks re-enable sleep on connect).
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  showTransient("WiFi", "Connected");
}
