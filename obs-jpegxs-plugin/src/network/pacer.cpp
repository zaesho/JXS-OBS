#include "pacer.h"
#include <iostream>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace jpegxs {

Pacer::Pacer() : running_(false), last_packet_end_time_(0) {
}

Pacer::~Pacer() {
    stop();
}

void Pacer::setSender(PacketSender sender) {
    sender_ = sender;
}

void Pacer::start(uint64_t bitrate_bits_per_sec) {
    if (running_) return;
    
    bitrate_bps_ = bitrate_bits_per_sec;
    running_ = true;
    last_packet_end_time_ = 0;
    
#ifdef _WIN32
    // Increase timer resolution on Windows for better sleep precision
    timeBeginPeriod(1);
#endif

    pacer_thread_ = std::thread(&Pacer::pacerLoop, this);
    
    // Set thread priority to High/TimeCritical to avoid preemption during streaming
#ifdef _WIN32
    HANDLE handle = (HANDLE)pacer_thread_.native_handle();
    // TIME_CRITICAL is 15, HIGHEST is 2. Let's try HIGHEST first, TimeCritical might starve system if we spin too much.
    // But since we sleep, TimeCritical is safer for 2110-21.
    SetThreadPriority(handle, THREAD_PRIORITY_TIME_CRITICAL);
#else
    // pthread_setschedparam... (Linux/macOS) - can implement if needed
#endif
}

void Pacer::stop() {
    if (!running_) return;
    
    running_ = false;
    cv_.notify_all();
    
    if (pacer_thread_.joinable()) {
        pacer_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<PacerPacket> empty;
        std::swap(packet_queue_, empty);
        last_packet_end_time_ = 0;
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

void Pacer::enqueueFrame(const std::vector<std::vector<uint8_t>>& packets, uint64_t frame_duration_ns) {
    if (packets.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    
    // Calculate spacing
    // ST 2110-21 Type N (Narrow) Sender
    // Distribute packets evenly over the active video time (or full frame time for simplicity)
    // Ideally, we should send during active lines and pause during blanking, 
    // but simple linear pacing over frame duration is a good start for software.
    
    // Use 90% of frame duration to be safe and ensure we finish before next frame
    uint64_t active_duration = (frame_duration_ns * 90) / 100;
    uint64_t interval_ns = active_duration / packets.size();
    
    uint64_t now = get_time_ns();
    
    // Determine start time
    // If queue was empty or last packet finished long ago, start "now".
    // Otherwise, schedule immediately after the last packet.
    // This prevents "catch up" bursts if we fell behind, but maintains smoothness if we are just slightly ahead/behind.
    
    // Tolerance: If we are more than 1 frame behind, reset to "now" to avoid permanent latency buildup
    if (last_packet_end_time_ == 0 || (now > last_packet_end_time_ + frame_duration_ns)) {
        last_packet_end_time_ = now;
    }
    
    // Schedule packets
    for (size_t i = 0; i < packets.size(); ++i) {
        PacerPacket p;
        p.data = packets[i];
        
        // Packet N is scheduled at Start + (N * Interval)
        // Update last_packet_end_time_ as we go
        p.target_send_time_ns = last_packet_end_time_ + interval_ns;
        
        packet_queue_.push(std::move(p));
        last_packet_end_time_ = p.target_send_time_ns;
    }
    
    cv_.notify_one();
}

void Pacer::pacerLoop() {
    while (running_) {
        PacerPacket packet;
        bool has_packet = false;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !packet_queue_.empty() || !running_; });
            
            if (!running_) break;
            
            // Peek at first packet
            packet = packet_queue_.front();
            
            // Check if it's time to send
            // If not, sleep and don't pop yet
            uint64_t now = get_time_ns();
            if (packet.target_send_time_ns > now) {
                // We need to wait
                uint64_t diff = packet.target_send_time_ns - now;
                lock.unlock(); // Unlock while waiting
                
                // STRICT PACING LOGIC:
                // If wait time > 2ms, use sleep to save CPU.
                // If wait time <= 2ms, use BUSY SPIN.
                // Do NOT yield() in the spin loop, as Yield() is unpredictable (can be >10ms).
                
                if (diff > 2000000) { // 2ms
                    // Sleep for diff - 1.5ms to be safe
                    std::this_thread::sleep_for(std::chrono::nanoseconds(diff - 1500000)); 
                } else {
                    // Busy spin (burning CPU) for microsecond precision
                    while (get_time_ns() < packet.target_send_time_ns) {
                        // No yield, no sleep. Just spin.
                        // _mm_pause() could be used here but standard C++ empty loop is fine
                    }
                }
                continue; // Check again (will pass immediately after spin)
            }
            
            // Time to send (or overdue)
            packet_queue_.pop();
            has_packet = true;
        }
        
        if (has_packet) {
            if (sender_) {
                sender_(packet.data);
            }
        }
    }
}

uint64_t Pacer::get_time_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

} // namespace jpegxs
