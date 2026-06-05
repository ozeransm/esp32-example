#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>

#define SD_CS 5

// ===== Налаштування Wi-Fi =====
const char *ssid = "moto g84 5G_3101";
const char *password = "11111111";

// ===== Локальний HTTP сервер =====
AsyncWebServer localServer(80);

// ===== Оголошення функцій =====
String detectMime(const String &path);
void setupEndpoints();

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Безлімітний Веб-сервер та SD-картка запускаються ---");

  // Налаштування апаратного SPI (SCK=18, MISO=19, MOSI=23, CS=5)
  SPI.begin(18, 19, 23, SD_CS);

  // Запуск SD-карти на частоті 4 МГц для стабільного віддавання сторінок
  if (!SD.begin(SD_CS, SPI, 4000000))
  {
    Serial.println("SD.begin failed! Перевірте формат FAT32 або підключення.");
  }
  else
  {
    Serial.println("SD.begin OK!");
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("Card size: %lluMB\n", cardSize);
  }

  // Підключення до WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP-адреса сервера: ");
  Serial.println(WiFi.localIP());

  // Налаштування CORS заголовків (щоб fetch з localhost на ПК не блокувався браузером)
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  // Ініціалізація ендпоінтів для Preact (Insert / Delete)
  setupEndpoints();

  // ===== МАРШРУТИ ДЛЯ СТАТИКИ З SD-КАРТИ =====

  // Головна сторінка SPA
  localServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
    if (SD.exists("/index.html")) {
      request->send(SD, "/index.html", "text/html");
    } else {
      request->send(404, "text/plain", "404: index.html not found on SD");
    } });

  // Автоматична роздача збірки Preact (js, css, json/jsonl файли, картинки)
  localServer.onNotFound([](AsyncWebServerRequest *request)
                         {
    String path = request->url();
    if (SD.exists(path)) {
      String mime = detectMime(path);
      request->send(SD, path, mime);
    } else {
      request->send(404, "text/plain", "404: File Not Found on SD");
    } });

  // Запуск локального HTTP сервера
  localServer.begin();
  Serial.println("Локальний HTTP сервер запущено на порту 80.");
}

void loop()
{
  // Сервер асинхронний, loop вільний і готовий до інших задач
}

