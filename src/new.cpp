#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>

const char* ssid = "OPPO Reno12 5G";
const char* password = "qiwa7765";

const int ledPin = 33; // Onboard LED / flash
const char* firmwareURL = "https://github.com/Jobians/esp32-cam/releases/download/v1.0.0/firmware.bin";
const unsigned long OTA_TIMEOUT = 480000; // 8 minutes

WebServer server(80); // Web server on port 80

// ---------- WiFi ----------
void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.println("IP: " + WiFi.localIP().toString());
}

// ---------- LED ----------
void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(ledPin, HIGH);
    delay(delayMs);
    digitalWrite(ledPin, LOW);
    delay(delayMs);
  }
}

// ---------- OTA ----------
bool otaUpdate(const char* url) {
  HTTPClient http;
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.println("Failed to download firmware. HTTP code: " + String(httpCode));
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("Invalid firmware size");
    http.end();
    return false;
  }

  Serial.printf("Firmware size: %d KB\n", contentLength / 1024);
  Serial.printf("Max OTA partition size: %d KB\n", Update.size() / 1024);

  if (!Update.begin(contentLength)) {
    Serial.println("OTA begin failed!");
    Serial.println("Error: " + String(Update.errorString()));
    http.end();
    return false;
  }

  Serial.println("Starting OTA update...");
  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  uint8_t buffer[128];
  int lastProgress = 0;
  unsigned long lastDataTime = millis();

  while (written < contentLength) {
    if (stream->available()) {
      size_t len = stream->read(buffer, sizeof(buffer));
      if (len > 0) {
        Update.write(buffer, len);
        written += len;

        int progress = (written * 100) / contentLength;
        if (progress != lastProgress) {
          Serial.printf("Progress: %d%%\n", progress);
          lastProgress = progress;
        }

        lastDataTime = millis(); // reset timeout
        digitalWrite(ledPin, HIGH);
        delay(50);
        digitalWrite(ledPin, LOW);
      }
    }

    // Timeout check
    if (millis() - lastDataTime > OTA_TIMEOUT) {
      Serial.println("Timeout: OTA aborted");
      Update.abort();
      http.end();
      return false;
    }

    yield();
  }

  if (!Update.end()) {
    Serial.println("OTA end failed!");
    Serial.println("Error: " + String(Update.errorString()));
    http.end();
    return false;
  }

  if (Update.isFinished()) {
    Serial.println("OTA successfully completed. Rebooting...");
    http.end();
    delay(1000);
    ESP.restart();
    return true;
  } else {
    Serial.println("OTA not finished correctly!");
    http.end();
    return false;
  }
}

// ---------- Web server ----------
void handleUpdate() {
  server.send(200, "text/plain", "OTA update triggered! Check Serial for progress.");
  blinkLED(2, 200);
  otaUpdate(firmwareURL);
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);

  connectWiFi();
  blinkLED(3, 200); // indicate ready

  // Setup web server
  server.on("/update", handleUpdate);
  server.begin();
  Serial.println("Web server running. Visit http://" + WiFi.localIP().toString() + "/update to start OTA.");
}

void loop() {
  server.handleClient();

  // Optional idle LED blink
  digitalWrite(ledPin, HIGH);
  delay(1000);
  digitalWrite(ledPin, LOW);
  delay(1000);
}
