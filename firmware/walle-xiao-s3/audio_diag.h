#pragma once

// Live audio diagnostics shared by the board (writer), the DIAG log and the web console.
#include <atomic>
#include <mutex>
#include <string>

namespace audio_diag {
inline std::atomic<int> mic_rms{0};       // mic level after MIC_GAIN, last ~1 s
inline std::atomic<int> mic_peak{0};      // highest sample in the last ~1 s
inline std::atomic<int> mic_clipped{0};   // samples at full scale in the last ~1 s
inline std::atomic<int> mic_dc{0};        // raw mic DC offset (before filtering)
inline std::mutex wake_words_mutex;
inline std::string wake_words = "?";      // updated on the main loop

inline std::string WakeWords() {
    std::lock_guard<std::mutex> lock(wake_words_mutex);
    return wake_words;
}
}  // namespace audio_diag
