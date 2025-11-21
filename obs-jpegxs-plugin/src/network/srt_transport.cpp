#include "srt_transport.h"
#include <srt/srt.h>
#include <cstring>
#include <chrono>

namespace jpegxs {

constexpr size_t SRT_BUFFER_SIZE = 2048;  // Safer MTU size (Jumbo frames support)

SRTTransport::SRTTransport(const Config& cfg)
    : config_(cfg)
    , connection_socket_(SRT_INVALID_SOCK)
    , listener_socket_(SRT_INVALID_SOCK)
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
        // Create socket for caller
        connection_socket_ = srt_create_socket();
        if (connection_socket_ == SRT_INVALID_SOCK) {
            setError("Failed to create SRT socket");
            return false;
        }

        if (!configureSRTSocket(connection_socket_)) {
            srt_close(connection_socket_);
            connection_socket_ = SRT_INVALID_SOCK;
            return false;
        }

        if (!connectCaller()) {
            running_ = false;
            cleanupSRT();
            return false;
        }
        
        // Start receive thread
        recv_thread_ = std::make_unique<std::thread>(&SRTTransport::receiveLoop, this);
    } else {
        // Create socket for listener
        listener_socket_ = srt_create_socket();
        if (listener_socket_ == SRT_INVALID_SOCK) {
            setError("Failed to create SRT listener socket");
            return false;
        }

        if (!configureSRTSocket(listener_socket_)) {
            srt_close(listener_socket_);
            listener_socket_ = SRT_INVALID_SOCK;
            return false;
        }

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
    
    // Close sockets FIRST to unblock recv/accept calls
    cleanupSRT();
    
    if (recv_thread_ && recv_thread_->joinable()) {
        // Check if we are joining the current thread (should not happen in normal usage but possible)
        if (recv_thread_->get_id() != std::this_thread::get_id()) {
            recv_thread_->join();
        } else {
            // Detach if we are stopping from within the thread (e.g. error callback)
            recv_thread_->detach();
        }
    }
    
    if (accept_thread_ && accept_thread_->joinable()) {
        if (accept_thread_->get_id() != std::this_thread::get_id()) {
            accept_thread_->join();
        } else {
            accept_thread_->detach();
        }
    }
    
    recv_thread_.reset();
    accept_thread_.reset();
}

bool SRTTransport::isConnected() const {
    return connected_;
}

