#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ==== Wi-Fi ====
const char* ssid = "OPPO Reno12 5G";
const char* password = "qiwa7765";

// ==== Camera Pin Definitions (AI Thinker) ====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define LED_GPIO_NUM      4   // Flash LED pin

WebServer server(80);
bool flashOn = false;

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  Serial.println();

  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);

  // ---- Camera Config ----
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565; // will convert to JPEG
  config.frame_size = FRAMESIZE_240X240;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  Serial.println("Camera initialized!");

  // ---- Wi-Fi ----
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! Visit: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/cam");

  // ---- Web Routes ----
  server.on("/", []() { server.sendHeader("Location", "/cam"); server.send(302); });
  server.on("/cam", handleCam);
  server.on("/photo", handlePhoto);
  server.on("/flash", handleFlash);
  server.begin();
}

// ==== Web Handlers ====
void handleCam() {
  String page = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head><meta name="viewport" content="width=device-width,initial-scale=1">
  <style>
    body { background:#111; color:#eee; text-align:center; font-family:Arial; }
    img { width:90%; border:3px solid #444; border-radius:10px; margin-top:10px; }
    button { background:#0f9d58; border:none; padding:10px 20px; color:white;
             font-size:18px; border-radius:5px; margin-top:10px; cursor:pointer; }
    button.off { background:#d23f31; }
  </style>
  </head>
  <body>
  <h2>ESP32-CAM MJPEG Stream</h2>
  <img id="photo" src="/photo">
  <br>
  <button id="flashBtn" onclick="toggleFlash()">Flash: OFF</button>
  <script>
    function toggleFlash(){
      fetch('/flash').then(r=>r.text()).then(t=>{
        const btn=document.getElementById('flashBtn');
        if(t.includes('ON')){btn.textContent='Flash: ON';btn.classList.remove('off');}
        else {btn.textContent='Flash: OFF';btn.classList.add('off');}
      });
    }
  </script>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", page);
}

void handlePhoto() {
  WiFiClient client = server.client();
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println();

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      continue;
    }

    size_t buf_len = fb->len;
    uint8_t *buf = fb->buf;

    if (fb->format != PIXFORMAT_JPEG) {
      uint8_t *jpg_buf = NULL;
      size_t jpg_len = 0;
      if (!frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
        Serial.println("JPEG conversion failed");
        esp_camera_fb_return(fb);
        continue;
      }
      buf = jpg_buf;
      buf_len = jpg_len;
      esp_camera_fb_return(fb);
      fb = NULL;
    }

    client.printf("--frame\r\n");
    client.printf("Content-Type: image/jpeg\r\n");
    client.printf("Content-Length: %u\r\n\r\n", (unsigned int)buf_len);
    client.write(buf, buf_len);
    client.print("\r\n");

    if (fb) esp_camera_fb_return(fb);
    else free(buf);

    delay(100); // ~10 fps, adjust as needed
  }
}

void handleFlash() {
  flashOn = !flashOn;
  digitalWrite(LED_GPIO_NUM, flashOn ? HIGH : LOW);
  server.send(200, "text/plain", flashOn ? "ON" : "OFF");
}

void loop() {
  server.handleClient();
}
