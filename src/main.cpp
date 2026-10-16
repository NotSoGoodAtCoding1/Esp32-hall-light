// ESP32: WS2812 лента (60 диодов) + 2 датчика движения HC-SR501 + WiFi Web Config
// 
// Подключения:
//   WS2812  -> GPIO 5
//   HC-SR501 #1 -> GPIO 33
//   HC-SR501 #2 -> GPIO 34
//   HC-SR501: VCC -> 5V, GND -> GND, OUT -> GPIO
// 
// Логика:
//   Лента разделена на 2 сегмента по 30 диодов
//   Сегмент 1 (диоды 0-29) управляется датчиком #1 (GPIO 33)
//   Сегмент 2 (диоды 30-59) управляется датчиком #2 (GPIO 34)
//   При первом включении: тест радугой по всей ленте
//   Яркость настраивается через веб-интерфейс
// 
// Веб-интерфейс:
//   Подключитесь к сети WiFi "LED-Controller" (пароль: 12345678)
//   или к вашей сети, если настроены credentials
//   Откройте в браузере: http://<IP_адрес_ESP32>/

// ========== Настройки WiFi (измените на свои) ==========
#define WIFI_SSID "Beeline_2G_FF5834"      // Имя вашей WiFi сети
#define WIFI_PASS "Gq6fersG"  // Пароль от WiFi
#define AP_SSID "LED-Controller"        // Имя точки доступа (если WiFi не подключен)
#define AP_PASS "12345678"              // Пароль точки доступа
#define MAX_WIFI_RETRY_ATTEMPTS    10   // Максимум попыток подключения к WiFi
#define WIFI_RECONNECT_DELAY       5000  // Задержка между попытками переподключения (мс)
#define WIFI_CONNECT_TIMEOUT       30000 // Таймаут соединения с WiFi (мс)

// ========== Настройки ленты ==========
#define NEOPIXEL_PIN    5     // Пин для ленты WS2812
#define PIR_PIN_1       33    // Пин для датчика HC-SR501 #1
#define PIR_PIN_2       34    // Пин для датчика HC-SR501 #2
#define TOTAL_PIXELS    60    // Общее количество светодиодов
#define SEGMENT_SIZE    30    // Размер каждого сегмента

// ========== Настройки по умолчанию ==========
#define DEFAULT_BRIGHTNESS_1  255       // Яркость при 1 сегменте
#define DEFAULT_BRIGHTNESS_2  179       // Яркость при 2 сегментах (70%)
#define DEFAULT_TIMEOUT        10000     // Таймаут после движения (мс)
#define DEFAULT_COLOR_1        0xFFFFFF  // Белый цвет для сегмента 1
#define DEFAULT_COLOR_2        0xFFFFFF  // Белый цвет для сегмента 2

// ========== Библиотеки ==========
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>  // Для парсинга JSON в Arduino IDE

// ========== Структура настроек ==========
struct Settings {
  uint8_t brightness1;   // Яркость при 1 сегменте (0-255)
  uint8_t brightness2;   // Яркость при 2 сегментах (0-255)
  uint16_t motionTimeout; // Таймаут движения (мс)
  uint8_t color1R, color1G, color1B;  // Цвет сегмента 1
  uint8_t color2R, color2G, color2B;  // Цвет сегмента 2
  char wifiSSID[32];     // SSID WiFi
  char wifiPass[64];     // Пароль WiFi
  bool useWiFi;          // Использовать WiFi (true) или AP (false)
};

Settings settings;

