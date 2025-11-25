// Routes.cpp —— ESPAsyncWebServer 版（优化版）
#include "include/Routes.h"
#include "include/HtmlIndex.h"
#include "Config.h"
#include "include/FSManager.h"
#include <LittleFS.h>
#include "include/RamStore.h"     // ← 新增
// FreeRTOS 轻量临界区（ESP32）
#include "freertos/FreeRTOS.h"
#include "include/Config.h"

// ===== MIME 判断：补充 webp/webm/ogv/ogg/wav/m4a 等 =====
static String contentTypeFromName(const String &name) {
  String n = name;
  n.toLowerCase();

  if (n.endsWith(".html") || n.endsWith(".htm")) return "text/html";
  if (n.endsWith(".txt") || n.endsWith(".log")) return "text/plain";
  if (n.endsWith(".json")) return "application/json";

  if (n.endsWith(".jpg") || n.endsWith(".jpeg")) return "image/jpeg";
  if (n.endsWith(".png")) return "image/png";
  if (n.endsWith(".gif")) return "image/gif";
  if (n.endsWith(".svg")) return "image/svg+xml";
  if (n.endsWith(".webp")) return "image/webp";
  if (n.endsWith(".bmp")) return "image/bmp";

  if (n.endsWith(".mp4")) return "video/mp4";
  if (n.endsWith(".webm")) return "video/webm";
  if (n.endsWith(".ogv")) return "video/ogg";

  if (n.endsWith(".mp3")) return "audio/mpeg";
  if (n.endsWith(".m4a")) return "audio/mp4";
  if (n.endsWith(".wav")) return "audio/wav";
  if (n.endsWith(".ogg")) return "audio/ogg";

  if (n.endsWith(".css")) return "text/css";
  if (n.endsWith(".js")) return "application/javascript";
  if (n.endsWith(".pdf")) return "application/pdf";
  if (n.endsWith(".ico")) return "image/x-icon";

  return "application/octet-stream";
}

// ===== 全局上传互斥：仅允许一个上传 =====
static volatile bool g_uploadBusy = false; // 是否有正在进行的上传
static AsyncWebServerRequest *g_activeReq = nullptr; // 当前持锁的请求
static File uploadFile;
static bool uploadOpenOK = false;
static bool uploadRejected = false;
static size_t written = 0;
static size_t hardCap = 0; // 本次上传允许的最大总字节（min(MAX_UPLOAD, free - safety)）
static String currentName;

// 轻量临界区
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

// 复位当前上传上下文（仅在该请求完成发送响应后调用）
static void resetActiveUpload() {
  if (uploadFile) uploadFile.close();

  taskENTER_CRITICAL(&g_mux);
  g_activeReq = nullptr;
  g_uploadBusy = false;
  taskEXIT_CRITICAL(&g_mux);

  uploadOpenOK = false;
  uploadRejected = false;
  written = 0;
  hardCap = 0;
  currentName = "";
}

// =================== 新增：RAM 信息 ===================
static void handleRamInfo(AsyncWebServerRequest *req) {
  size_t total = RamStore::totalBytes();
  size_t freeB = RamStore::freeBytes();
  size_t used = RamStore::has() ? RamStore::size() : 0;

  size_t allowance = 0;
  if (RamStore::available() && freeB > Config::RAM_SAFETY_MARGIN) {
    allowance = freeB - Config::RAM_SAFETY_MARGIN;
    if (allowance > Config::RAM_MAX) allowance = Config::RAM_MAX;
  }

  String json = "{";
  json += "\"psram\":";
  json += (RamStore::available() ? "true" : "false");
  json += ",";
  json += "\"total\":" + String((uint32_t) total) + ",";
  json += "\"free\":" + String((uint32_t) freeB) + ",";
  json += "\"used\":" + String((uint32_t) used) + ",";
  json += "\"has\":" + String(RamStore::has() ? "true" : "false") + ",";
  json += "\"name\":\"" + String(RamStore::has() ? RamStore::filename() : "") + "\",";
  json += "\"maxUpload\":" + String((uint32_t) allowance) + ",";
  json += "\"safetyMargin\":" + String((uint32_t) Config::RAM_SAFETY_MARGIN);
  json += "}";

  auto *res = req->beginResponse(200, "application/json", json);
  res->addHeader("Cache-Control", "no-store");
  res->addHeader("X-Content-Type-Options", "nosniff");
  req->send(res);
}

