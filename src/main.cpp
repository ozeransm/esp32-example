#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
// put function declarations here:
int myFunction(int, int);
#define LED_PIN 2
AsyncWebServer server(80);

// Встав свій SSID і пароль Wi-Fi
const char* ssid     = "moto g84 5G_3101";
const char* password = "11111111";

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);           // Запускаємо серійний порт
  delay(1000);
  
  Serial.println();
  Serial.println("Connecting to WiFi...");

  WiFi.begin(ssid, password);     // Підключаємося до Wi-Fi

  // Чекаємо підключення
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println("WiFi connected!");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  // Виводимо IP адресу ESP32
  // Ініціалізуємо LittleFS
  if(!LittleFS.begin()){
    Serial.println("LittleFS Mount Failed");
    return;
  }
  
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while(file){
      Serial.print("FILE: ");
      Serial.println(file.name());
      file = root.openNextFile();
      file.close();
  }
  root.close();
  // Віддавати статичні файли
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
  digitalWrite(LED_PIN, HIGH);
  request->send(200, "text/plain", "LED ON");
});

server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
  digitalWrite(LED_PIN, LOW);
  request->send(200, "text/plain", "LED OFF");
});

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.begin();
  Serial.println("HTTP server started");
 
}

void loop() {
  // Тут можна додати код для роботи по Wi-Fi
  
}