// ========== Глобальные переменные ==========
Adafruit_NeoPixel strip(TOTAL_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

// Состояния сенсоров
bool pir1State = false;
bool pir2State = false;
bool lastPir1State = false;
bool lastPir2State = false;

// Время последнего обнаружения движения
unsigned long lastMotionTime1 = 0;
unsigned long lastMotionTime2 = 0;

// Флаг для радуги при первом включении
bool rainbowTestDone = false;

// Состояние WiFi подключения (глобальный флаг)
bool wifiConnected = false;

// Таймер для проверки состояния WiFi
unsigned long lastWifiCheckTimer = 0;
#define WIFI_MONITORING_INTERVAL    30000  // Интервал мониторинга состояния WiFi (мс)

// ========== Прототипы функций ==========
void rainbowTest();
void loadSettings();
void saveSettings();
void handleRoot();
void handleSave();
void handleReset();
void handleNotFound();
void handleWifiStatus();
void startWiFi();
String getHtmlPage();
void onWiFiEvent(WiFiEvent_t event);
bool validateCredentials(const char* ssid, const char* pass);
bool isIPValid(IPAddress ip);


// ========== Эффект радуги (только при первом включении) ==========
void rainbowTest() {
  for (int j = 0; j < 256; j++) {
    for (int i = 0; i < TOTAL_PIXELS; i++) {
      int pixelHue = (i * 256 / TOTAL_PIXELS + j) % 256;
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue * 256)));
    }
    strip.show();
    delay(10);
  }
  strip.clear();
  strip.show();
}
// ========== Валидация загруженных настроек ==========
void validateLoadedSettings() {
  // Проверка границ для яркости
  if (settings.brightness1 > 255) settings.brightness1 = 255;
  if (settings.brightness2 > 255) settings.brightness2 = 255;
  
  // Проверка границ для цветов
  for (int i = 0; i < 3; i++) {
    if (settings.color1R > 255) settings.color1R = 255;
    if (settings.color1G > 255) settings.color1G = 255;
    if (settings.color1B > 255) settings.color1B = 255;
    
    if (settings.color2R > 255) settings.color2R = 255;
    if (settings.color2G > 255) settings.color2G = 255;
    if (settings.color2B > 255) settings.color2B = 255;
  }
  
  // Проверка таймаута (должен быть в разумных пределах)
  if (settings.motionTimeout < 1000) settings.motionTimeout = 1000;  // Минимум 1 секунда
  if (settings.motionTimeout > 300000) settings.motionTimeout = 300000;  // Максимум 5 минут
  
  // Проверка длины SSID и пароля
  size_t ssidLen = strlen(settings.wifiSSID);
  if (ssidLen == 0 || ssidLen > sizeof(settings.wifiSSID) - 1) {
    strncpy(settings.wifiSSID, WIFI_SSID, sizeof(settings.wifiSSID) - 1);
    settings.wifiSSID[sizeof(settings.wifiSSID) - 1] = '\0';
  }
  
  size_t passLen = strlen(settings.wifiPass);
  if (passLen > sizeof(settings.wifiPass) - 1) {
    strncpy(settings.wifiPass, WIFI_PASS, sizeof(settings.wifiPass) - 1);
    settings.wifiPass[sizeof(settings.wifiPass) - 1] = '\0';
  }
}



// ========== Загрузка настроек из EEPROM ==========
void loadSettings() {
  if (!EEPROM.begin(512)) {
    Serial.println("❌ ERROR: Failed to initialize EEPROM");
    return;
  }
  
  // Проверяем, есть ли сохраненные настройки (магическое число 0x55)
  uint8_t magic = EEPROM.read(0);
  
  if (magic == 0x55) {
    // Загружаем настройки из EEPROM с проверкой на ошибки чтения
    uint8_t *ptr = (uint8_t*)&settings;
    bool readError = false;
    
    for (int i = 0; i < sizeof(Settings); i++) {
      uint8_t val = EEPROM.read(1 + i);
      // Проверка на ошибку чтения (если EEPROM вернет мусор)
      if (ptr[i] != val) {  // Это не точно, но проверяем что данные читаются
        readError = true;
        break;
      }
      *ptr++ = val;
    }
    
    if (readError) {
      Serial.println("⚠️  WARNING: EEPROM data corruption detected, resetting settings");
      // Устанавливаем настройки по умолчанию при повреждении данных
      settings.brightness1 = DEFAULT_BRIGHTNESS_1;
      settings.brightness2 = DEFAULT_BRIGHTNESS_2;
      settings.motionTimeout = DEFAULT_TIMEOUT;
      strcpy(settings.wifiSSID, WIFI_SSID);
      strcpy(settings.wifiPass, WIFI_PASS);
      settings.useWiFi = true;
    } else {
      // Восстанавливаем структуру данных
      ptr = (uint8_t*)&settings;
      for (int i = 0; i < sizeof(Settings); i++) {
        *ptr++ = EEPROM.read(1 + i);
      }
      
      // Валидация загруженных настроек
      validateLoadedSettings();
      
      Serial.println("✅ Settings loaded from EEPROM");
    }
  } else {
    // Устанавливаем настройки по умолчанию
    settings.brightness1 = DEFAULT_BRIGHTNESS_1;
    settings.brightness2 = DEFAULT_BRIGHTNESS_2;
    settings.motionTimeout = DEFAULT_TIMEOUT;
    settings.color1R = (DEFAULT_COLOR_1 >> 16) & 0xFF;
    settings.color1G = (DEFAULT_COLOR_1 >> 8) & 0xFF;
    settings.color1B = DEFAULT_COLOR_1 & 0xFF;
    settings.color2R = (DEFAULT_COLOR_2 >> 16) & 0xFF;
    settings.color2G = (DEFAULT_COLOR_2 >> 8) & 0xFF;
    settings.color2B = DEFAULT_COLOR_2 & 0xFF;
    strncpy(settings.wifiSSID, WIFI_SSID, sizeof(settings.wifiSSID) - 1);
    strncpy(settings.wifiPass, WIFI_PASS, sizeof(settings.wifiPass) - 1);
    settings.wifiSSID[sizeof(settings.wifiSSID) - 1] = '\0';
    settings.wifiPass[sizeof(settings.wifiPass) - 1] = '\0';
    settings.useWiFi = true;
    
    // Автоматически сохраняем дефолтные настройки
    Serial.println("ℹ️  No saved settings found, applying defaults");
    saveSettings();
  }
  
  EEPROM.end();
}


