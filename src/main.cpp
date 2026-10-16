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

// ========== Прототипы функций ==========
void rainbowTest();
void loadSettings();
void saveSettings();
void handleRoot();
void handleSave();
void handleReset();
void handleNotFound();
void startWiFi();
String getHtmlPage();

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

// ========== Загрузка настроек из EEPROM ==========
void loadSettings() {
  EEPROM.begin(512);
  
  // Проверяем, есть ли сохраненные настройки (магическое число 0x55)
  uint8_t magic = EEPROM.read(0);
  
  if (magic == 0x55) {
    // Загружаем настройки из EEPROM
    uint8_t *ptr = (uint8_t*)&settings;
    for (int i = 0; i < sizeof(Settings); i++) {
      *ptr++ = EEPROM.read(1 + i);
    }
    Serial.println("Settings loaded from EEPROM");
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
    strcpy(settings.wifiSSID, WIFI_SSID);
    strcpy(settings.wifiPass, WIFI_PASS);
    settings.useWiFi = true;
    
    saveSettings();
    Serial.println("Default settings applied");
  }
  EEPROM.end();
}

// ========== Сохранение настроек в EEPROM ==========
void saveSettings() {
  EEPROM.begin(512);
  
  // Сохраняем магическое число
  EEPROM.write(0, 0x55);
  
  // Сохраняем настройки
  uint8_t *ptr = (uint8_t*)&settings;
  for (int i = 0; i < sizeof(Settings); i++) {
    EEPROM.write(1 + i, *ptr++);
  }
  
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Settings saved to EEPROM");
}

// ========== Подключение к WiFi ==========
void startWiFi() {
  loadSettings();
  
  if (settings.useWiFi && strlen(settings.wifiSSID) > 0) {
    Serial.print("Connecting to WiFi: ");
    Serial.println(settings.wifiSSID);
    
    WiFi.begin(settings.wifiSSID, settings.wifiPass);
    
    // Ждем подключения
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return;
    }
  }
  
  // Если не получилось подключиться к WiFi - создаем точку доступа
  Serial.println("\nFailed to connect to WiFi, starting Access Point...");
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  settings.useWiFi = false;
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
  if (server.hasArg("brightness1")) {
    settings.brightness1 = server.arg("brightness1").toInt();
  }
  if (server.hasArg("brightness2")) {
    settings.brightness2 = server.arg("brightness2").toInt();
  }
  if (server.hasArg("timeout")) {
    settings.motionTimeout = server.arg("timeout").toInt() * 1000; // Преобразуем в миллисекунды
  }
  
  // Цвет сегмента 1
  if (server.hasArg("color1r")) settings.color1R = server.arg("color1r").toInt();
  if (server.hasArg("color1g")) settings.color1G = server.arg("color1g").toInt();
  if (server.hasArg("color1b")) settings.color1B = server.arg("color1b").toInt();
  
  // Цвет сегмента 2
  if (server.hasArg("color2r")) settings.color2R = server.arg("color2r").toInt();
  if (server.hasArg("color2g")) settings.color2G = server.arg("color2g").toInt();
  if (server.hasArg("color2b")) settings.color2B = server.arg("color2b").toInt();
  
  // WiFi настройки
  if (server.hasArg("ssid")) {
    strncpy(settings.wifiSSID, server.arg("ssid").c_str(), sizeof(settings.wifiSSID) - 1);
    settings.wifiSSID[sizeof(settings.wifiSSID) - 1] = '\0';
  }
  if (server.hasArg("pass")) {
    strncpy(settings.wifiPass, server.arg("pass").c_str(), sizeof(settings.wifiPass) - 1);
    settings.wifiPass[sizeof(settings.wifiPass) - 1] = '\0';
  }
  
  // Сохраняем настройки
  saveSettings();
  
  // Переподключаемся к WiFi если изменились настройки
  if (server.hasArg("ssid") || server.hasArg("pass")) {
    settings.useWiFi = true;
    saveSettings();
    Serial.println("WiFi settings changed, reconnecting...");
    WiFi.disconnect();
    delay(1000);
    startWiFi();
  }
  
  server.sendHeader("Location", String("/"));
  server.send(302, "text/plain", "Settings saved! Redirecting...");
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
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
  
  // Тест радугой при первом включении
  Serial.println("\n--- Rainbow test on first power-up ---");
  rainbowTest();
  rainbowTestDone = true;
  Serial.println("Initialization complete. Waiting for motion...");
}

// ========== MAIN LOOP ==========
void loop() {
  server.handleClient(); // Обработка HTTP запросов
  
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
