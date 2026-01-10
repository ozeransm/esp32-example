#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#define LED_PIN 2

// ===== WiFi =====
const char* ssid     = "moto g84 5G_3101";
const char* password = "11111111";
// const char* ssid     = "iPhone";
// const char* password = "123456789";

// ===== WS server =====
const char* WS_HOST = "ozy.pp.ua";
const uint16_t WS_PORT = 443;
const char* WS_PATH = "/ws";

// ===== WebSocket client =====
WebSocketsClient ws;

// ===== Local HTTP server =====
AsyncWebServer localServer(80);

// ===== Packet types =====
enum PacketType : uint8_t {
  HTTP_REQUEST        = 0x01,
  HTTP_RESPONSE_HDR   = 0x02,
  HTTP_RESPONSE_CHUNK = 0x03,
  HTTP_RESPONSE_END   = 0x04,
  ERROR_PACKET        = 0x05
};

// ===== Forward declarations =====
void wsEvent(WStype_t type, uint8_t * payload, size_t length);
void sendResponseHeaders(uint32_t reqId, const char* mime, size_t size);
void sendResponseChunk(uint32_t reqId, const uint8_t* data, size_t len);
void sendResponseEnd(uint32_t reqId);
String detectMime(const String& path);
void processHttpRequest(uint32_t reqId, const String& path);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(500);

  // ---- FS ----
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  } else {
    Serial.println("LittleFS mounted");
  }

  // ---- WiFi ----
  Serial.println("Connecting WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");
  Serial.println(WiFi.localIP());

  // ---- WebSocket ----
  // ws.begin(WS_HOST, WS_PORT, WS_PATH);
  ws.beginSSL(WS_HOST, WS_PORT, WS_PATH); 
  ws.onEvent(wsEvent);
  ws.setReconnectInterval(5000);

  // ---- Local fallback server ----
  localServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  localServer.begin();
}

void loop() {
  ws.loop();
}

void wsEvent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("WS connected");
    return;
  }

  if (type == WStype_DISCONNECTED) {
    Serial.println("WS disconnected");
    return;
  }

  if (type == WStype_BIN) {
    if (length < 9) return; // мінімум: TYPE + ID + LEN

    uint8_t packetType = payload[0];

    uint32_t reqId = (payload[1] << 24) |
                     (payload[2] << 16) |
                     (payload[3] << 8)  |
                      payload[4];

    uint32_t dataLen = (payload[5] << 24) |
                       (payload[6] << 16) |
                       (payload[7] << 8)  |
                        payload[8];

    const uint8_t* data = payload + 9;

    if (packetType == HTTP_REQUEST) {
      String path = String((const char*)data, dataLen);
      Serial.printf("HTTP_REQUEST id=%u path=%s\n", reqId, path.c_str());
      processHttpRequest(reqId, path);
    }
  }
}

void processHttpRequest(uint32_t reqId, const String& path) {
  Serial.println("======================================");
  Serial.printf("[REQ] id=%u rawPath=%s\n", reqId, path.c_str());

  String filePath = path;
  if (filePath == "/") filePath = "/index.html";

  Serial.printf("[PATH] resolved=%s\n", filePath.c_str());

  // --- Check existence ---
  if (!LittleFS.exists(filePath)) {
    Serial.println("[FS] File does NOT exist!");
    const char* msg = "404 Not Found";
    sendResponseHeaders(reqId, "text/plain", strlen(msg));
    sendResponseChunk(reqId, (const uint8_t*)msg, strlen(msg));
    sendResponseEnd(reqId);
    Serial.println("[END] 404 sent");
    return;
  }

  // --- Open file ---
  File f = LittleFS.open(filePath, "r");
  if (!f) {
    Serial.println("[FS] FAILED to open file!");
    const char* msg = "500 FS Error";
    sendResponseHeaders(reqId, "text/plain", strlen(msg));
    sendResponseChunk(reqId, (const uint8_t*)msg, strlen(msg));
    sendResponseEnd(reqId);
    Serial.println("[END] 500 sent");
    return;
  }

  size_t fileSize = f.size();
  Serial.printf("[FS] File opened OK, size=%u bytes\n", fileSize);

  // --- MIME ---
  String mime = detectMime(filePath);
  Serial.printf("[MIME] %s\n", mime.c_str());

  // --- Send headers ---
  Serial.println("[HDR] Sending headers...");
  sendResponseHeaders(reqId, mime.c_str(), fileSize);

  // --- Send file chunks ---
  uint8_t buf[2048];
  uint32_t totalSent = 0;

  while (true) {
    size_t n = f.read(buf, sizeof(buf));
    if (n == 0) {
      Serial.println("[FS] EOF reached");
      break;
    }

    Serial.printf("[CHUNK] read=%u bytes\n", n);

    sendResponseChunk(reqId, buf, n);
    totalSent += n;

    Serial.printf("[CHUNK] sent, total=%u\n", totalSent);

    ws.loop();   // critical
    delay(0);    // critical
  }

  f.close();

  // --- End packet ---
  Serial.println("[END] Sending end packet...");
  sendResponseEnd(reqId);

  Serial.printf("[DONE] Response complete, totalSent=%u bytes\n", totalSent);
  Serial.println("======================================");
}

void sendResponseHeaders(uint32_t reqId, const char* mime, size_t size) {
  String hdr = String(mime) + "|" + String(size);

  uint32_t len = hdr.length();
  uint8_t* buf = (uint8_t*)malloc(9 + len);

  buf[0] = HTTP_RESPONSE_HDR;
  buf[1] = (reqId >> 24) & 0xFF;
  buf[2] = (reqId >> 16) & 0xFF;
  buf[3] = (reqId >> 8) & 0xFF;
  buf[4] = reqId & 0xFF;

  buf[5] = (len >> 24) & 0xFF;
  buf[6] = (len >> 16) & 0xFF;
  buf[7] = (len >> 8) & 0xFF;
  buf[8] = len & 0xFF;

  memcpy(buf + 9, hdr.c_str(), len);

  ws.sendBIN(buf, 9 + len);
  free(buf);
}

void sendResponseChunk(uint32_t reqId, const uint8_t* data, size_t len) {
  uint8_t* buf = (uint8_t*)malloc(9 + len);

  buf[0] = HTTP_RESPONSE_CHUNK;
  buf[1] = (reqId >> 24) & 0xFF;
  buf[2] = (reqId >> 16) & 0xFF;
  buf[3] = (reqId >> 8) & 0xFF;
  buf[4] = reqId & 0xFF;

  buf[5] = (len >> 24) & 0xFF;
  buf[6] = (len >> 16) & 0xFF;
  buf[7] = (len >> 8) & 0xFF;
  buf[8] = len & 0xFF;

  memcpy(buf + 9, data, len);

  ws.sendBIN(buf, 9 + len);
  free(buf);
}

void sendResponseEnd(uint32_t reqId) {
  uint8_t buf[9];
  buf[0] = HTTP_RESPONSE_END;
  buf[1] = (reqId >> 24) & 0xFF;
  buf[2] = (reqId >> 16) & 0xFF;
  buf[3] = (reqId >> 8) & 0xFF;
  buf[4] = reqId & 0xFF;
  buf[5] = buf[6] = buf[7] = buf[8] = 0;

  ws.sendBIN(buf, 9);
}

String detectMime(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg"))  return "image/jpeg";
  if (path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".txt"))  return "text/plain";
  return "application/octet-stream";
}