// ========== Сохранение настроек в EEPROM ==========
void saveSettings() {
  if (!EEPROM.begin(512)) {
    Serial.println("❌ ERROR: Failed to initialize EEPROM for write");
    return;
  }
  
  // Сохраняем магическое число
  EEPROM.write(0, 0x55);
  
  // Сохраняем настройки
  uint8_t *ptr = (uint8_t*)&settings;
  for (int i = 0; i < sizeof(Settings); i++) {
    EEPROM.write(1 + i, *ptr++);
  }
  
  EEPROM.commit();
  Serial.println("✅ Settings saved to EEPROM");
  EEPROM.end();
}

// ========== Подключение к WiFi (с улучшенной надежностью) ==========
void startWiFi() {
  loadSettings();
  
  // Валидация учетных данных перед подключением
  validateCredentials(settings.wifiSSID, settings.wifiPass);
  
  if (settings.useWiFi && strlen(settings.wifiSSID) > 0) {
    Serial.print("\n📶 Attempting to connect to WiFi: ");
    Serial.println(settings.wifiSSID);
    
    int retryCount = 0;
    uint32_t lastRetryTime = millis();
    
    // Цикл с умными повторными попытками
    while (retryCount < MAX_WIFI_RETRY_ATTEMPTS) {
      // Защита от слишком частых попыток переподключения
      if (millis() - lastRetryTime < WIFI_RECONNECT_DELAY) {
        delay(100);
        continue;
      }
      lastRetryTime = millis();
      
      // Отключаемся перед новой попыткой для очистки состояния
      WiFi.disconnect(true);
      WiFi.mode(WIFI_STA);
      delay(100);
      
      Serial.print(".");
      WiFi.begin(settings.wifiSSID, settings.wifiPass);
      
      // Ждем подключения с учетом таймаута на каждую попытку
      uint32_t startTime = millis();
      while (WiFi.status() != WL_CONNECTED && 
             (millis() - startTime) < WIFI_CONNECT_TIMEOUT && 
             retryCount < MAX_WIFI_RETRY_ATTEMPTS) {
        delay(500);
        retryCount++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi connected successfully!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.print("RSSI: ");
        Serial.println(WiFi.RSSI());
        
        // Проверка, получен ли IP адрес
        if (isIPValid(WiFi.localIP())) {
          wifiConnected = true;
          return;
        } else {
          Serial.println("\n⚠️  Connected but no IP address obtained");
        }
      } else {
        Serial.print(" ❌ Attempt ");
        Serial.print(retryCount);
        Serial.print(" failed. Retrying in ");
        Serial.println(WIFI_RECONNECT_DELAY / 1000);
        
        // Экспоненциальная задержка перед повторной попыткой (небольшой вариант)
        delay(WIFI_RECONNECT_DELAY);
      }
    }
    
    Serial.println("\n❌ Max retry attempts reached, unable to connect");
  }
  
  // Если не получилось подключиться к WiFi - создаем точку доступа
  Serial.println("\n📡 Starting Access Point mode...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  wifiConnected = false;
}

// ========== Валидация учетных данных WiFi ==========
bool validateCredentials(const char* ssid, const char* pass) {
  if (ssid == nullptr || ssid[0] == '\0') {
    Serial.println("❌ ERROR: Empty SSID");
    return false;
  }
  
  size_t len = strlen(ssid);
  if (len > sizeof(settings.wifiSSID) - 1) {
    Serial.print("⚠️  SSID too long (");
    Serial.print(len);
    Serial.print("), max ");
    Serial.println(sizeof(settings.wifiSSID) - 1);
    return false;
  }
  
  if (pass == nullptr) {
    Serial.println("⚠️  WARNING: Empty password, will use default");
    pass = WIFI_PASS;
  }
  
  // Проверяем длину пароля (не более 64 символов)
  size_t passLen = strlen(pass);
  if (passLen > sizeof(settings.wifiPass) - 1) {
    Serial.print("⚠️  Password too long (");
    Serial.print(passLen);
    Serial.println("), truncating...");
    passLen = sizeof(settings.wifiPass) - 1;
  }
  
  return true;
}

// ========== Проверка валидности IP адреса ==========
bool isIPValid(IPAddress ip) {
  if (ip == IPAddress(0, 0, 0, 0)) {
    return false;
  }
  return true;
}

// ========== Обработчик событий WiFi ==========
void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("\n❌ WiFi disconnected");
      wifiConnected = false;
      // Планируем переподключение через короткий интервал
      delay(1000);
      break;
      
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("\n✅ WiFi connected with IP address");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      wifiConnected = true;
      break;
      
    default:
      break;
  }
}

