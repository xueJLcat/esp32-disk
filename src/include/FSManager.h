#pragma once
#include <Arduino.h>
#include "FS.h"

class FSManager {
public:
  bool begin(bool formatOnFail = true);

  // 仅在小目录情况下可用的快速 JSON；大目录建议路由里用流式输出
  String listJson();

  bool remove(const String &path);

  bool exists(const String &path);

  File openRead(const String &path);

  File openWrite(const String &path);

  // 取 basename + 过滤非法字符（控制字符、空格→_、仅保留 ASCII 安全 + UTF-8 非 ASCII）
  String sanitizePath(const String &raw);

  String basename(const String &raw); // 同 sanitizePath 语义

  // 去掉 /littlefs 前缀（公开，路由可用）
  String stripMountPrefix(const String &p);
};