// ===== РЕАЛІЗАЦІЯ ЕНДПОІНТІВ ДЛЯ РОБОТИ З JSON LINES (NDJSON) БАЗОЮ =====
void setupEndpoints()
{
  // 1. ОПЦІЇ (Preflight OPTIONS запити від браузера перед POST/DELETE)
  localServer.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {});
  localServer.on("/api/insert", HTTP_OPTIONS, [](AsyncWebServerRequest *request)
                 { request->send(200); });
  localServer.on("/api/delete", HTTP_OPTIONS, [](AsyncWebServerRequest *request)
                 { request->send(200); });
  // 4. ОПЦІЇ ДЛЯ UPDATE (Preflight OPTIONS запит)
  localServer.on("/api/update", HTTP_OPTIONS, [](AsyncWebServerRequest *request)
                 { request->send(200); });
  // 2. БЕЗЛІМІТНИЙ INSERT (Додавання об'єкта {id, name, text} строго в кінець файлу)
  localServer.on("/api/insert", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
                 {
        // Створюємо маленький буфер суто під ОДИН новий запис
        JsonDocument newRecordDoc;
        // StaticJsonDocument<512> newRecordDoc;
        DeserializationError error = deserializeJson(newRecordDoc, data, len);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"Bad JSON\"}");
            return;
        }

        // Відкриваємо файл у режимі FILE_APPEND (дозапис у хвіст)
        // Старий вміст файлу не читається в оперативну пам'ять!
        File file = SD.open("/DB/db.jsonl", FILE_APPEND);
        if (file) {
            serializeJson(newRecordDoc, file); // Записуємо об'єкт як один компактний рядок
            file.println();                   // Обов'язково ставимо символ переносу рядка '\n'
            file.close();
            
            request->send(200, "application/json", "{\"status\":\"success\"}");
        } else {
            request->send(500, "application/json", "{\"error\":\"SD append error\"}");
        } });

  // 3. БЕЗЛІМІТНЕ ВИДАЛЕННЯ (Фільтрація через проміжний файл temp.jsonl рядок за рядком)
  localServer.on("/api/delete", HTTP_DELETE, [](AsyncWebServerRequest *request)
                 {
        if (!request->hasParam("id")) {
            request->send(400, "application/json", "{\"error\":\"Missing ID param\"}");
            return;
        }
        
        String targetId = request->getParam("id")->value();
        
        // Відкриваємо оригінальну базу для читання, а тимчасовий файл — для запису
        File sourceFile = SD.open("/DB/db.jsonl", FILE_READ);
        File tempFile = SD.open("/DB/temp.jsonl", FILE_WRITE);
        
        if (!sourceFile || !tempFile) {
            if (sourceFile) sourceFile.close();
            if (tempFile) tempFile.close();
            request->send(500, "application/json", "{\"error\":\"File system error\"}");
            return;
        }
        JsonDocument lineDoc;
        // StaticJsonDocument<512> lineDoc; // Крихітний буфер для обробки одного поточного рядка
        bool found = false;

        // Зчитуємо оригінальний файл рядок за рядком
        while (sourceFile.available()) {
            String line = sourceFile.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue; // Пропускаємо порожні рядки, якщо вони є

            lineDoc.clear();
            DeserializationError err = deserializeJson(lineDoc, line);
            if (err) {
                // Якщо якийсь рядок пошкоджений, про всяк випадок копіюємо його як є
                tempFile.println(line);
                continue;
            }

            // Перевіряємо, чи збігається ID
            if (lineDoc["id"].as<String>() == targetId) {
                found = true; // Знайшли запис, який треба видалити (просто НЕ копіюємо його)
            } else {
                tempFile.println(line); // Усі інші записи переписуємо в тимчасовий файл
            }
        }

        sourceFile.close();
        tempFile.close();

        // Якщо видалення відбулося, робимо рокіровку файлів
        if (found) {
            SD.remove("/DB/db.jsonl");             // Видаляємо стару базу
            SD.rename("/DB/temp.jsonl", "/DB/db.jsonl"); // Тимчасовий файл стає новою базою
            request->send(200, "application/json", "{\"status\":\"deleted\"}");
        } else {
            SD.remove("/DB/temp.jsonl"); // Якщо ID не знайшли, просто прибираємо за собою темп-файл
            request->send(404, "application/json", "{\"error\":\"Record not found\"}");
        } });

  // 5. БЕЗЛІМІТНЕ ОНОВЛЕННЯ (Порядкове перезаписування через temp.jsonl)
  localServer.on("/api/update", HTTP_PUT, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
                 {
        JsonDocument updateDoc;
        DeserializationError error = deserializeJson(updateDoc, data, len);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"Bad JSON\"}");
            return;
        }

        if (!updateDoc.containsKey("id")) {
            request->send(400, "application/json", "{\"error\":\"Missing ID in body\"}");
            return;
        }
        
        String targetId = updateDoc["id"].as<String>();
        
        File sourceFile = SD.open("/DB/db.jsonl", FILE_READ);
        File tempFile = SD.open("/DB/temp.jsonl", FILE_WRITE);
        
        if (!sourceFile || !tempFile) {
            if (sourceFile) sourceFile.close();
            if (tempFile) tempFile.close();
            request->send(500, "application/json", "{\"error\":\"File system error during update\"}");
            return;
        }

        JsonDocument lineDoc;
        bool updated = false;

        // Читаємо базу рядок за рядком
        while (sourceFile.available()) {
            String line = sourceFile.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            lineDoc.clear();
            DeserializationError err = deserializeJson(lineDoc, line);
            if (err) {
                tempFile.println(line); // Якщо рядок битий, просто копіюємо його
                continue;
            }

            // Якщо знайшли потрібний ID — записуємо нові дані з updateDoc
            if (lineDoc["id"].as<String>() == targetId) {
                serializeJson(updateDoc, tempFile);
                tempFile.println();
                updated = true;
            } else {
                // Всі інші записи копіюємо як є
                tempFile.println(line);
            }
        }

        sourceFile.close();
        tempFile.close();

        if (updated) {
            SD.remove("/DB/db.jsonl");
            SD.rename("/DB/temp.jsonl", "/DB/db.jsonl");
            request->send(200, "application/json", "{\"status\":\"updated\"}");
        } else {
            SD.remove("/DB/temp.jsonl");
            request->send(404, "application/json", "{\"error\":\"Record to update not found\"}");
        } });
}

// ===== Визначення MIME-типів =====
String detectMime(const String &path)
{
  if (path.endsWith(".html"))
    return "text/html";
  if (path.endsWith(".css"))
    return "text/css";
  if (path.endsWith(".js"))
    return "application/javascript";
  if (path.endsWith(".json"))
    return "application/json";
  if (path.endsWith(".jsonl"))
    return "application/json"; // Додано для підтримки розширення JSON Lines
  if (path.endsWith(".ico"))
    return "image/x-icon";
  if (path.endsWith(".png"))
    return "image/png";
  if (path.endsWith(".jpg"))
    return "image/jpeg";
  return "text/plain";
}