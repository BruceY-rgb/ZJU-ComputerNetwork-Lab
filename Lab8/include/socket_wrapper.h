#ifndef SOCKET_WRAPPER_H
#define SOCKET_WRAPPER_H

#include "protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <vector>

class SocketWrapper {
private:
    int sockfd;
    bool valid;
    
public:
    SocketWrapper(int fd = -1) : sockfd(fd), valid(fd >= 0) {}
    
    ~SocketWrapper() {
        if (valid && sockfd >= 0) {
            ::close(sockfd);
        }
    }
    
    // 禁止拷贝
    SocketWrapper(const SocketWrapper&) = delete;
    SocketWrapper& operator=(const SocketWrapper&) = delete;
    
    // 允许移动
    SocketWrapper(SocketWrapper&& other) noexcept {
        sockfd = other.sockfd;
        valid = other.valid;
        other.valid = false;
    }
    
    bool isValid() const { return valid && sockfd >= 0; }
    int getFd() const { return sockfd; }
    
    // 发送数据包
    bool send(const Packet& pkt) {
        if (!isValid()) return false;
        
        std::string data = pkt.serialize();
        size_t totalSent = 0;
        
        while (totalSent < data.size()) {
            ssize_t sent = ::send(sockfd, data.c_str() + totalSent, 
                                  data.size() - totalSent, 0);
            if (sent < 0) {
                std::cerr << "[Error] Failed to send data" << std::endl;
                return false;
            }
            totalSent += sent;
        }
        return true;
    }
    
    // 接收数据包
    bool recv(Packet& pkt) {
        if (!isValid()) return false;
        
        // 先接收头部
        PacketHeader header;
        size_t totalRecv = 0;
        
        while (totalRecv < sizeof(PacketHeader)) {
            ssize_t n = ::recv(sockfd, 
                              reinterpret_cast<char*>(&header) + totalRecv,
                              sizeof(PacketHeader) - totalRecv, 0);
            if (n <= 0) {
                if (n == 0) {
                    std::cerr << "[Info] Connection closed by peer" << std::endl;
                } else {
                    std::cerr << "[Error] Failed to receive header" << std::endl;
                }
                return false;
            }
            totalRecv += n;
        }
        
        // 接收数据部分
        uint16_t dataLen = ntohs(header.length);
        std::vector<char> buffer(dataLen);
        totalRecv = 0;
        
        while (totalRecv < dataLen) {
            ssize_t n = ::recv(sockfd, buffer.data() + totalRecv,
                              dataLen - totalRecv, 0);
            if (n <= 0) {
                std::cerr << "[Error] Failed to receive data" << std::endl;
                return false;
            }
            totalRecv += n;
        }
        
        // 构造完整数据包
        pkt.header = header;
        pkt.data.assign(buffer.begin(), buffer.end());
        
        return true;
    }
    
    // 重载操作符,类似cin/cout
    SocketWrapper& operator<<(const Packet& pkt) {
        send(pkt);
        return *this;
    }
    
    SocketWrapper& operator>>(Packet& pkt) {
        recv(pkt);
        return *this;
    }
    
    void close() {
        if (valid && sockfd >= 0) {
            ::close(sockfd);
            sockfd = -1;
            valid = false;
        }
    }
};

#endif // SOCKET_WRAPPER_H