#pragma once

#include <chrono>
#include <cstdint>

namespace jpegxs {

class PTPClock {
public:
    // Get current PTP time in nanoseconds
    // Assumes system clock is synchronized to PTP
    static uint64_t now_ns() {
        // Use system_clock for absolute time (PTP is absolute)
        // steady_clock is monotonic but starts at boot.
        // ST 2110 requires PTP (TAI) time.
        // On Windows/Linux, system_clock is usually UTC.
        // TAI = UTC + 37s (approx).
        // We'll just use system clock + offset for now.
        
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
    
    // Get RTP timestamp (90kHz clock)
    static uint32_t get_rtp_timestamp() {
        // RTP timestamp is 32-bit, wraps around.
        // 90000 Hz
        uint64_t ns = now_ns();
        return (uint32_t)((ns * 90) / 1000000);
    }
};

} // namespace jpegxs