// ====== 解析 Range 头（单段）======
static bool parseRangeHeader(const String &h, size_t total, size_t &start, size_t &end) {
  if (!h.startsWith("bytes=")) return false;
  String r = h.substring(6);
  int sep = r.indexOf('-');
  if (sep < 0) return false;

  String a = r.substring(0, sep);
  String b = r.substring(sep + 1);

  if (a.length() == 0) {
    // bytes=-N
    size_t suffix = (size_t) b.toInt();
    if (suffix == 0) return false;
    if (suffix > total) suffix = total;
    start = total - suffix;
    end = total - 1;
    return true;
  }

  size_t s = (size_t) a.toInt();
  if (s >= total) return false;

  if (b.length() == 0) {
    // bytes=S-
    start = s;
    end = total - 1;
    return true;
  }

  size_t e = (size_t) b.toInt(); // bytes=S-E
  if (e >= total) e = total - 1;
  if (s > e) return false;

  start = s;
  end = e;
  return true;
}

// =================== RAM 下载 / 预览：支持 Range ===================
static void handleRamDownload(AsyncWebServerRequest *req, bool inlineView) {
  if (!RamStore::has()) {
    req->send(404, "text/plain", "RAM buffer empty");
    return;
  }

  const uint8_t *buf = (const uint8_t *) RamStore::data();
  size_t total = RamStore::size();
  String fname = RamStore::filename();
  String mime = fname.length() ? contentTypeFromName("/" + fname) : "application/octet-stream";

  String rangeVal;
  if (req->hasHeader("Range")) {
    rangeVal = req->header("Range"); // ← 直接拿到字符串
  }

  if (rangeVal.length()) {
    size_t rs = 0, re = 0;
    if (!parseRangeHeader(rangeVal, total, rs, re)) {
      AsyncWebServerResponse *res = req->beginResponse(416, "text/plain", "Range Not Satisfiable");
      res->addHeader("Content-Range", "bytes */" + String((unsigned long) total));
      res->addHeader("Accept-Ranges", "bytes");
      res->addHeader("X-Content-Type-Options", "nosniff");
      req->send(res);
      return;
    }

    const size_t partLen = re - rs + 1;
    AsyncWebServerResponse *res = req->beginResponse(206, mime, buf + rs, partLen);
    res->addHeader("Content-Range",
                   "bytes " + String((unsigned long) rs) + "-" + String((unsigned long) re) +
                   "/" + String((unsigned long) total));
    res->addHeader("Accept-Ranges", "bytes");
    res->addHeader("Cache-Control", "public, max-age=600");
    res->addHeader("X-Content-Type-Options", "nosniff");
    if (!inlineView) {
      res->addHeader("Content-Disposition",
                     "attachment; filename=\"" + (fname.length() ? fname : "ram.bin") + "\"");
    }
    req->send(res);
    return;
  }

  // 无 Range：200 全量，但声明可接受 Range
  AsyncWebServerResponse *res = req->beginResponse(200, mime, buf, total);
  res->addHeader("Accept-Ranges", "bytes");
  res->addHeader("Cache-Control", "public, max-age=600");
  res->addHeader("X-Content-Type-Options", "nosniff");
  if (!inlineView) {
    res->addHeader("Content-Disposition",
                   "attachment; filename=\"" + (fname.length() ? fname : "ram.bin") + "\"");
  }
  req->send(res);
}

// =================== 新增：RAM 上传（/upload-ram） ===================
static bool ram_uploadOpenOK = false; // RAM 上传状态
static bool ram_uploadRejected = false;
static size_t ram_written = 0;
static size_t ram_hardCap = 0;

static void resetRamUploadState() {
  ram_uploadOpenOK = false;
  ram_uploadRejected = false;
  ram_written = 0;
  ram_hardCap = 0;
}

