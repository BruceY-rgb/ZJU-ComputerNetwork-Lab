#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#include <string>
#include <map>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <sys/socket.h>

// 前向声明工具函数（定义在tools.h中）
std::string toLower(const std::string& str);
std::string trim(const std::string& str);
std::map<std::string, std::string> parseHeaders(const std::string& headers);

class HTTPRequest {
private:
    std::string method; //选取的HTTP方法，如GET、POST、PUT、DELETE等
    std::string uri; // 请求的URI
    std::string httpVersion; // HTTP版本，如HTTP/1.0
    std::map<std::string, std::string> headers; //请求头字段
    std::string body;//请求正文
    bool isValid;//请求是否有效
public:
     /**
     * 构造函数：从原始请求字符串解析
     * @param rawRequest 完整的HTTP请求字符串（包括头部，可能包括部分正文）
     */
    HTTPRequest(const std::string& rawRequest) : isValid(false) {
        parse(rawRequest);
    }

    /**
     * 默认构造函数
     */
    HTTPRequest() : isValid(false) {}

    /**
     * 解析原始HTTP请求字符串
     * @param rawRequest 完整的HTTP请求字符串（包括头部，可能包括部分正文）
     */
     void parse(const std::string& rawRequest) {
        if(rawRequest.empty()) {
            return;
        }

        // 1. 解析请求行
        size_t firstCRLF = rawRequest.find("\r\n");
        if (firstCRLF == std::string::npos) {
            // 无效请求：没有CRLF
            return;
        }
        std::string requestLine = rawRequest.substr(0, firstCRLF);

        // 分割请求行：包括方法、URI和HTTP版本
        size_t firstSpacePos = requestLine.find(" ");
        size_t secondSpacePos = requestLine.find(" ", firstSpacePos + 1);

        if (firstSpacePos == std::string::npos || secondSpacePos == std::string::npos) {
            // 格式错误的请求行
            return;
        }

        method = requestLine.substr(0, firstSpacePos);
        uri = requestLine.substr(firstSpacePos + 1, secondSpacePos - firstSpacePos - 1);
        httpVersion = requestLine.substr(secondSpacePos + 1);

        // 2. 解析请求头
        size_t pos = firstCRLF + 2;
        size_t endPos = rawRequest.find("\r\n\r\n");
        if(endPos != std::string::npos) {
            std::string headersStr = rawRequest.substr(pos, endPos - pos);
            headers = parseHeaders(headersStr);

            // 3. 提取已经接受的请求正文部分
            size_t bodyStartPos = endPos + 4;
            if(bodyStartPos < rawRequest.size()){
                body = rawRequest.substr(bodyStartPos);
            }
        }

        isValid = true;
    }

     /**
     * 接收完整的请求正文
     * @param clientFd 客户端socket
     * @return 是否成功接收
     */
     bool receiveBody(int clientFd) {
        // 查找Content-Length头字段
        auto it = headers.find("content-length");
        if(it == headers.end()) {
            // 没有Content-Length说明没有请求正文
            return true;
        }

        int contentLength = 0;
        try{
            contentLength = std::stoi(it->second);
        }catch(...) {
            // Content-Length无效
            return false;
        }

        if(contentLength <= 0) {
            // 长度为0，说明没有请求正文
            return true;
        }

        size_t alreadyReceived = body.size();
        while(alreadyReceived < (size_t)contentLength){
            char buf[4096];
            size_t remaining = contentLength - alreadyReceived;
            size_t toRecv = std::min(sizeof(buf), remaining);

            ssize_t n = recv(clientFd, buf, toRecv, 0);

            if(n <= 0) {
                // 接收失败
                return false;
            }
            body.append(buf, n);
            alreadyReceived += n;
        }

        return true;
     }

     // Getter 方法
    bool valid() const { return isValid; }
    const std::string& getMethod() const { return method; }
    const std::string& getUri() const { return uri; }
    const std::string& getHttpVersion() const { return httpVersion; }
    const std::string& getBody() const { return body; }
    const std::map<std::string, std::string>& getHeaders() const { return headers; }

    /**
     * 获取指定的头字段值
     * @param key 字段名（会自动转换为小写）
     * @return 字段值，如果不存在返回空字符串
     */
     std::string getHeader(const std::string& key) const {
        std::string lowerKey = toLower(key);
        auto it = headers.find(lowerKey);
        return (it != headers.end()) ? it->second : "";
    }

    /**
     * 检查是否有指定的头字段
     */
    bool hasHeader(const std::string& key) const {
        std::string lowerKey = toLower(key);
        return headers.find(lowerKey) != headers.end();
    }
};

#endif
