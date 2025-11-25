#pragma once
#include <Arduino.h>

namespace RamStore {
    bool available();                 // 是否检测到 PSRAM
    void clear();                     // 释放现有缓冲
    bool start(const String& name, size_t capacity); // 分配 capacity 字节的 PSRAM
    bool append(const uint8_t* data, size_t len);    // 追加数据
    bool finalize();                  // 可选（目前无特殊逻辑）
    const uint8_t* data();            // 只读数据指针
    size_t size();                    // 已写入大小
    size_t capacity();                // 当前缓冲容量
    const String& filename();         // 当前缓冲的文件名
    bool has();                       // 是否有数据

    // PSRAM 信息
    size_t freeBytes();               // 剩余 PSRAM
    size_t totalBytes();              // 总 PSRAM
}
