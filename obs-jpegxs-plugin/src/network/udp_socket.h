#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    using socket_t = int;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace jpegxs {

class UDPSocket {
public:
    UDPSocket();
    ~UDPSocket();

    // Initialize socket (starts Winsock on Windows)
    bool init();

    // Bind to a local port (for receiving)
    bool bind(uint16_t port, const std::string& interface_ip = "0.0.0.0");

    // Join a multicast group
    bool joinMulticast(const std::string& multicast_ip, const std::string& interface_ip = "0.0.0.0");

    // Send data to a specific destination
    bool sendTo(const uint8_t* data, size_t size, const std::string& dest_ip, uint16_t dest_port);

    // Receive data (blocking or non-blocking depending on setup)
    // Returns bytes received, or -1 on error, 0 on shutdown/empty
    int recvFrom(uint8_t* buffer, size_t max_size, std::string& src_ip, uint16_t& src_port);

    // Set socket options
    void setNonBlocking(bool non_blocking);
    void setSendBuffer(int size);
    void setRecvBuffer(int size);
    void setMulticastTTL(int ttl);
    void setMulticastLoop(bool loop);
    void setMulticastInterface(const std::string& interface_ip);

    void close();

private:
    socket_t sock_ = INVALID_SOCKET;
    bool is_multicast_ = false;
};

} // namespace jpegxs

