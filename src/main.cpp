#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "include/Config.h"
#include "include/FSManager.h"
#include "include/Routes.h"

AsyncWebServer server(Config::HTTP_PORT);
FSManager fsm;

static void startAP() {
  IPAddress ip(Config::IP[0], Config::IP[1], Config::IP[2], Config::IP[3]);
  IPAddress gw(Config::GATE[0], Config::GATE[1], Config::GATE[2], Config::GATE[3]);
  IPAddress mask(Config::SUBNET[0], Config::SUBNET[1], Config::SUBNET[2], Config::SUBNET[3]);

  WiFiClass::mode(WIFI_AP);
  WiFi.setSleep(false); // 降低异步回调超时概率
  WiFi.softAPConfig(ip, gw, mask);
  WiFi.setTxPower(WIFI_POWER_19_5dBm); // 提升 AP 发射功率（更稳，但耗电略增）
  // 可选：设置 AP 主机名（部分手机不展示）
  // WiFi.softAPsetHostname("esp32-files");

  bool ok = WiFi.softAP(Config::AP_SSID, Config::AP_PASS);
  Serial.printf("AP %s (%s)\n", ok ? "started" : "failed", WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!fsm.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  startAP();
  registerRoutes(server, fsm);

  server.begin(); // 异步服务器启动
  Serial.printf("HTTP server on http://%s\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
  // 异步服务器：不需要 handleClient()
}
