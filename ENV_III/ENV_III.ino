#include <M5Unified.h>
#include "M5UnitENV.h"
#include <WiFi.h>
#include <time.h>
#include "config.h"  // WIFI_SSID, WIFI_PASSWORD,NOSTR_PRIVATE_KEY,NOSTR_PUBLIC_KEY を含むことを想定
#include <NostrEvent.h>
#include <NostrRelayManager.h>

#define NTP_TIMEZONE "JST-9"           // 日本時間 +9時間
#define NTP_SERVER1 "pool.ntp.org"     // 主NTPサーバー
#define NTP_SERVER2 "ntp.nict.jp"      // 予備NTPサーバー (情報通信研究機構)
#define NTP_SERVER3 "time.google.com"  // 予備NTPサーバー

// ESP32のSNTP関連ヘッダの有無をチェック
#if __has_include(<esp_sntp.h>)
#include <esp_sntp.h>
#define SNTP_ENABLED 1
#elif __has_include(<sntp.h>)
#include <sntp.h>
#define SNTP_ENABLED 1
#endif

#ifndef SNTP_ENABLED
#define SNTP_ENABLED 0
#endif

SHT3X sht3x;
QMP6988 qmp;
NostrEvent nostr;
NostrRelayManager nostrRelayManager;
NostrQueueProcessor nostrQueue;

char const* nsecHex = NOSTR_PRIVATE_KEY;
char const* npubHex = NOSTR_PUBLIC_KEY;

// 投稿関連の変数
int lastPostHour = -1;  // 最後に投稿した時間（0-23）
int postCount = 0;      // 投稿回数をカウント

// センサーデータ用グローバル変数
float currentTemperature = 0.0;
float currentHumidity = 0.0;
float currentPressure = 0.0;

// グローバル変数として追加
bool postSuccess = false;
unsigned long postStartTime = 0;
const unsigned long POST_TIMEOUT = 10000;  // 10秒タイムアウト

// okEventを修正
void okEvent(const std::string& key, const char* payload) {
  Serial.println("OK event received");
  Serial.println(payload);
  postSuccess = true;
}


unsigned long lastNtpSync = 0;
// 60分間隔（ミリ秒）。頻繁な同期が不要であればこのままでOK
const unsigned long ntpSyncInterval = 60 * 60 * 1000;

// センサーデータをNostrに投稿する関数
void postSensorDataToNostr(bool isManual = false) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  postSuccess = false;
  postStartTime = millis();

  // 現在時刻の取得（Unix timestamp）
  time_t now = time(nullptr);

  postCount++;

  // 投稿メッセージの作成

  //   message += "固定文字"
  // → 容量不足時だけ再確保
  // "文字列" + String(数値)
  // → 一時的なString生成が発生する可能性が高い
  // message.reserve(200)済み
  // → 200バイト以内なら再確保なし

 String message;
 message.reserve(150);

message += "🌡️ 温度: ";
message += String(currentTemperature, 1);
message += "°C\n";

message += "💧 湿度: ";
message += String(currentHumidity, 1);
message += "%\n";

message += "📊 気圧: ";
message += String(currentPressure, 1);
message += "hPa";

  // // 現在時刻を追加
  // auto current_date = M5.Rtc.getDate();
  // auto current_time = M5.Rtc.getTime();
  // message += "📅 " + String(current_date.year) + "/" +
  //            String(current_date.month) + "/" +
  //            String(current_date.date) + " " +
  //            String(current_time.hours) + ":" +
  //            String(current_time.minutes) + " JST";


  Serial.print("Posting: ");
  Serial.println(message);
  Serial.printf("message bytes: %d\n", message.length()); // この数字＋α を message.reserve(x); に設定する

  Serial.printf("Heap before getNote: %d\n", ESP.getFreeHeap());
  String noteString = nostr.getNote(nsecHex, npubHex, now, message);
  Serial.printf("Heap after getNote: %d\n", ESP.getFreeHeap());
  
  nostrRelayManager.enqueueMessage(noteString.c_str());

  // 投稿完了を待つ
  while (!postSuccess && (millis() - postStartTime < POST_TIMEOUT)) {
    nostrRelayManager.loop();
    nostrRelayManager.broadcastEvents();
    delay(100);
  }

  if (postSuccess) {
    Serial.println("Post successful!");
    if (!isManual) {
      lastPostHour = M5.Rtc.getTime().hours;
    }
  } else {
    Serial.println("Post failed or timed out");
  }

  // 最後の投稿時間を更新（自動投稿の場合のみ）
  if (!isManual) {
    lastPostHour = M5.Rtc.getTime().hours;
  }

  // 投稿中の表示
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 80);
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.setTextSize(2);
  if (isManual) {
    M5.Lcd.println("手動投稿中...");
  } else {
    M5.Lcd.println("定期投稿中...");
  }
  M5.Lcd.setCursor(10, 110);
  M5.Lcd.setTextSize(1);
  M5.Lcd.printf("#%d", postCount);
}

