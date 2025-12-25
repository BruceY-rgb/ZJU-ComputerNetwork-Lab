#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <cstring>
#include <cstdint>
#include <arpa/inet.h>

// 消息类型定义
enum class MessageType : uint8_t {
    // 客户端请求
    REQ_CONNECT = 0x01,
    REQ_DISCONNECT = 0x02,
    REQ_GET_TIME = 0x03,
    REQ_GET_NAME = 0x04,
    REQ_GET_CLIENTS = 0x05,
    REQ_SEND_MSG = 0x06,
    
    // 服务端响应
    RESP_CONNECT = 0x11,
    RESP_TIME = 0x12,
    RESP_NAME = 0x13,
    RESP_CLIENTS = 0x14,
    RESP_SEND_RESULT = 0x15,
    
    // 服务端通知
    NOTIFY_MSG = 0x21,
    NOTIFY_DISCONNECT = 0x22
};

// 数据包结构 (固定头部 + 变长数据)
#pragma pack(push, 1)
struct PacketHeader {
    uint8_t type;           // 消息类型
    uint16_t length;        // 数据长度(不包括头部)
    uint32_t sequence;      // 序列号(用于调试和匹配)
};
#pragma pack(pop)

// 完整数据包

class Packet {
public:
    PacketHeader header;
    std::string data;

    Packet() {
        memset(&header, 0, sizeof(header));
    }
    
    Packet(MessageType type, const std::string& payload = "") {
        header.type = static_cast<uint8_t>(type);
        header.length = htons(payload.length());
        header.sequence = 0;
        data = payload;
    }

    // 序列化为字节流
    std::string serialize() const {
        std::string result;
        result.append(reinterpret_cast<const char*>(&header), sizeof(header));
        result.append(data);
        return result;
    }
    
    // 从字节流反序列化
    static bool deserialize(const char* buffer, size_t len, Packet& pkt) {
        if (len < sizeof(PacketHeader)) {
            return false;
        }
        
        memcpy(&pkt.header, buffer, sizeof(PacketHeader));
        uint16_t dataLen = ntohs(pkt.header.length);
        
        if (len < sizeof(PacketHeader) + dataLen) {
            return false;
        }
        
        pkt.data.assign(buffer + sizeof(PacketHeader), dataLen);
        return true;
    }
    
    MessageType getType() const {
        return static_cast<MessageType>(header.type);
    }
    
    size_t totalSize() const {
        return sizeof(PacketHeader) + ntohs(header.length);
    }
};

#endif // PROTOCOL_H