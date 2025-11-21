#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <functional>

namespace jpegxs {

// Interface for the sender callback
using PacketSender = std::function<bool(const std::vector<uint8_t>&)>;

struct PacerPacket {
    std::vector<uint8_t> data;
    uint64_t target_send_time_ns; // 0 if immediate/calculated automatically
};

class Pacer {
public:
    Pacer();
    ~Pacer();

    void setSender(PacketSender sender);
    
    // Start the pacing thread
    // bitrate_bits_per_sec: Target bitrate for pacing calculations
    void start(uint64_t bitrate_bits_per_sec);
    
    void stop();

    // Enqueue packets for sending
    // packets: vector of serialized RTP packets
    // frame_duration_ns: total duration of this frame (e.g., 16666666 for 60fps)
    void enqueueFrame(const std::vector<std::vector<uint8_t>>& packets, uint64_t frame_duration_ns);

private:
    void pacerLoop();

    PacketSender sender_;
    std::thread pacer_thread_;
    std::atomic<bool> running_;
    
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<PacerPacket> packet_queue_;
    
    uint64_t bitrate_bps_ = 0;
    uint64_t last_packet_end_time_ = 0;
    
    // High-precision clock helper
    static uint64_t get_time_ns();
};

} // namespace jpegxs