void registerRoutes(AsyncWebServer &server, FSManager &fs) {
  // 首页（从 PROGMEM 流式发送，避免整页拷到堆）
  // Routes.cpp 中首页处理器
  // Routes.cpp 中：首页处理器
  server.on("/", HTTP_GET, [&](AsyncWebServerRequest *req) {
    const size_t htmlLen = sizeof(INDEX_HTML) - 1; // 排除结尾 '\0'
    AsyncWebServerResponse *res =
        req->beginResponse(
          200,
          "text/html",
          reinterpret_cast<const uint8_t *>(INDEX_HTML), // 从 PROGMEM 直接发送
          htmlLen
        );
    res->addHeader("Cache-Control", "no-store");
    res->addHeader("X-Content-Type-Options", "nosniff");
    res->addHeader("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'");
    req->send(res);
  });

  // 容量信息（用于前端容量条/颜色/预检查）
  server.on("/info", HTTP_GET, [&](AsyncWebServerRequest *req) {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    size_t freeB = (total > used) ? (total - used) : 0;

    String json = "{";
    json += "\"total\":" + String((uint32_t) total) + ",";
    json += "\"used\":" + String((uint32_t) used) + ",";
    json += "\"free\":" + String((uint32_t) freeB) + ",";
    json += "\"maxUpload\":" + String((uint32_t) Config::MAX_UPLOAD) + ",";
    json += "\"safetyMargin\":" + String((uint32_t) Config::FS_SAFETY_MARGIN);
    json += "}";

    auto *res = req->beginResponse(200, "application/json", json);
    res->addHeader("Cache-Control", "no-store");
    res->addHeader("X-Content-Type-Options", "nosniff");
    req->send(res);
  });

  // 列表（流式输出，避免大 JSON 拼接碎片）
  server.on("/list", HTTP_GET, [&](AsyncWebServerRequest *req) {
    auto *res = req->beginResponseStream("application/json");
    res->addHeader("Cache-Control", "no-store");
    res->addHeader("X-Content-Type-Options", "nosniff");
    res->print('[');
    bool first = true;
    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      if (!first) res->print(',');
      first = false;
      String name = fs.stripMountPrefix(String(f.name()));
      res->printf("{\"name\":\"%s\",\"size\":%u}", name.c_str(), (unsigned) f.size());
      f.close();
    }
    res->print(']');
    req->send(res);
  });

  // 删除
  server.on("/delete", HTTP_POST, [&](AsyncWebServerRequest *req) {
    if (!req->hasParam("name", true)) {
      // true=body
      req->send(400, "text/plain", "Missing 'name'");
      return;
    }
    String name = req->getParam("name", true)->value();
    bool ok = fs.remove(name);
    AsyncWebServerResponse *res = req->beginResponse(ok ? 200 : 500, "text/plain", ok ? "Deleted" : "Delete failed");
    res->addHeader("X-Content-Type-Options", "nosniff");
    req->send(res);
  });

  // 下载（支持 Range，加 nosniff/缓存头）
  server.on("/download", HTTP_GET, [&](AsyncWebServerRequest *req) {
    if (!req->hasParam("name")) {
      req->send(400, "text/plain", "Missing 'name'");
      return;
    }
    String path = fs.sanitizePath(req->getParam("name")->value());
    if (!fs.exists(path)) {
      req->send(404, "text/plain", "Not Found");
      return;
    }
    const String mime = contentTypeFromName(path);
    AsyncWebServerResponse *res = req->beginResponse(LittleFS, path, mime, true);
    String fname = path.length() > 1 ? path.substring(1) : "download.bin";
    res->addHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
    res->addHeader("Accept-Ranges", "bytes");
    res->addHeader("Cache-Control", "public, max-age=600");
    res->addHeader("X-Content-Type-Options", "nosniff");
    req->send(res);
  });

  // 预览（inline 不强制下载，浏览器可直接展示；支持 Range）
  server.on("/view", HTTP_GET, [&](AsyncWebServerRequest *req) {
    if (!req->hasParam("name")) {
      req->send(400, "text/plain", "Missing 'name'");
      return;
    }
    String path = fs.sanitizePath(req->getParam("name")->value());
    if (!fs.exists(path)) {
      req->send(404, "text/plain", "Not Found");
      return;
    }
    const String mime = contentTypeFromName(path);
    // 注意：这里不要传下载标志、也不要加 Content-Disposition
    AsyncWebServerResponse *res = req->beginResponse(LittleFS, path, mime);
    res->addHeader("Accept-Ranges", "bytes"); // 支持视频拖动
    res->addHeader("Cache-Control", "public, max-age=600");
    res->addHeader("X-Content-Type-Options", "nosniff");
    req->send(res);
  });

  // ===== 上传（异步；单并发 + 一次性计算硬上限） =====
  server.on(
    "/upload",
    HTTP_POST,

    // onRequest：当上传传输完成（或我们未处理数据）时调用，用于统一返回
    [&](AsyncWebServerRequest *req) {
      if (req == g_activeReq) {
        // 活跃请求：根据状态返回 200/413/500
        int code = uploadOpenOK && !uploadRejected ? 200 : (uploadRejected ? 413 : 500);
        const char *msg =
            (code == 200) ? "OK" : (code == 413) ? "Payload Too Large or No Space" : "Upload failed";

        AsyncWebServerResponse *res = req->beginResponse(code, "text/plain", msg);
        res->addHeader("X-Content-Type-Options", "nosniff");
        req->send(res);

        // 若失败，清理半截文件
        if (code != 200 && currentName.length() && LittleFS.exists(currentName)) {
          LittleFS.remove(currentName);
        }
        resetActiveUpload();
      } else {
        // 非活跃（并发）请求：我们未接受它的数据，直接告知被锁
        req->send(423, "text/plain", "Another upload is in progress");
      }
    },

    // onUpload：分片数据回调
    [&](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
      // 若已有活动上传且不是同一个请求，直接丢弃并发数据（节省资源）
      if (g_uploadBusy && req != g_activeReq) return;

      if (index == 0) {
        // 新上传开始：尝试获取互斥锁
        taskENTER_CRITICAL(&g_mux);
        if (g_uploadBusy && req != g_activeReq) {
          taskEXIT_CRITICAL(&g_mux);
          return;
        }
        g_uploadBusy = true;
        g_activeReq = req;
        taskEXIT_CRITICAL(&g_mux);

        uploadOpenOK = false;
        uploadRejected = false;
        written = 0;
        hardCap = 0;

        currentName = fs.basename(filename);
        Serial.printf("[UPLOAD START] %s (raw: %s)\n", currentName.c_str(), filename.c_str());

        // 同名先删
        if (fs.exists(currentName)) fs.remove(currentName);

        // 计算本次可用“硬上限”：min(MAX_UPLOAD, free - safety)
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        size_t freeB = (total > used) ? (total - used) : 0;
        if (freeB <= Config::FS_SAFETY_MARGIN) {
          Serial.println("Upload rejected: no free space");
          uploadRejected = true;
          return;
        }
        size_t allowance = freeB - Config::FS_SAFETY_MARGIN;
        hardCap = (allowance < Config::MAX_UPLOAD) ? allowance : Config::MAX_UPLOAD;
        if (hardCap == 0) {
          uploadRejected = true;
          return;
        }

        uploadFile = fs.openWrite(currentName);
        if (!uploadFile) {
          Serial.printf("OPEN WRITE FAIL: %s\n", currentName.c_str());
        } else {
          uploadOpenOK = true;
        }
      }

      // 若已拒绝或未成功打开文件，忽略后续数据
      if (!uploadOpenOK || uploadRejected) {
        if (final && req == g_activeReq) {
          if (uploadFile) uploadFile.close();
        }
        return;
      }

      // 统一用硬上限判断
      size_t nextWritten = written + len;
      if (nextWritten > hardCap) {
        Serial.printf("Upload rejected: exceed hardCap (%u > %u)\n",
                      (unsigned) nextWritten, (unsigned) hardCap);
        uploadRejected = true;
        if (uploadFile) uploadFile.close();
        if (currentName.length() && LittleFS.exists(currentName)) LittleFS.remove(currentName);
        return;
      }

      // 真正写入
      size_t w = uploadFile.write(data, len);
      if (w != len) {
        Serial.printf("WRITE FAIL: wrote %u / %u\n", (unsigned) w, (unsigned) len);
        uploadRejected = true;
        uploadFile.close();
        if (currentName.length() && LittleFS.exists(currentName)) LittleFS.remove(currentName);
        return;
      }
      written = nextWritten;

      if (final) {
        if (uploadFile) uploadFile.close();
        Serial.printf("[UPLOAD END] %s - %u bytes\n", currentName.c_str(), (unsigned) written);
        // 注意：不在此处 reset，由 onRequest 统一返回 + reset
      }
    }
  );

  // ========== RAM 相关路由 ==========
  server.on("/ram/info", HTTP_GET, [&](AsyncWebServerRequest *req) {
    handleRamInfo(req);
  });

  server.on("/ram/clear", HTTP_POST, [&](AsyncWebServerRequest *req) {
    RamStore::clear();
    req->send(200, "text/plain", "Cleared");
  });

  server.on("/ram/download", HTTP_GET, [&](AsyncWebServerRequest *req) {
    handleRamDownload(req, /*inlineView*/ false);
  });

  server.on("/ram/view", HTTP_GET, [&](AsyncWebServerRequest *req) {
    handleRamDownload(req, /*inlineView*/ true);
  });

  // === RAM 上传：单并发 + 一次计算硬上限 ===
  server.on(
    "/upload-ram",
    HTTP_POST,

    // onRequest（上传完成时统一返回）
    [&](AsyncWebServerRequest *req) {
      if (req == g_activeReq) {
        int code = ram_uploadOpenOK && !ram_uploadRejected ? 200 : (ram_uploadRejected ? 413 : 500);
        const char *msg = (code == 200) ? "OK" : (code == 413) ? "Payload Too Large or No Space" : "Upload failed";
        req->send(code, "text/plain", msg);
        if (code != 200) RamStore::clear();
        resetRamUploadState();
        resetActiveUpload();
      } else {
        req->send(423, "text/plain", "Another upload is in progress");
      }
    },

    // onUpload（分片写入到 PSRAM）
    [&](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (g_uploadBusy && req != g_activeReq) return;

      if (index == 0) {
        // 抢占全局上传锁
        taskENTER_CRITICAL(&g_mux);
        if (g_uploadBusy && req != g_activeReq) {
          taskEXIT_CRITICAL(&g_mux);
          return;
        }
        g_uploadBusy = true;
        g_activeReq = req;
        taskEXIT_CRITICAL(&g_mux);

        ram_uploadOpenOK = false;
        ram_uploadRejected = false;
        ram_written = 0;
        ram_hardCap = 0;

        currentName = fs.basename(filename); // 复用原 basename 过滤
        Serial.printf("[RAM UPLOAD START] %s (raw: %s)\n", currentName.c_str(), filename.c_str());

        // 计算 RAM 硬上限
        if (!RamStore::available()) {
          ram_uploadRejected = true;
          return;
        }
        size_t freeB = RamStore::freeBytes();
        if (freeB <= Config::RAM_SAFETY_MARGIN) {
          ram_uploadRejected = true;
          return;
        }
        size_t allowance = freeB - Config::RAM_SAFETY_MARGIN;
        ram_hardCap = (allowance < Config::RAM_MAX) ? allowance : Config::RAM_MAX;
        if (ram_hardCap == 0) {
          ram_uploadRejected = true;
          return;
        }

        // 按硬上限分配 PSRAM 缓冲
        if (!RamStore::start(currentName, ram_hardCap)) {
          ram_uploadRejected = true;
          return;
        }
        ram_uploadOpenOK = true;
      }

      if (!ram_uploadOpenOK || ram_uploadRejected) return;

      size_t next = ram_written + len;
      if (next > ram_hardCap) {
        ram_uploadRejected = true;
        RamStore::clear();
        return;
      }

      if (!RamStore::append(data, len)) {
        ram_uploadRejected = true;
        RamStore::clear();
        return;
      }
      ram_written = next;

      if (final) {
        RamStore::finalize();
        Serial.printf("[RAM UPLOAD END] %s - %u bytes\n", currentName.c_str(), (unsigned) ram_written);
        // 统一在 onRequest 中返回 + reset
      }
    }
  );

  // 健康检查
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *res = req->beginResponse(200, "text/plain", "pong");
    res->addHeader("X-Content-Type-Options", "nosniff");
    req->send(res);
  });

  // 兜底 404
  server.onNotFound([](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *res = req->beginResponse(404, "text/plain", "Not Found");
    res->addHeader("X-Content-Type-Options", "nosniff");
    req->send(res);
  });
}
