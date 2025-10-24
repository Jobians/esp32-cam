#include "Arduino.h"
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"

const char* ssid = "fREE wIFI";
const char* password = "kISCO@123x4";

#define PIN_FLASHLIGHT 4

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char part_buf[128];

    static int64_t last_frame = 0;
    if (!last_frame) last_frame = esp_timer_get_time();

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        _timestamp.tv_sec = fb->timestamp.tv_sec;
        _timestamp.tv_usec = fb->timestamp.tv_usec;

        if (fb->format != PIXFORMAT_JPEG)
        {
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
            esp_camera_fb_return(fb);
            fb = NULL;
            if (!jpeg_converted)
            {
                Serial.println("JPEG compression failed");
                res = ESP_FAIL;
                break;
            }
        }
        else
        {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res == ESP_OK)
        {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);

        if (fb)
            esp_camera_fb_return(fb);
        else if (_jpg_buf)
            free(_jpg_buf);

        if (res != ESP_OK)
        {
            Serial.println("Send frame failed");
            break;
        }

        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = (fr_end - last_frame) / 1000;
        last_frame = fr_end;
        // Serial.printf("MJPEG: %lld ms/frame (%.1f fps)\n", frame_time, 1000.0 / (float)frame_time);

        vTaskDelay(1); // yield
    }

    return res;
}

void startCameraServer()
{
    Serial.println("Starting Camera Stream Server...");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &index_uri);
    }
    else
    {
        Serial.println("❌ Failed to start HTTP server.");
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(false);
    Serial.println();

    pinMode(PIN_FLASHLIGHT, OUTPUT);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = 5;
    config.pin_d1 = 18;
    config.pin_d2 = 19;
    config.pin_d3 = 21;
    config.pin_d4 = 36;
    config.pin_d5 = 39;
    config.pin_d6 = 34;
    config.pin_d7 = 35;
    config.pin_xclk = 0;
    config.pin_pclk = 22;
    config.pin_vsync = 25;
    config.pin_href = 23;
    config.pin_sccb_sda = 26;
    config.pin_sccb_scl = 27;
    config.pin_pwdn = 32;
    config.pin_reset = -1;

    config.xclk_freq_hz = 10000000; // Better stability for GC2145 clone
    config.pixel_format = PIXFORMAT_RGB565; // Converted to JPEG later
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 20;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_count = psramFound() ? 2 : 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return;
    }

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(300);
        Serial.print(".");
    }

    Serial.println("\n✅ WiFi connected.");
    Serial.print("📶 Stream at: http://");
    Serial.println(WiFi.localIP());

    startCameraServer();

    digitalWrite(PIN_FLASHLIGHT, HIGH);
    delay(300);
    digitalWrite(PIN_FLASHLIGHT, LOW);
}

void loop() {}