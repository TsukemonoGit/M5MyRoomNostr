#include <M5Unified.h>
#include "M5UnitENV.h"
#include <WiFi.h>
#include <time.h>
#include "config.h" // WIFI_SSID, WIFI_PASSWORD を含むことを想定

// config.hにこれらの定義がない場合は、ここに直接記述してください
// #define WIFI_SSID       "あなたのWiFi名"
// #define WIFI_PASSWORD   "あなたのパスワード"

#define NTP_TIMEZONE    "JST-9"            // 日本時間 +9時間
#define NTP_SERVER1     "pool.ntp.org"     // 主NTPサーバー
#define NTP_SERVER2     "ntp.nict.jp"      // 予備NTPサーバー (情報通信研究機構)
#define NTP_SERVER3     "time.google.com"  // 予備NTPサーバー

// ESP32のSNTP関連ヘッダの有無をチェック
#if __has_include (<esp_sntp.h>)
 #include <esp_sntp.h>
 #define SNTP_ENABLED 1
#elif __has_include (<sntp.h>)
 #include <sntp.h>
 #define SNTP_ENABLED 1
#endif

#ifndef SNTP_ENABLED
#define SNTP_ENABLED 0
#endif


SHT3X sht3x;
QMP6988 qmp;

unsigned long lastNtpSync = 0;
// 60分間隔（ミリ秒）。頻繁な同期が不要であればこのままでOK
const unsigned long ntpSyncInterval = 60 * 60 * 1000;

// NTP同期処理をsetup内とloop内の両方から呼び出せるように関数化
void syncTimeAndSetRTC() {
  // タイムゾーンとNTPサーバーを設定。これによりESP32の内部時刻はJSTになる
  configTzTime(NTP_TIMEZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

  M5.Lcd.println("Waiting for NTP sync..."); // 同期中であることをLCDに表示
  Serial.print("NTP sync status: "); // シリアルにも表示

#if SNTP_ENABLED
  // sntp_get_sync_status() が利用可能ならこちらを使用
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    Serial.print(".");
    M5.Lcd.print("."); // LCDにも進捗表示
    delay(1000); // 1秒ごとにチェック
  }
#else
  // sntp_get_sync_status() が利用できない場合は getLocalTime() で待機
  struct tm timeinfo;
  int retries = 0;
  while (!getLocalTime(&timeinfo, 1000) && retries < 60) { // 最大60秒待機
    Serial.print(".");
    M5.Lcd.print("."); // LCDにも進捗表示
    delay(1000);
    retries++;
  }
#endif

  Serial.println("\nTime synchronized successfully!");
  M5.Lcd.println("\nTime synced!");

  // NTPで同期されたESP32の内部時刻を取得し、M5StackのRTCに設定
  time_t now = time(nullptr);
  // localtime() で現在設定されているタイムゾーン（JST）の時刻を取得
  struct tm* p_timeinfo = localtime(&now); 

  if (p_timeinfo != nullptr) {
    Serial.printf("NTP Server Time (JST): %04d/%02d/%02d %02d:%02d:%02d\n",
                  p_timeinfo->tm_year + 1900,
                  p_timeinfo->tm_mon + 1,
                  p_timeinfo->tm_mday,
                  p_timeinfo->tm_hour,
                  p_timeinfo->tm_min,
                  p_timeinfo->tm_sec);
    
    // M5StackのRTCにJST時刻を設定
    M5.Rtc.setDateTime( { { (uint16_t)(p_timeinfo->tm_year + 1900), (uint8_t)(p_timeinfo->tm_mon + 1), (uint8_t)p_timeinfo->tm_mday },
                          { (uint8_t)p_timeinfo->tm_hour, (uint8_t)p_timeinfo->tm_min, (uint8_t)p_timeinfo->tm_sec } } );
    Serial.println("RTC updated with JST time.");
  } else {
    Serial.println("Failed to convert NTP time to JST.");
  }
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setFont(&fonts::efontCN_24);

  // センサー初期化
  if (!qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 32, 33, 400000U)) {
    Serial.println("Couldn't find QMP6988");
    M5.Lcd.println("QMP6988 Error!");
    while (1) delay(1);
  }
  if (!sht3x.begin(&Wire, SHT3X_I2C_ADDR, 32, 33, 400000U)) {
    Serial.println("Couldn't find SHT3X");
    M5.Lcd.println("SHT3X Error!");
    while (1) delay(1);
  }

  M5.Lcd.println("Sensors Ready!");
  delay(2000);

  // WiFi接続
  M5.Lcd.println("Connecting to WiFi...");
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    M5.Lcd.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    M5.Lcd.println("\nWiFi Connected!");
    // WiFi接続後、NTP同期とRTC設定を行う
    syncTimeAndSetRTC(); 
    lastNtpSync = millis();
  } else {
    Serial.println("\nFailed to connect to WiFi.");
    M5.Lcd.println("\nWiFi Connect Failed!");
    M5.Lcd.println("Cannot sync time. RTC may be inaccurate.");
  }
  delay(3000); // メッセージ表示時間を確保
  M5.Lcd.fillScreen(BLACK);
}

void loop() {
  M5.update();
  M5.Lcd.fillScreen(BLACK);

  // 定期的なNTP同期（WiFi接続済みの場合のみ）
  if (WiFi.status() == WL_CONNECTED && millis() - lastNtpSync > ntpSyncInterval) {
    Serial.println("Re-syncing time with NTP...");
    syncTimeAndSetRTC(); // 再同期
    lastNtpSync = millis();
  }

  // M5StackのRTCから時刻を取得（これはJSTとして設定されているはず）
  auto current_date = M5.Rtc.getDate();
  auto current_time = M5.Rtc.getTime();

  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.setTextSize(1);
  // RTCから取得したJST時刻を直接表示
  M5.Lcd.printf("%04d/%02d/%02d %02d:%02d:%02d (JST)",
                current_date.year, current_date.month, current_date.date,
                current_time.hours, current_time.minutes, current_time.seconds);

  // センサーデータ取得・表示
  float temperature = 0.0;
  float humidity = 0.0;
  float pressure = 0.0;

  if (sht3x.update()) {
    temperature = sht3x.cTemp;
    humidity = sht3x.humidity;
    Serial.printf("SHT3X Temp: %.2f C, Humidity: %.2f %%\n", temperature, humidity);
  }

  if (qmp.update()) {
    pressure = qmp.pressure / 100.0;
    Serial.printf("QMP6988 Temp: %.2f C, Pressure: %.2f hPa, Alt: %.2f m\n",
                  qmp.cTemp, pressure, qmp.altitude);
  }

  M5.Lcd.setCursor(10, 60);
  M5.Lcd.setTextColor(RED);
  M5.Lcd.setTextSize(1);
  M5.Lcd.printf("おんど: %.1f C", temperature);

  M5.Lcd.setCursor(10, 90);
  M5.Lcd.setTextColor(BLUE);
  M5.Lcd.printf("しつど: %.1f %%", humidity);

  M5.Lcd.setCursor(10, 120);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.printf("きあつ: %.1f hPa", pressure);

  delay(1000); // 1秒待機
}