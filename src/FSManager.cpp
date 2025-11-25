#include "include/FSManager.h"
#include <LittleFS.h>
#include <cstring>
#include "include/Config.h"

bool FSManager::begin(bool formatOnFail) {
  // mount 点一般是 /littlefs，但我们对外都用 “/文件名”
  return LittleFS.begin(formatOnFail /* , "/littlefs", 10 */);
}

String FSManager::stripMountPrefix(const String &p) {
  // 有些核心返回的 name() 会带上挂载点 /littlefs，统一去掉
  if (p.startsWith("/littlefs/")) return p.substring(strlen("/littlefs"));
  if (p == "/littlefs") return "/";
  return p;
}

String FSManager::sanitizePath(const String &raw) {
  String p = raw;
  p.replace("\\", "/");

  // 去路径，仅保留文件名
  int idx = p.lastIndexOf('/');
  if (idx >= 0) p = p.substring(idx + 1);

  // 去掉盘符 C: 之类
  int colon = p.indexOf(':');
  if (colon >= 0) p = p.substring(colon + 1);

  p.trim();

  String out;
  out.reserve(p.length() + 1);
  for (size_t i = 0; i < p.length(); ++i) {
    unsigned char c = (unsigned char) p[i];

    // 过滤控制字符
    if (c < 0x20 || c == 0x7F) continue;

    if ((c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        c == '.' || c == '_' || c == '-') {
      out += (char) c;
    } else if (c == ' ') {
      out += '_';
    } else if (c & 0x80) {
      // 非 ASCII 字节（UTF-8）
      out += (char) c;
    }
  }

  if (out.isEmpty()) out = "unnamed";

  // 限长
  if (out.length() > Config::MAX_PATH_LEN)
    out = out.substring(0, Config::MAX_PATH_LEN);

  if (!out.startsWith("/")) out = "/" + out;
  return out;
}

String FSManager::basename(const String &raw) { return sanitizePath(raw); }

String FSManager::listJson() {
  // 适合小目录；大目录请用路由里的流式输出
  String json = "[";
  bool first = true;
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    if (!first) json += ",";
    String name = stripMountPrefix(String(file.name()));
    json += "{\"name\":\"";
    json += name;
    json += "\",\"size\":";
    json += String((uint32_t) file.size());
    json += "}";
    first = false;
    file.close();
    file = root.openNextFile();
  }
  json += "]";
  return json;
}

bool FSManager::remove(const String &path) {
  return LittleFS.remove(sanitizePath(path));
}

bool FSManager::exists(const String &path) {
  return LittleFS.exists(sanitizePath(path));
}

File FSManager::openRead(const String &path) {
  return LittleFS.open(sanitizePath(path), FILE_READ);
}

File FSManager::openWrite(const String &path) {
  return LittleFS.open(sanitizePath(path), FILE_WRITE);
}