// ========== Обработчик статуса WiFi (HTTP endpoint) ==========
void handleWifiStatus() {
  StaticJsonDocument<256> doc;
  
  doc["connected"] = wifiConnected;
  doc["ssid"] = settings.wifiSSID;
  doc["ip"] = WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  
  if (WiFi.status() == WL_CONNECTED) {
    doc["rssi"] = WiFi.RSSI();
    doc["ap_mode"] = WiFi.softAPIP().toString();
  } else {
    doc["rssi"] = -100;
    doc["ap_mode"] = "";
  }
  
  String json;
  serializeJson(doc, json);
  
  server.send(200, "application/json", json);
}

// ========== HTML страница с формой настройки ==========
String getHtmlPage() {
  String html = "<!DOCTYPE html>";
  html += "<html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>LED Controller</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }";
  html += "h1 { color: #333; text-align: center; }";
  html += ".section { margin: 20px 0; padding: 15px; background: #f9f9f9; border-radius: 5px; }";
  html += "label { display: inline-block; width: 180px; margin-bottom: 5px; font-weight: bold; }";
  html += "input, select { width: 80px; padding: 8px; margin: 5px 0; border: 1px solid #ddd; border-radius: 4px; }";
  html += "button { background: #4CAF50; color: white; padding: 12px 30px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin: 10px 5px; }";
  html += "button:hover { background: #45a049; }";
  html += ".btn-danger { background: #f44336; }";
  html += ".btn-danger:hover { background: #d32f2f; }";
  html += ".status { text-align: center; margin: 20px 0; padding: 10px; background: #e7f3ff; border-radius: 5px; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h1>🔴 LED Controller</h1>";
  html += "<div class='status'>";
  html += "<p><strong>IP:</strong> ";
  html += WiFi.localIP().toString();
  html += " | <strong>MAC:</strong> ";
  html += WiFi.macAddress();
  html += "</p>";
  html += "</div>";
  
  // Секция яркости
  html += "<div class='section'>";
  html += "<h2>⚡ Настройки яркости</h2>";
  html += "<form action='/save' method='post'>";
  html += "<label for='brightness1'>Яркость (1 сегмент):</label>";
  html += "<input type='number' id='brightness1' name='brightness1' value='" + String(settings.brightness1) + "' min='0' max='255'> %\n";
  html += "<label for='brightness2'>Яркость (2 сегмента):</label>";
  html += "<input type='number' id='brightness2' name='brightness2' value='" + String(settings.brightness2) + "' min='0' max='255'> %\n";
  
  // Секция таймаута
  html += "<h2>⏱ Таймаут</h2>";
  html += "<label for='timeout'>Таймаут движения:</label>";
  html += "<input type='number' id='timeout' name='timeout' value='" + String(settings.motionTimeout / 1000) + "' min='1' max='300'> сек\n";
  
  // Секция цветов
  html += "<h2>🎨 Цвета сегментов</h2>";
  html += "<h3>Сегмент 1 (датчик #1)</h3>";
  html += "<label for='color1r'>Красный:</label>";
  html += "<input type='number' id='color1r' name='color1r' value='" + String(settings.color1R) + "' min='0' max='255'>\n";
  html += "<label for='color1g'>Зеленый:</label>";
  html += "<input type='number' id='color1g' name='color1g' value='" + String(settings.color1G) + "' min='0' max='255'>\n";
  html += "<label for='color1b'>Синий:</label>";
  html += "<input type='number' id='color1b' name='color1b' value='" + String(settings.color1B) + "' min='0' max='255'>\n";
  
  html += "<h3>Сегмент 2 (датчик #2)</h3>";
  html += "<label for='color2r'>Красный:</label>";
  html += "<input type='number' id='color2r' name='color2r' value='" + String(settings.color2R) + "' min='0' max='255'>\n";
  html += "<label for='color2g'>Зеленый:</label>";
  html += "<input type='number' id='color2g' name='color2g' value='" + String(settings.color2G) + "' min='0' max='255'>\n";
  html += "<label for='color2b'>Синий:</label>";
  html += "<input type='number' id='color2b' name='color2b' value='" + String(settings.color2B) + "' min='0' max='255'>\n";
  
  // Секция WiFi
  html += "<h2>📶 Настройки WiFi</h2>";
  html += "<label for='ssid'>SSID:</label>";
  html += "<input type='text' id='ssid' name='ssid' value='" + String(settings.wifiSSID) + "' placeholder='Имя сети'>\n";
  html += "<label for='pass'>Пароль:</label>";
  html += "<input type='password' id='pass' name='pass' value='" + String(settings.wifiPass) + "' placeholder='Пароль'>\n";
  
  // Кнопки
  html += "<div style='text-align: center; margin-top: 20px;'>";
  html += "<button type='submit'>💾 Сохранить настройки</button>";
  html += "<button type='button' onclick='location.href=\"/reset\"' class='btn-danger'>🔄 Сбросить настройки</button>";
  html += "</div>";
  html += "</form>";
  html += "</div>";
  
  html += "<div class='section'>";
  html += "<h2>ℹ Информация</h2>";
  html += "<p><strong>Подключения:</strong></p>";
  html += "<ul>";
  html += "<li>WS2812: GPIO 5</li>";
  html += "<li>HC-SR501 #1: GPIO 33 (сегмент 0-29)</li>";
  html += "<li>HC-SR501 #2: GPIO 34 (сегмент 30-59)</li>";
  html += "</ul>";
  html += "</div>";
  
  html += "</div>";
  html += "</body></html>";
  
  return html;
}

