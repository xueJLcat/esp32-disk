#pragma once
#include <Arduino.h>

namespace Config {
  // ===== Wi-Fi AP 配置 =====
  static constexpr const char *AP_SSID = "esp32s3";
  static constexpr const char *AP_PASS = "esp32s3"; // >= 8 位

  // 固定 AP 网段
  static constexpr uint8_t IP[4] = {192, 168, 4, 1};
  static constexpr uint8_t GATE[4] = {192, 168, 4, 1};
  static constexpr uint8_t SUBNET[4] = {255, 255, 255, 0};

  // HTTP 端口
  static constexpr uint16_t HTTP_PORT = 80;

  // ===== 上传限制（按需调整） =====
  // 使用二进制 MiB，前后端基于 /info 同步展示，不写死小数
  static constexpr size_t MAX_UPLOAD = 2 * 1024 * 1024; // 2 MiB

  // 文件系统安全余量（避免写爆闪存，建议 32KB+）
  static constexpr size_t FS_SAFETY_MARGIN = 32 * 1024; // 32KB

  // 路径最大长度（避免 LittleFS 过长路径失败）
  static constexpr size_t MAX_PATH_LEN = 96;

  // ===== RAM（PSRAM）临时上传限制 =====
  static constexpr size_t RAM_MAX            = 8 * 1024 * 1024; // 8 MiB 逻辑上限
  static constexpr size_t RAM_SAFETY_MARGIN  = 128 * 1024;       // 128 KiB，避免把 PSRAM 用满
}
