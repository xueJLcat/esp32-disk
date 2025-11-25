#include "include/RamStore.h"
#include <esp_heap_caps.h>
#include <esp32-hal-psram.h>

namespace {
    uint8_t *g_buf = nullptr;
    size_t g_cap = 0;
    size_t g_sz = 0;
    String g_name;
}

namespace RamStore {
    bool available() { return psramFound(); }

    void clear() {
        if (g_buf) {
            heap_caps_free(g_buf);
            g_buf = nullptr;
        }
        g_cap = 0;
        g_sz = 0;
        g_name = String();
    }

    bool start(const String &name, size_t capacity) {
        clear();
        if (!available()) return false;
        if (capacity == 0) return false;
        // 只从 PSRAM 申请
        g_buf = (uint8_t *) heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM);
        if (!g_buf) return false;
        g_cap = capacity;
        g_sz = 0;
        g_name = name;
        return true;
    }

    bool append(const uint8_t *data, size_t len) {
        if (!g_buf || g_sz + len > g_cap) return false;
        memcpy(g_buf + g_sz, data, len);
        g_sz += len;
        return true;
    }

    bool finalize() { return g_buf != nullptr; }

    const uint8_t *data() { return g_buf; }
    size_t size() { return g_sz; }
    size_t capacity() { return g_cap; }
    const String &filename() { return g_name; }
    bool has() { return g_buf && g_sz > 0; }

    size_t freeBytes() {
        return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }

    size_t totalBytes() {
        return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    }
} // namespace RamStore