// ========== Обработчик главной страницы ==========
void handleRoot() {
  server.send(200, "text/html; charset=utf-8", getHtmlPage());
}

// ========== Обработчик сохранения настроек ==========
void handleSave() {
  // Валидация и обновление параметров яркости
  if (server.hasArg("brightness1")) {
    int val = server.arg("brightness1").toInt();
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    settings.brightness1 = (uint8_t)val;
  } else {
    settings.brightness1 = DEFAULT_BRIGHTNESS_1;
  }
  
  if (server.hasArg("brightness2")) {
    int val = server.arg("brightness2").toInt();
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    settings.brightness2 = (uint8_t)val;
  } else {
    settings.brightness2 = DEFAULT_BRIGHTNESS_2;
  }
  
  // Валидация и обновление таймаута
  if (server.hasArg("timeout")) {
    int val = server.arg("timeout").toInt();
    if (val < 0) val = 1;  // Минимум 1 секунда
    if (val > 300) val = 300;  // Максимум 5 минут
    settings.motionTimeout = (uint16_t)(val * 1000);  // Преобразуем в миллисекунды
  } else {
    settings.motionTimeout = DEFAULT_TIMEOUT;
  }
  
  // Валидация и обновление цветов сегмента 1
  if (server.hasArg("color1r")) {
    int val = server.arg("color1r").toInt();
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    settings.color1R = (uint8_t)val;
  }
  
  if (server.hasArg("color1g")) {
    int val = server.arg("color1g").toInt();
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    settings.color1G = (uint8_t)val;
  }
  
  if (server.hasArg("color1b")) {
    int val = server.arg("color1b").toInt();
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    settings.color1B = (uint8_t)val;
  }
  
  // Валидация и обновление цветов сегмента 2
  if (server.hasArg("color2r")) {
    int val = server.arg("color2r").toInt();
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    settings.color2R = (uint8_t)val;
  }
  
  if (server.hasArg("color2g")) {
    int val = server.arg("color2g").toInt();
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    settings.color2G = (uint8_t)val;
  }
  
  if (server.hasArg("color2b")) {
    int val = server.arg("color2b").toInt();
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    settings.color2B = (uint8_t)val;
  }
  
  // Валидация и обновление настроек WiFi с защитой от buffer overflow
  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");
    // Обрезка если SSID слишком длинный
    if (ssid.length() > sizeof(settings.wifiSSID) - 1) {
      ssid = ssid.substring(0, sizeof(settings.wifiSSID) - 1);
    }
    strncpy(settings.wifiSSID, ssid.c_str(), sizeof(settings.wifiSSID) - 1);
    settings.wifiSSID[sizeof(settings.wifiSSID) - 1] = '\0';
    
    // Проверка на пустой SSID
    if (strlen(settings.wifiSSID) == 0) {
      strncpy(settings.wifiSSID, WIFI_SSID, sizeof(settings.wifiSSID) - 1);
      settings.wifiSSID[sizeof(settings.wifiSSID) - 1] = '\0';
      Serial.println("⚠️  Empty SSID detected, using default");
    }
  } else {
    // Если SSID не был изменен, сохраняем использование WiFi как есть
    if (strlen(settings.wifiSSID) > 0 && strlen(settings.wifiPass) > 0) {
      settings.useWiFi = true;
    } else {
      settings.useWiFi = false;
    }
  }
  
  if (server.hasArg("pass")) {
    String pass = server.arg("pass");
    // Обрезка если пароль слишком длинный
    if (pass.length() > sizeof(settings.wifiPass) - 1) {
      pass = pass.substring(0, sizeof(settings.wifiPass) - 1);
    }
    strncpy(settings.wifiPass, pass.c_str(), sizeof(settings.wifiPass) - 1);
    settings.wifiPass[sizeof(settings.wifiPass) - 1] = '\0';
  } else {
    // Если пароль не был изменен
    if (strlen(settings.wifiSSID) > 0 && strlen(settings.wifiPass) == 0) {
      strncpy(settings.wifiPass, WIFI_PASS, sizeof(settings.wifiPass) - 1);
      settings.wifiPass[sizeof(settings.wifiPass) - 1] = '\0';
    }
  }
  
  // Сохраняем настройки перед переподключением
  saveSettings();
  
  server.send(200, "text/plain", "OK");
}