// NTP同期処理をsetup内とloop内の両方から呼び出せるように関数化
void syncTimeAndSetRTC() {
  // タイムゾーンとNTPサーバーを設定。これによりESP32の内部時刻はJSTになる
  configTzTime(NTP_TIMEZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

  M5.Lcd.println("Waiting for NTP sync...");  // 同期中であることをLCDに表示
  Serial.print("NTP sync status: ");          // シリアルにも表示

#if SNTP_ENABLED
  // sntp_get_sync_status() が利用可能ならこちらを使用
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    Serial.print(".");
    M5.Lcd.print(".");  // LCDにも進捗表示
    delay(1000);        // 1秒ごとにチェック
  }
#else
  // sntp_get_sync_status() が利用できない場合は getLocalTime() で待機
  struct tm timeinfo;
  int retries = 0;
  while (!getLocalTime(&timeinfo, 1000) && retries < 60) {  // 最大60秒待機
    Serial.print(".");
    M5.Lcd.print(".");  // LCDにも進捗表示
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
    M5.Rtc.setDateTime({ { (uint16_t)(p_timeinfo->tm_year + 1900), (uint8_t)(p_timeinfo->tm_mon + 1), (uint8_t)p_timeinfo->tm_mday },
                         { (uint8_t)p_timeinfo->tm_hour, (uint8_t)p_timeinfo->tm_min, (uint8_t)p_timeinfo->tm_sec } });
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

  std::vector<String> relays = {
    "x.kojira.io",
    "yabu.me"
  };
  int relayCount = sizeof(relays) / sizeof(relays[0]);

  nostr.setLogging(false);
  nostrRelayManager.setRelays(relays);
  nostrRelayManager.setMinRelaysAndTimeout(1, 10000);  // 最小リレー数を1に変更
  nostrRelayManager.setEventCallback("ok", okEvent);
  nostrRelayManager.connect();

  // 初期接続待機
  delay(2000);
  M5.Lcd.fillScreen(BLACK);

  // 初回投稿時間を設定（現在の時間を記録）
  lastPostHour = M5.Rtc.getTime().hours;
}

void loop() {
  M5.update();
  M5.Lcd.fillScreen(BLACK);
  nostrRelayManager.loop();
  nostrRelayManager.broadcastEvents();

  // 定期的なNTP同期（WiFi接続済みの場合のみ）
  if (WiFi.status() == WL_CONNECTED && millis() - lastNtpSync > ntpSyncInterval) {
    Serial.println("Re-syncing time with NTP...");
    syncTimeAndSetRTC();  // 再同期
    lastNtpSync = millis();
  }

  // M5StackのRTCから時刻を取得（これはJSTとして設定されているはず）
  auto current_date = M5.Rtc.getDate();
  auto current_time = M5.Rtc.getTime();

  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.setTextSize(2);  // フォントサイズを大きく変更
  // RTCから取得したJST時刻を直接表示
  M5.Lcd.printf("%04d/%02d/%02d",
                current_date.year, current_date.month, current_date.date);

  M5.Lcd.setCursor(10, 40);
  M5.Lcd.printf("%02d:%02d:%02d JST",
                current_time.hours, current_time.minutes, current_time.seconds);

  // センサーデータ取得・表示
  if (sht3x.update()) {
    currentTemperature = sht3x.cTemp;
    currentHumidity = sht3x.humidity;
    //  Serial.printf("SHT3X Temp: %.2f C, Humidity: %.2f %%\n", currentTemperature, currentHumidity);
  }

  if (qmp.update()) {
    currentPressure = qmp.pressure / 100.0;
    // Serial.printf("QMP6988 Temp: %.2f C, Pressure: %.2f hPa, Alt: %.2f m\n",
    //               qmp.cTemp, currentPressure, qmp.altitude);
  }

  M5.Lcd.setCursor(10, 80);
  M5.Lcd.setTextColor(RED);
  M5.Lcd.setTextSize(1);
  M5.Lcd.printf("おんど: %.1f C", currentTemperature);

  M5.Lcd.setCursor(10, 110);
  M5.Lcd.setTextColor(BLUE);
  M5.Lcd.printf("しつど: %.1f %%", currentHumidity);

  M5.Lcd.setCursor(10, 140);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.printf("きあつ: %.1f hPa", currentPressure);

  // 次の投稿時刻の表示
  // M5.Lcd.setCursor(10, 170);
  // M5.Lcd.setTextColor(YELLOW);
  // M5.Lcd.setTextSize(1);
  int currentHour = current_time.hours;
  int currentMinute = current_time.minutes;
  // int nextPostHour = (currentHour + 1) % 24;
  // M5.Lcd.printf("次回投稿: %02d:00", nextPostHour);

  // 現在時刻が00分で、前回投稿した時間と異なる場合
  // bool shouldPost = (currentMinute == 0 && currentHour != lastPostHour);
  // if (shouldPost) {
  //   M5.Lcd.setCursor(200, 170);
  //   M5.Lcd.setTextColor(RED);
  //   M5.Lcd.printf("投稿時刻!");
  // }

  // // 投稿回数表示
  // M5.Lcd.setCursor(10, 190);
  // M5.Lcd.setTextColor(WHITE);
  // M5.Lcd.printf("投稿回数: %d回", postCount);

  // ボタン操作説明
  M5.Lcd.setCursor(10, 210);
  M5.Lcd.setTextColor(ORANGE);
  M5.Lcd.printf("ボタンA: しゅどう投稿");

  // ボタンAが押された場合の手動投稿
  if (M5.BtnA.wasPressed()) {
    Serial.println("Button A pressed - Manual post triggered");
    postSensorDataToNostr(true);
  }



  // 現在時刻が00分で、前回投稿した時間と異なる場合に投稿
  if (currentMinute == 0 && currentHour != lastPostHour) {
    Serial.printf("Auto post triggered at %02d:00\n", currentHour);
    postSensorDataToNostr(false);
  }

  delay(1000);  // 1秒待機
}
