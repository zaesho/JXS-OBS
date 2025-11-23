#include "udp_socket.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
#endif

namespace jpegxs {

UDPSocket::UDPSocket() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

UDPSocket::~UDPSocket() {
    close();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool UDPSocket::init() {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) {
        return false;
    }
    
    // INCREASE DEFAULT KERNEL BUFFERS
    // This is critical for high-bandwidth video (100Mbps+).
    // Default is usually 64KB, which fills instantly during any jitter.
    // Set to 1MB (reduced from 8MB) to avoid bufferbloat and reduce latency accumulation.
    // For strict ST 2110-22 low latency, smaller buffers force dropped packets instead of lag.
    int buf_size = 1 * 1024 * 1024; 
    setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, (const char*)&buf_size, sizeof(buf_size));
    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, (const char*)&buf_size, sizeof(buf_size));
    
    return true;
}

void UDPSocket::close() {
    if (sock_ != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(sock_);
#else
        ::close(sock_);
#endif
        sock_ = INVALID_SOCKET;
    }
}

bool UDPSocket::bind(uint16_t port, const std::string& interface_ip) {
    if (sock_ == INVALID_SOCKET) init();

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, interface_ip.c_str(), &addr.sin_addr) <= 0) {
        return false;
    }

#ifdef _WIN32
    // Allow re-use of address/port for multicast
    int reuse = 1;
    if (setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        // Log warning?
    }
#else
    int reuse = 1;
    if (setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        // Log warning?
    }
#endif

    if (::bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        return false;
    }
    return true;
}

bool UDPSocket::connect(const std::string& dest_ip, uint16_t dest_port) {
    if (sock_ == INVALID_SOCKET) init();

    sockaddr_in dest_addr;
    std::memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    
    if (inet_pton(AF_INET, dest_ip.c_str(), &dest_addr.sin_addr) <= 0) {
        return false;
    }

    if (::connect(sock_, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) == SOCKET_ERROR) {
        return false;
    }
    return true;
}

bool UDPSocket::joinMulticast(const std::string& multicast_ip, const std::string& interface_ip) {
    if (sock_ == INVALID_SOCKET) return false;

    struct ip_mreq mreq;
    if (inet_pton(AF_INET, multicast_ip.c_str(), &mreq.imr_multiaddr) <= 0) {
        return false;
    }
    if (inet_pton(AF_INET, interface_ip.c_str(), &mreq.imr_interface) <= 0) {
        return false;
    }

    if (setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
        return false;
    }
    
    is_multicast_ = true;
    return true;
}

bool UDPSocket::sendTo(const uint8_t* data, size_t size, const std::string& dest_ip, uint16_t dest_port) {
    if (sock_ == INVALID_SOCKET) init();

    sockaddr_in dest_addr;
    std::memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    
    if (inet_pton(AF_INET, dest_ip.c_str(), &dest_addr.sin_addr) <= 0) {
        return false;
    }

    int sent = sendto(sock_, (const char*)data, (int)size, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    return sent == (int)size;
}

bool UDPSocket::send(const uint8_t* data, size_t size) {
    if (sock_ == INVALID_SOCKET) return false;
    
    // send() uses the connected address
#ifdef _WIN32
    int sent = ::send(sock_, (const char*)data, (int)size, 0);
#else
    int sent = ::send(sock_, data, size, 0);
#endif
    return sent == (int)size;
}

int UDPSocket::recvFrom(uint8_t* buffer, size_t max_size, std::string& src_ip, uint16_t& src_port) {
    if (sock_ == INVALID_SOCKET) return -1;

    sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    int received = recvfrom(sock_, (char*)buffer, (int)max_size, 0, (struct sockaddr*)&src_addr, &addr_len);

    if (received > 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        src_ip = ip_str;
        src_port = ntohs(src_addr.sin_port);
    }

    return received;
}

void UDPSocket::setNonBlocking(bool non_blocking) {
    if (sock_ == INVALID_SOCKET) return;

#ifdef _WIN32
    u_long mode = non_blocking ? 1 : 0;
    ioctlsocket(sock_, FIONBIO, &mode);
#else
    int flags = fcntl(sock_, F_GETFL, 0);
    if (non_blocking) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    fcntl(sock_, F_SETFL, flags);
#endif
}

void UDPSocket::setSendBuffer(int size) {
    if (sock_ == INVALID_SOCKET) return;
    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, (const char*)&size, sizeof(size));
}

void UDPSocket::setRecvBuffer(int size) {
    if (sock_ == INVALID_SOCKET) return;
    setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, (const char*)&size, sizeof(size));
}

void UDPSocket::setMulticastTTL(int ttl) {
    if (sock_ == INVALID_SOCKET) return;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
}

void UDPSocket::setMulticastLoop(bool loop) {
    if (sock_ == INVALID_SOCKET) return;
    int loop_val = loop ? 1 : 0;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop_val, sizeof(loop_val));
}

void UDPSocket::setMulticastInterface(const std::string& interface_ip) {
    if (sock_ == INVALID_SOCKET) return;
    
    struct in_addr addr;
    if (inet_pton(AF_INET, interface_ip.c_str(), &addr) > 0) {
        setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&addr, sizeof(addr));
    }
}

} // namespace jpegxs