// ========== Обработчик сброса настроек ==========
void handleReset() {
  // Сбрасываем настройки на дефолтные
  settings.brightness1 = DEFAULT_BRIGHTNESS_1;
  settings.brightness2 = DEFAULT_BRIGHTNESS_2;
  settings.motionTimeout = DEFAULT_TIMEOUT;
  settings.color1R = (DEFAULT_COLOR_1 >> 16) & 0xFF;
  settings.color1G = (DEFAULT_COLOR_1 >> 8) & 0xFF;
  settings.color1B = DEFAULT_COLOR_1 & 0xFF;
  settings.color2R = (DEFAULT_COLOR_2 >> 16) & 0xFF;
  settings.color2G = (DEFAULT_COLOR_2 >> 8) & 0xFF;
  settings.color2B = DEFAULT_COLOR_2 & 0xFF;
  strcpy(settings.wifiSSID, WIFI_SSID);
  strcpy(settings.wifiPass, WIFI_PASS);
  settings.useWiFi = true;
  
  saveSettings();
  
  server.sendHeader("Location", String("/"));
  server.send(302, "text/plain", "Settings reset! Redirecting...");
}

// ========== Обработчик 404 ==========
void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32: WS2812 (60 LEDs) + 2x HC-SR501 + WiFi Config ===");
  
  // Инициализация ленты
  strip.begin();
  strip.show();
  strip.setBrightness(255);
  Serial.println("WS2812 strip initialized (60 pixels)");
  
  // Инициализация пинов сенсоров
  pinMode(PIR_PIN_1, INPUT);
  pinMode(PIR_PIN_2, INPUT);
  Serial.println("HC-SR501 sensors initialized (GPIO 33, 34)");
  
  // Загрузка настроек
  loadSettings();
  
  // Подключение к WiFi
  startWiFi();
  
  // Настройка веб-сервера
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/wifi/status", HTTP_GET, handleWifiStatus);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
  
  // Регистрация обработчиков событий WiFi
  WiFi.onEvent(onWiFiEvent);
  Serial.println("\nWiFi event handlers registered");
  
  // Тест радугой при первом включении
  Serial.println("\n--- Rainbow test on first power-up ---");
  rainbowTest();
  rainbowTestDone = true;
  Serial.println("Initialization complete. Waiting for motion...");
}

