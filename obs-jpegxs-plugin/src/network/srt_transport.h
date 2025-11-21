#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

// Forward declare SRT socket type
typedef int SRTSOCKET;

namespace jpegxs {

/**
 * SRT Transport wrapper for low-latency streaming
 */
class SRTTransport {
public:
    enum class Mode {
        CALLER,    // Client mode (connects to server)
        LISTENER   // Server mode (listens for connections)
    };
    
    struct Config {
        Mode mode = Mode::CALLER;
        std::string address = "127.0.0.1";
        uint16_t port = 9000;
        
        // SRT specific settings for low latency
        int32_t latency_ms = 20;           // SRTO_LATENCY
        int32_t recv_buffer_size = 48000000;  // 48MB
        int32_t send_buffer_size = 48000000;  // 48MB
        int64_t max_bandwidth = 1200000000;   // 1.2 Gbps
        bool too_late_packet_drop = true;  // SRTO_TLPKTDROP
        bool nak_report = true;            // SRTO_NAKREPORT
        int32_t peer_latency_ms = 0;       // 0 = same as latency
        
        // Encryption
        std::string passphrase;            // Empty = no encryption
        int32_t key_length = 16;           // 16, 24, or 32 (AES 128/192/256)
        
        // Connection
        int32_t connect_timeout_ms = 3000;
        bool enable_reconnect = true;
    };
    
    struct Stats {
        // Network stats
        int64_t bytes_sent = 0;
        int64_t bytes_received = 0;
        int64_t packets_sent = 0;
        int64_t packets_received = 0;
        int64_t packets_lost = 0;
        int64_t packets_retransmitted = 0;
        
        // Timing stats
        double rtt_ms = 0.0;
        double bandwidth_mbps = 0.0;
        int32_t send_buffer_available = 0;
        int32_t recv_buffer_available = 0;
        
        // Connection state
        bool connected = false;
    };
    
    using DataCallback = std::function<void(const uint8_t* data, size_t size)>;
    using StateCallback = std::function<void(bool connected, const std::string& error)>;
    
    SRTTransport(const Config& config);
    ~SRTTransport();
    
    // Configuration
    bool configure(const Config& config);
    
    // Connection management
    bool start();
    void stop();
    bool isConnected() const;
    
    // Data transmission
    bool send(const uint8_t* data, size_t size);
    
    // Callbacks
    void setDataCallback(DataCallback callback);
    void setStateCallback(StateCallback callback);
    
    // Statistics
    Stats getStats() const;
    void resetStats();
    
    // Error handling
    std::string getLastError() const;
    
private:
    Config config_;
    SRTSOCKET connection_socket_; // Active data connection
    SRTSOCKET listener_socket_;   // For listener mode (server)
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    
    std::unique_ptr<std::thread> recv_thread_;
    std::unique_ptr<std::thread> accept_thread_;
    
    DataCallback data_callback_;
    StateCallback state_callback_;
    
    mutable std::mutex mutex_;
    std::string last_error_;
    Stats stats_;
    
    // Internal methods
    bool initSRT();
    void cleanupSRT();
    bool configureSRTSocket(SRTSOCKET sock);
    bool connectCaller();
    bool startListener();
    void receiveLoop();
    void acceptLoop();
    void updateStats();
    void setError(const std::string& error);
    
    // Disable copy
    SRTTransport(const SRTTransport&) = delete;
    SRTTransport& operator=(const SRTTransport&) = delete;
};

} // namespace jpegxs
