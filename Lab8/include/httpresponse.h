#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#include <string>
#include <map>
#include <fstream>
#include <sys/socket.h>

// --- ANSI 颜色与样式定义 ---
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m" 
#define CLEAR   "\033[2J\033[H"

// ============================================
// HTTP响应构建类
// ============================================

class HTTPResponse {
private:
    int statusCode;
    std::string reasonPhrase;
    std::map<std::string, std::string> headers;
    std::string body;
    
public:
    /**
     * 构造函数
     */
    HTTPResponse(int code = 200, const std::string& phrase = "OK") 
        : statusCode(code), reasonPhrase(phrase) {
        // 默认添加一些必要的头字段
        headers["Connection"] = "close";
    }
    
    /**
     * 设置状态码
     */
    void setStatus(int code, const std::string& phrase) {
        statusCode = code;
        reasonPhrase = phrase;
    }
    
    /**
     * 设置响应头字段
     */
    void setHeader(const std::string& key, const std::string& value) {
        headers[key] = value;
    }
    
    /**
     * 设置响应正文
     */
    void setBody(const std::string& content) {
        body = content;
        // 自动设置 Content-Length
        headers["Content-Length"] = std::to_string(content.length());
    }
    
    /**
     * 序列化为HTTP响应字符串
     */
    std::string serialize() const {
        std::string response;
        
        // 状态行
        response += "HTTP/1.0 " + std::to_string(statusCode) + " " + reasonPhrase + "\r\n";
        
        // 响应头
        for (const auto& [key, value] : headers) {
            response += key + ": " + value + "\r\n";
        }
        
        // 空行
        response += "\r\n";
        
        // 响应正文
        response += body;
        
        return response;
    }
};

#endif