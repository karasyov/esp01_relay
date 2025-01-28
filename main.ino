#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTelegram2.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

// Конфигурация железа
#define RELAY_PIN 0
#define LED_PIN 1

// Конфигурация прошивки
const char* FW_VERSION = "1.0.0";
const char* GH_REPO = "karasyov/esp01_relay"; // Замените на свой репозиторий!

// Глобальные объекты
AsyncWebServer server(80);
BearSSL::WiFiClientSecure client; // Используем BearSSL для ESP8266
AsyncTelegram2 myBot(client);     // Передаем клиент в конструктор

// Структура настроек
struct Settings {
  String ssid;
  String password;
  String botToken;
  int64_t chatID;
  String language; // Добавляем язык в настройки
} settings;

void connectWiFi();
void loadSettings();
void setupTelegram();
void setupWebServer();
void handleTelegram();
String getMessage(const String& key);

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  
  LittleFS.begin();
  loadSettings();
  
  connectWiFi();
  setupTelegram();
  setupWebServer();
}

void loop() {
  handleTelegram();
}

// Загрузка настроек из LittleFS
void loadSettings() {
  if (LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "r");
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, file);
    
    settings.ssid = doc["ssid"].as<String>();
    settings.password = doc["password"].as<String>();
    settings.botToken = doc["bot_token"].as<String>();
    settings.chatID = doc["chat_id"].as<int64_t>();  
    settings.language = doc.containsKey("language") ? doc["language"].as<String>() : "en"; // Загружаем язык
    
    file.close();
  }
}

// Сохранение настроек
void saveSettings() {
  DynamicJsonDocument doc(1024);
  doc["ssid"] = settings.ssid;
  doc["password"] = settings.password;
  doc["bot_token"] = settings.botToken;
  doc["chat_id"] = settings.chatID;
  doc["language"] = settings.language; // Сохраняем язык

  File file = LittleFS.open("/config.json", "w");
  serializeJson(doc, file);
  file.close();
}

// Подключение к WiFi
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
  
  for (int i = 0; i < 20; i++) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(500);
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP("ESP-Relay-Config", "setup1234");
  }
}

// Настройка Telegram бота
void setupTelegram() {
  client.setInsecure();
  myBot.setUpdateTime(1000);
  myBot.setTelegramToken(settings.botToken.c_str());

  if (settings.chatID > 0) {
    myBot.sendTo(settings.chatID, getMessage("device_started") + String(FW_VERSION));
  }
}

// Настройка веб-сервера
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid", true)) {
      settings.ssid = request->getParam("ssid", true)->value();
      settings.password = request->getParam("password", true)->value();
      settings.botToken = request->getParam("bot_token", true)->value();
      if(request->hasParam("chat_id", true)){
        String chatIDStr = request->getParam("chat_id", true)->value();
        settings.chatID = strtoll(chatIDStr.c_str(), NULL, 10);
      }
      saveSettings();
      request->send(200, "text/plain", getMessage("settings_saved"));
      ESP.restart();
    }
  });

  server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("state")) {
      String state = request->getParam("state")->value();
      digitalWrite(RELAY_PIN, state == "on" ? LOW : HIGH);
      request->send(200, "text/plain", "OK");
    }
  });

  server.on("/getSettings", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    doc["ssid"] = settings.ssid;
    doc["password"] = settings.password;
    doc["bot_token"] = settings.botToken;
    doc["chat_id"] = settings.chatID;
    doc["language"] = settings.language;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.begin();
}

// Обработчик Telegram сообщений
void handleTelegram() {
  TBMessage msg;
  if (myBot.getNewMessage(msg)) {
    String cmd = msg.text;
    cmd.toLowerCase();

    if (cmd == "/on") {
      digitalWrite(RELAY_PIN, LOW);
      myBot.sendMessage(msg, getMessage("relay_on"));
    }
    else if (cmd == "/off") {
      digitalWrite(RELAY_PIN, HIGH);
      myBot.sendMessage(msg, getMessage("relay_off"));
    }
    else if (cmd == "/version") {
      sendVersionHistory(msg);
    }
    else if (cmd == "/update") {
      checkFirmwareUpdate(msg);
    }
    else if (cmd == "/help") {
      myBot.sendMessage(msg, getMessage("help_message"));
    }
    else if (cmd == "/language") {
      settings.language = (settings.language == "en") ? "ru" : "en";
      saveSettings();
      myBot.sendMessage(msg, getMessage("language_changed"));
    }
  }
}

