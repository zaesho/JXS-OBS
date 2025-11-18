#include "srt_transport.h"
#include <srt/srt.h>
#include <cstring>
#include <chrono>

namespace jpegxs {

constexpr size_t SRT_BUFFER_SIZE = 1500;  // MTU-safe size

SRTTransport::SRTTransport(const Config& cfg)
    : config_(cfg)
    , socket_(SRT_INVALID_SOCK)
    , accept_socket_(SRT_INVALID_SOCK)
    , running_(false)
    , connected_(false) {
}

SRTTransport::~SRTTransport() {
    stop();
    cleanupSRT();
}

bool SRTTransport::configure(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (running_) {
        setError("Cannot configure while running");
        return false;
    }
    
    config_ = config;
    return true;
}

bool SRTTransport::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (running_) {
        return true;  // Already running
    }
    
    if (!initSRT()) {
        return false;
    }
    
    running_ = true;
    
    if (config_.mode == Mode::CALLER) {
        if (!connectCaller()) {
            running_ = false;
            cleanupSRT();
            return false;
        }
        
        // Start receive thread
        recv_thread_ = std::make_unique<std::thread>(&SRTTransport::receiveLoop, this);
    } else {
        if (!startListener()) {
            running_ = false;
            cleanupSRT();
            return false;
        }
        
        // Start accept thread
        accept_thread_ = std::make_unique<std::thread>(&SRTTransport::acceptLoop, this);
    }
    
    return true;
}

void SRTTransport::stop() {
    running_ = false;
    connected_ = false;
    
    if (recv_thread_ && recv_thread_->joinable()) {
        recv_thread_->join();
    }
    
    if (accept_thread_ && accept_thread_->joinable()) {
        accept_thread_->join();
    }
    
    recv_thread_.reset();
    accept_thread_.reset();
    
    cleanupSRT();
}

bool SRTTransport::isConnected() const {
    return connected_;
}

bool SRTTransport::send(const uint8_t* data, size_t size) {
    if (!connected_ || socket_ == SRT_INVALID_SOCK) {
        return false;
    }
    
    int sent = srt_send(socket_, reinterpret_cast<const char*>(data), static_cast<int>(size));
    
    if (sent == SRT_ERROR) {
        setError(std::string("Send failed: ") + srt_getlasterror_str());
        return false;
    }
    
    stats_.bytes_sent += sent;
    stats_.packets_sent++;
    
    return sent == static_cast<int>(size);
}

void SRTTransport::setDataCallback(DataCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_callback_ = callback;
}

void SRTTransport::setStateCallback(StateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_callback_ = callback;
}

SRTTransport::Stats SRTTransport::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void SRTTransport::resetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = Stats();
    stats_.connected = connected_;
}

std::string SRTTransport::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

bool SRTTransport::initSRT() {
    static bool srt_initialized = false;
    
    if (!srt_initialized) {
        if (srt_startup() != 0) {
            setError("Failed to initialize SRT library");
            return false;
        }
        srt_initialized = true;
    }
    
    socket_ = srt_create_socket();
    if (socket_ == SRT_INVALID_SOCK) {
        setError("Failed to create SRT socket");
        return false;
    }
    
    if (!configureSRTSocket(socket_)) {
        srt_close(socket_);
        socket_ = SRT_INVALID_SOCK;
        return false;
    }
    
    return true;
}

void SRTTransport::cleanupSRT() {
    if (accept_socket_ != SRT_INVALID_SOCK) {
        srt_close(accept_socket_);
        accept_socket_ = SRT_INVALID_SOCK;
    }
    
    if (socket_ != SRT_INVALID_SOCK) {
        srt_close(socket_);
        socket_ = SRT_INVALID_SOCK;
    }
}