bool SRTTransport::send(const uint8_t* data, size_t size) {
    // Atomic read of connection socket
    SRTSOCKET sock = connection_socket_;
    
    if (!connected_ || sock == SRT_INVALID_SOCK) {
        return false;
    }
    
    int sent = srt_send(sock, reinterpret_cast<const char*>(data), static_cast<int>(size));
    
    if (sent == SRT_ERROR) {
        // Update last error but avoid locking if not needed (optimization)
        // But for now, let's set it so caller can retrieve it
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
    return true;
}

void SRTTransport::cleanupSRT() {
    // Close listener socket
    if (listener_socket_ != SRT_INVALID_SOCK) {
        srt_close(listener_socket_);
        listener_socket_ = SRT_INVALID_SOCK;
    }
    
    // Close connection socket
    if (connection_socket_ != SRT_INVALID_SOCK) {
        srt_close(connection_socket_);
        connection_socket_ = SRT_INVALID_SOCK;
    }
}

bool SRTTransport::configureSRTSocket(SRTSOCKET sock) {
    // Explicitly set Message API mode (Live)
    int transtype = SRTT_LIVE;
    if (srt_setsockopt(sock, 0, SRTO_TRANSTYPE, &transtype, sizeof(transtype)) != 0) {
        setError("Failed to set SRTO_TRANSTYPE");
        return false;
    }

    // Set payload size to be safe (default is 1316, we need slightly more for our RTP packets ~1300+Header)
    int payload_size = 1456;
    if (srt_setsockopt(sock, 0, SRTO_PAYLOADSIZE, &payload_size, sizeof(payload_size)) != 0) {
        setError("Failed to set SRTO_PAYLOADSIZE");
        return false;
    }

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
    
    if (srt_connect(connection_socket_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == SRT_ERROR) {
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
    
    if (srt_bind(listener_socket_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == SRT_ERROR) {
        setError(std::string("Bind failed: ") + srt_getlasterror_str());
        return false;
    }
    
    if (srt_listen(listener_socket_, 1) == SRT_ERROR) {
        setError(std::string("Listen failed: ") + srt_getlasterror_str());
        return false;
    }
    
    return true;
}

void SRTTransport::receiveLoop() {
    uint8_t buffer[SRT_BUFFER_SIZE];
    
    while (running_) {
        try {
            SRTSOCKET sock = connection_socket_;
            
            if (sock == SRT_INVALID_SOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            int received = srt_recv(sock, reinterpret_cast<char*>(buffer), SRT_BUFFER_SIZE);
            
            if (received == SRT_ERROR) {
                int error = srt_getlasterror(nullptr);
                if (error != SRT_EASYNCRCV) {  // Ignore async receive timeout
                    // Only report error if we didn't intentionally close the socket
                    if (running_ && connection_socket_ == sock) {
                        setError(std::string("Receive failed: ") + srt_getlasterror_str());
                        connected_ = false;
                        stats_.connected = false;
                        
                        if (state_callback_) {
                            state_callback_(false, last_error_);
                        }
                        
                        if (config_.mode == Mode::CALLER && config_.enable_reconnect) {
                            // Reconnect logic for caller
                            srt_close(connection_socket_);
                            connection_socket_ = srt_create_socket();
                            configureSRTSocket(connection_socket_);
                            
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            if (running_) {
                                connectCaller();
                            }
                        } else if (config_.mode == Mode::LISTENER) {
                            // Listener logic: Close connection and wait for new one
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                if (connection_socket_ == sock) {
                                    srt_close(connection_socket_);
                                    connection_socket_ = SRT_INVALID_SOCK;
                                }
                            }
                            // Loop will continue, but sock check at top will sleep
                        }
                    } else if (connection_socket_ == SRT_INVALID_SOCK) {
                        // Socket was closed, just wait
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    } else {
                        // Socket ID mismatch (old socket), just wait
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
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
        } catch (const std::exception& e) {
            setError(std::string("Exception in receiveLoop: ") + e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (...) {
            setError("Unknown exception in receiveLoop");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void SRTTransport::acceptLoop() {
    while (running_) {
        try {
            sockaddr_storage client_addr;
            int addr_len = sizeof(client_addr);
            
            SRTSOCKET client_sock = srt_accept(listener_socket_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
            
            if (client_sock == SRT_INVALID_SOCK) {
                // If listener socket was closed, we should exit
                if (!running_) break;
                continue;
            }
            
            // Configure the accepted socket
            configureSRTSocket(client_sock);

            // Thread-safe socket replacement
            {
                std::lock_guard<std::mutex> lock(mutex_);
                
                // Close previous client if any
                if (connection_socket_ != SRT_INVALID_SOCK) {
                    srt_close(connection_socket_);
                }
                
                connection_socket_ = client_sock;
                connected_ = true;
                stats_.connected = true;
            }
            
            if (state_callback_) {
                state_callback_(true, "");
            }
            
            // Start receive loop if not already running
            if (!recv_thread_) {
                recv_thread_ = std::make_unique<std::thread>(&SRTTransport::receiveLoop, this);
            }
        } catch (const std::exception& e) {
            setError(std::string("Exception in acceptLoop: ") + e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (...) {
            setError("Unknown exception in acceptLoop");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void SRTTransport::updateStats() {
    SRTSOCKET sock = connection_socket_;
    if (sock == SRT_INVALID_SOCK || !connected_) {
        return;
    }
    
    SRT_TRACEBSTATS stats;
    if (srt_bistats(sock, &stats, 0, 1) == 0) {
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