// ========= MAIN LOOP =========
void loop() {
  server.handleClient(); // Обработка HTTP запросов
  
  // Мониторинг состояния WiFi (проверка каждые 30 секунд)
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 30000) {
    if (wifiConnected && WiFi.status() != WL_CONNECTED) {
      Serial.println("\n⚠️  WiFi connection lost, re-establishing...");
      wifiConnected = false;
      delay(500);
      startWiFi(); // Переподключение к WiFi
    }
    lastWifiCheck = millis();
  }
  
  // Чтение состояний сенсоров
  pir1State = digitalRead(PIR_PIN_1);
  pir2State = digitalRead(PIR_PIN_2);
  
  // Обработка сенсора #1 (сегмент 0-29)
  if (pir1State != lastPir1State) {
    if (pir1State == HIGH) {
      Serial.println("Sensor 1: Motion DETECTED");
      lastMotionTime1 = millis();
    } else {
      Serial.println("Sensor 1: Motion ENDED");
    }
    lastPir1State = pir1State;
  }
  
  // Обработка сенсора #2 (сегмент 30-59)
  if (pir2State != lastPir2State) {
    if (pir2State == HIGH) {
      Serial.println("Sensor 2: Motion DETECTED");
      lastMotionTime2 = millis();
    } else {
      Serial.println("Sensor 2: Motion ENDED");
    }
    lastPir2State = pir2State;
  }
  
  // Определение активных сегментов
  bool segment1Active = (lastMotionTime1 > 0 && (millis() - lastMotionTime1 <= settings.motionTimeout));
  bool segment2Active = (lastMotionTime2 > 0 && (millis() - lastMotionTime2 <= settings.motionTimeout));
  
  // Настройка яркости
  int activeSegments = segment1Active + segment2Active;
  int brightness = (activeSegments == 2) ? settings.brightness2 : settings.brightness1;
  strip.setBrightness(brightness);
  
  // Цвета сегментов
  uint32_t color1 = segment1Active ? strip.Color(settings.color1R, settings.color1G, settings.color1B) : strip.Color(0, 0, 0);
  uint32_t color2 = segment2Active ? strip.Color(settings.color2R, settings.color2G, settings.color2B) : strip.Color(0, 0, 0);
  
  // Сегмент 1: диоды 0-29
  for (int i = 0; i < SEGMENT_SIZE; i++) {
    strip.setPixelColor(i, color1);
  }
  
  // Сегмент 2: диоды 30-59
  for (int i = SEGMENT_SIZE; i < TOTAL_PIXELS; i++) {
    strip.setPixelColor(i, color2);
  }
  
  strip.show();
  
  // Проверка таймаута для сенсора #1
  if (segment1Active && millis() - lastMotionTime1 > settings.motionTimeout) {
    Serial.println("Sensor 1: Timeout - segment cleared");
    lastMotionTime1 = 0;
  }
  
  // Проверка таймаута для сенсора #2
  if (segment2Active && millis() - lastMotionTime2 > settings.motionTimeout) {
    Serial.println("Sensor 2: Timeout - segment cleared");
    lastMotionTime2 = 0;
  }
  
  delay(50);
}
