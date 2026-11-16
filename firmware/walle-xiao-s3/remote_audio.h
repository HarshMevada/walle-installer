#pragma once

// Audio routing between Chitu and the phone console.
//   phone mic  -> replaces Chitu's own mic (wake word, talking to the AI)
//   Chitu voice -> phone speaker (and optionally muted on the robot)
//   room sound (Chitu's mic) -> phone speaker
// All buffers live in PSRAM. Each buffer has exactly one writer and one reader task.

#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <atomic>
#include <cstdint>

namespace remote_audio {

inline std::atomic<bool> phone_listen{false};   // send Chitu's voice to the phone
inline std::atomic<bool> robot_speaker{true};   // keep the robot's own speaker on too
inline std::atomic<bool> room_listen{false};    // send Chitu's mic (room sound) to the phone
inline std::atomic<int64_t> last_phone_mic_us{0};

struct Ring {
    StreamBufferHandle_t handle = nullptr;
    StaticStreamBuffer_t ctrl;
    bool Init(size_t bytes) {
        if (handle != nullptr) return true;
        auto* storage = static_cast<uint8_t*>(heap_caps_malloc(bytes + 1, MALLOC_CAP_SPIRAM));
        if (storage == nullptr) return false;
        handle = xStreamBufferCreateStatic(bytes, 1, storage, &ctrl);
        return handle != nullptr;
    }
    void Push(const void* data, size_t bytes) {
        if (handle == nullptr) return;
        // Never blocks: when the reader falls behind, the newest audio is dropped
        // (sizes are always even, so samples stay aligned)
        xStreamBufferSend(handle, data, bytes, 0);
    }
    size_t Pop(void* data, size_t max_bytes) {
        if (handle == nullptr) return 0;
        return xStreamBufferReceive(handle, data, max_bytes, 0);
    }
    size_t Available() const { return handle ? xStreamBufferBytesAvailable(handle) : 0; }
    void Clear() {
        if (handle) xStreamBufferReset(handle);
    }
};

inline Ring phone_mic;   // phone -> robot, 16 kHz mono int16
inline Ring voice_out;   // robot output -> phone, output sample rate
inline Ring room_out;    // robot mic -> phone, 16 kHz

inline bool Init() {
    return phone_mic.Init(16000 * 2) && voice_out.Init(24000 * 2) && room_out.Init(16000 * 2);
}

// The phone mic counts as active while packets keep arriving
inline bool PhoneMicActive() {
    return esp_timer_get_time() - last_phone_mic_us.load() < 400 * 1000;
}

inline void ResetRouting() {
    phone_listen = false;
    robot_speaker = true;
    room_listen = false;
    last_phone_mic_us = 0;
    phone_mic.Clear();
    voice_out.Clear();
    room_out.Clear();
}

}  // namespace remote_audio