bool SRTTransport::configureSRTSocket(SRTSOCKET sock) {
    // Set latency
    if (srt_setsockopt(sock, 0, SRTO_LATENCY, &config_.latency_ms, sizeof(config_.latency_ms)) != 0) {
        setError("Failed to set SRTO_LATENCY");
        return false;
    }
    
    // Set receive buffer
    if (srt_setsockopt(sock, 0, SRTO_RCVBUF, &config_.recv_buffer_size, sizeof(config_.recv_buffer_size)) != 0) {
        setError("Failed to set SRTO_RCVBUF");
        return false;
    }
    
    // Set send buffer
    if (srt_setsockopt(sock, 0, SRTO_SNDBUF, &config_.send_buffer_size, sizeof(config_.send_buffer_size)) != 0) {
        setError("Failed to set SRTO_SNDBUF");
        return false;
    }
    
    // Set max bandwidth
    if (srt_setsockopt(sock, 0, SRTO_MAXBW, &config_.max_bandwidth, sizeof(config_.max_bandwidth)) != 0) {
        setError("Failed to set SRTO_MAXBW");
        return false;
    }
    
    // Set too-late packet drop
    int tlpktdrop = config_.too_late_packet_drop ? 1 : 0;
    if (srt_setsockopt(sock, 0, SRTO_TLPKTDROP, &tlpktdrop, sizeof(tlpktdrop)) != 0) {
        setError("Failed to set SRTO_TLPKTDROP");
        return false;
    }
    
    // Set NAK report
    int nakreport = config_.nak_report ? 1 : 0;
    if (srt_setsockopt(sock, 0, SRTO_NAKREPORT, &nakreport, sizeof(nakreport)) != 0) {
        setError("Failed to set SRTO_NAKREPORT");
        return false;
    }
    
    // Set encryption if passphrase provided
    if (!config_.passphrase.empty()) {
        if (srt_setsockopt(sock, 0, SRTO_PASSPHRASE, 
                          config_.passphrase.c_str(), 
                          static_cast<int>(config_.passphrase.length())) != 0) {
            setError("Failed to set SRTO_PASSPHRASE");
            return false;
        }
        
        if (srt_setsockopt(sock, 0, SRTO_PBKEYLEN, &config_.key_length, sizeof(config_.key_length)) != 0) {
            setError("Failed to set SRTO_PBKEYLEN");
            return false;
        }
    }
    
    return true;
}

bool SRTTransport::connectCaller() {
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(config_.port);
    
    if (inet_pton(AF_INET, config_.address.c_str(), &sa.sin_addr) != 1) {
        setError("Invalid address: " + config_.address);
        return false;
    }
    
    if (srt_connect(socket_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == SRT_ERROR) {
        setError(std::string("Connect failed: ") + srt_getlasterror_str());
        return false;
    }
    
    connected_ = true;
    stats_.connected = true;
    
    if (state_callback_) {
        state_callback_(true, "");
    }
    
    return true;
}

bool SRTTransport::startListener() {
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(config_.port);
    sa.sin_addr.s_addr = INADDR_ANY;
    
    if (srt_bind(socket_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == SRT_ERROR) {
        setError(std::string("Bind failed: ") + srt_getlasterror_str());
        return false;
    }
    
    if (srt_listen(socket_, 1) == SRT_ERROR) {
        setError(std::string("Listen failed: ") + srt_getlasterror_str());
        return false;
    }
    
    return true;
}

void SRTTransport::receiveLoop() {
    uint8_t buffer[SRT_BUFFER_SIZE];
    
    while (running_) {
        int received = srt_recv(socket_, reinterpret_cast<char*>(buffer), SRT_BUFFER_SIZE);
        
        if (received == SRT_ERROR) {
            int error = srt_getlasterror(nullptr);
            if (error != SRT_EASYNCRCV) {  // Ignore async receive timeout
                setError(std::string("Receive failed: ") + srt_getlasterror_str());
                connected_ = false;
                stats_.connected = false;
                
                if (state_callback_) {
                    state_callback_(false, last_error_);
                }
                
                if (config_.enable_reconnect && config_.mode == Mode::CALLER) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    connectCaller();
                }
                continue;
            }
        } else if (received > 0) {
            stats_.bytes_received += received;
            stats_.packets_received++;
            
            if (data_callback_) {
                data_callback_(buffer, static_cast<size_t>(received));
            }
        }
        
        // Periodically update stats
        static auto last_update = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_update).count() >= 1) {
            updateStats();
            last_update = now;
        }
    }
}

void SRTTransport::acceptLoop() {
    while (running_) {
        sockaddr_storage client_addr;
        int addr_len = sizeof(client_addr);
        
        SRTSOCKET client_sock = srt_accept(socket_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        
        if (client_sock == SRT_INVALID_SOCK) {
            continue;
        }
        
        // Close previous client if any
        if (accept_socket_ != SRT_INVALID_SOCK) {
            srt_close(accept_socket_);
        }
        
        accept_socket_ = client_sock;
        socket_ = client_sock;  // Use accepted socket for communication
        connected_ = true;
        stats_.connected = true;
        
        if (state_callback_) {
            state_callback_(true, "");
        }
        
        // Start receive loop
        if (!recv_thread_) {
            recv_thread_ = std::make_unique<std::thread>(&SRTTransport::receiveLoop, this);
        }
    }
}

void SRTTransport::updateStats() {
    if (socket_ == SRT_INVALID_SOCK || !connected_) {
        return;
    }
    
    SRT_TRACEBSTATS stats;
    if (srt_bistats(socket_, &stats, 0, 1) == 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        stats_.packets_lost = stats.pktRcvLoss;
        stats_.packets_retransmitted = stats.pktRetrans;
        stats_.rtt_ms = stats.msRTT;
        stats_.bandwidth_mbps = stats.mbpsBandwidth;
        stats_.send_buffer_available = stats.byteAvailSndBuf;
        stats_.recv_buffer_available = stats.byteAvailRcvBuf;
    }
}

void SRTTransport::setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = error;
}

} // namespace jpegxs