// Получение сообщения на выбранном языке
String getMessage(const String& key) {
  if (settings.language == "ru") {
    if (key == "device_started") return "⚡ Устройство запущено!\nВерсия: ";
    if (key == "relay_on") return "🔌 Реле ВКЛЮЧЕНО";
    if (key == "relay_off") return "🔌 Реле ВЫКЛЮЧЕНО";
    if (key == "settings_saved") return "Настройки сохранены. Перезагрузка...";
    if (key == "language_changed") return "Язык изменен на " + String(settings.language == "ru" ? "русский" : "английский");
    if (key == "help_message") return "Доступные команды:\n"
                                      "/on - Включить реле\n"
                                      "/off - Выключить реле\n"
                                      "/version - Показать историю версий\n"
                                      "/update - Проверить обновления\n"
                                      "/language - Сменить язык\n"
                                      "/help - Показать это сообщение";
  } else {
    if (key == "device_started") return "⚡ Device Started!\nVersion: ";
    if (key == "relay_on") return "🔌 Relay ON";
    if (key == "relay_off") return "🔌 Relay OFF";
    if (key == "settings_saved") return "Settings saved. Rebooting...";
    if (key == "language_changed") return "Language changed to " + String(settings.language == "ru" ? "Russian" : "English");
    if (key == "help_message") return "Available commands:\n"
                                      "/on - Turn relay ON\n"
                                      "/off - Turn relay OFF\n"
                                      "/version - Show version history\n"
                                      "/update - Check for updates\n"
                                      "/language - Change language\n"
                                      "/help - Show this message";
  }
  return "";
}

// Получение истории версий с GitHub
void sendVersionHistory(TBMessage &msg) {
  HTTPClient https;
  String url = "https://api.github.com/repos/" + String(GH_REPO) + "/releases";
  
  https.begin(client, url);
  https.addHeader("User-Agent", "ESP8266-Relay");
  
  int httpCode = https.GET();
  String response = getMessage("version_history") + "\n\n";
  
  if (httpCode == HTTP_CODE_OK) {
    DynamicJsonDocument doc(3072);
    DeserializationError error = deserializeJson(doc, https.getString());
    
    if (!error) {
      for (JsonObject release : doc.as<JsonArray>()) {
        String version = release["tag_name"].as<String>();
        String date = release["published_at"].as<String>().substring(0, 10);
        String changes = release["body"].as<String>();
        changes.replace("\n", " ");
        
        response += "🟢 v" + version + " (" + date + ")\n";
        response += "📝 " + changes + "\n\n";
      }
    } else {
      response = getMessage("parse_error");
    }
  } else {
    response = getMessage("api_error") + String(httpCode);
  }
  
  https.end();
  myBot.sendMessage(msg, response);
}

// Проверка обновлений
void checkFirmwareUpdate(TBMessage &msg) {
  HTTPClient https;
  String url = "https://api.github.com/repos/" + String(GH_REPO) + "/releases/latest";
  
  https.begin(client, url);
  https.addHeader("User-Agent", "ESP8266-Updater");
  
  int httpCode = https.GET();
  if (httpCode == HTTP_CODE_OK) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, https.getString());
    
    if (!error) {
      String latestVersion = doc["tag_name"].as<String>();
      if (latestVersion != FW_VERSION) {
        String fwUrl = doc["assets"][0]["browser_download_url"].as<String>();
        myBot.sendMessage(msg, getMessage("new_version_found") + latestVersion + getMessage("updating"));
        updateFirmware(fwUrl);
      } else {
        myBot.sendMessage(msg, getMessage("latest_version"));
      }
    }
  }
  https.end();
}

// Обновление прошивки
void updateFirmware(String url) {
  ESPhttpUpdate.setLedPin(LED_PIN, LOW);
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, url);
  
  switch(ret) {
    case HTTP_UPDATE_FAILED:
      break;
    case HTTP_UPDATE_NO_UPDATES:
      break;
    case HTTP_UPDATE_OK:
      break;
  }
}