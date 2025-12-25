#include "../include/protocol.h"
#include "../include/socket_wrapper.h"
#include "../include/httpresponse.h"
#include "../include/httprequest.h"
#include "../include/tools.h"
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <mutex>
#include <map>
#include <csignal>
#include <ctime>
#include <iomanip>
#include <fstream>     // for file operations
#include <algorithm>   // for std::min
#include <cctype>      // for std::tolower

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

#define SERVER_PORT 4703
#define MAX_CLIENT_QUEUE 20

// 全局变量
bool shouldExit = false;
std::mutex clientsMutex;
std::mutex logMutex; // 用于防止多个线程日志输出乱序
std::map<int, std::string> connectedClients; 


void log(std::string level, std::string color, std::string msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    time_t now = time(nullptr);
    tm* ltm = localtime(&now);
    std::cout << "[" << std::setfill('0') << std::setw(2) << ltm->tm_hour << ":" 
              << std::setw(2) << ltm->tm_min << ":" << std::setw(2) << ltm->tm_sec << "] "
              << color << BOLD << "[" << level << "] " << RESET << msg << std::endl;
}

void showServerBanner() {
    std::cout << CLEAR << CYAN << BOLD;
    std::cout << " ██████╗ ███████╗██████╗ ██╗   ██╗███████╗██████╗ \n";
    std::cout << "██╔════╝ ██╔════╝██╔══██╗██║   ██║██╔════╝██╔══██╗\n";
    std::cout << "╚█████╗  █████╗  ██████╔╝██║   ██║█████╗  ██████╔╝\n";
    std::cout << " ╚═══██╗ ██╔══╝  ██╔══██╗╚██╗ ██╔╝██╔══╝  ██╔══██╗\n";
    std::cout << "██████╔╝ ███████╗██║  ██║ ╚████╔╝ ███████╗██║  ██║\n";
    std::cout << "╚═════╝  ╚══════╝╚═╝  ╚═╝  ╚═══╝  ╚══════╝╚═╝  ╚═╝\n";
    std::cout << "         >> System Orchestrator | Port: " << SERVER_PORT << RESET << "\n\n";
}

void exitHandler(int signal) {
    log("SYSTEM", RED, "Shutting down server safely...");
    shouldExit = true;
}

std::string getCurrentTime() {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

std::string getClientList() {
    std::lock_guard<std::mutex> lock(clientsMutex);
    std::string result;
    int index = 1;
    for (const auto& [fd, addr] : connectedClients) {
        result += std::to_string(index++) + ". " + addr + " (Active)\n";
    }
    return result.empty() ? "No active connections." : result;
}

// --- 业务处理 ---
// ============================================
// GET 请求处理器
// ============================================

/**
 * 处理GET请求
 * @param clientFd 客户端socket描述符
 * @param uri 请求的URI
 * @param headers 请求头字段map
 */
 void getHandler(int clientFd, const std::string uri, const std::map<std::string, std::string>& headers) {
    log("TASK", BLUE, "Processing Get: " + uri);

    HTTPResponse response;

    //映射uri到文件路径
    std::string filePath = mapUriToPath(uri);


    if(filePath.empty()||!fileExists(filePath)){
        //文件不存在，返回404
        response.setStatus(404, "Not Found");
        response.setHeader("Content-Type", "text/plain");
        response.setBody("");
    }else {
        //读取文件内容
        std::string content = readFile(filePath);

        if(content.empty()&&fileExists(filePath)) {
            //文件存在但是读取失败
            response.setStatus(500, "Internal Server Error");
            response.setHeader("Content-Type", "text/plain");
            response.setBody("");
        }else {
            //成功读取文件
            response.setStatus(200, "OK");
            response.setHeader("Content-Type", getContentType(filePath));
            response.setBody(content);
        }
    }

    std::string responseStr = response.serialize();
    send(clientFd, responseStr.c_str(), responseStr.size(), 0);
    close(clientFd);
 }

 /**
 * 解析POST表单数据（application/x-www-form-urlencoded格式）
 * 输入："login=admin&pass=123456"
 * 输出：map{"login": "admin", "pass": "123456"}
 */

std::map<std::string, std::string> parsePostData(const std::string& postData) {
    std::map<std::string, std::string> result;
    size_t pos = 0;

    while(pos < postData.size()) {
        size_t ampPos = postData.find("&", pos);
        if(ampPos == std::string::npos) {
            ampPos = postData.size();
        }

        std::string pair = postData.substr(pos, ampPos - pos);
        size_t equalPos = pair.find("=");  // 在pair中查找

        if(equalPos != std::string::npos) {
            std::string key = pair.substr(0, equalPos);  // 从0开始
            std::string value = pair.substr(equalPos + 1);
            result[key] = value;
        }
        pos = ampPos + 1;
    }
    return result;
}

/**
 * 处理POST请求
 * @param clientFd 浏览器socket描述符
 * @param uri 请求的URI
 * @param headers 请求头字段map
 * @param body 请求体
 */

void postHandler(int clientFd, const std::string uri, const std::map<std::string, std::string>& headers, const std::string& body){
    log("TASK", BLUE, "Processing Post: " + uri);
    HTTPResponse response;

    if(uri == "/dopost"){
        //解析表单数据
        std::map<std::string, std::string> formData = parsePostData(body);

        // 获取用户名和密码
        std::string username = formData["login"];
        std::string password = formData["pass"];

        log("DEBUG", CYAN, "Login attempt - User: " + username);

        // 验证登录信息（硬编码的用户名和密码，你可以修改）
        std::string validUsername = "3230104703";
        std::string validPassword = "ysx050223";

        std::string htmlBody;
        if (username == validUsername && password == validPassword) {
            log("INFO", GREEN, "Login successful: " + username);
            htmlBody = "<html><body>Login Success</body></html>";
        } else {
            log("WARN", YELLOW, "Login failed: " + username);
            htmlBody = "<html><body>Login Failed</body></html>";
        }
        
        response.setStatus(200, "OK");
        response.setHeader("Content-Type", "text/html");
        response.setBody(htmlBody);
    }else {
        // 不是 /dopost，返回404
        log("WARN", YELLOW, "POST to invalid URI: " + uri);
        response.setStatus(404, "Not Found");
        response.setHeader("Content-Type", "text/plain");
        response.setBody("");
    }

    //发送响应
    std::string responseStr = response.serialize();
    send(clientFd, responseStr.c_str(), responseStr.size(), 0);
    close(clientFd);
}

// ============================================
// 主连接处理函数
// ============================================

void handleClient(int clientFd, std::string clientAddr) {
    log("HTTP_REQUEST", GREEN, "New request from: " + clientAddr);

    // 1. 接收请求数据
    char buffer[8192] = {0}; // 增大buffer以支持更大的请求
    ssize_t byteRead = recv(clientFd, buffer, sizeof(buffer) - 1, 0);

    if(byteRead <= 0) {
        log("ERROR", RED, "Failed to receive data from " + clientAddr);
        close(clientFd);
        return;
    }

    // 2. 解析请求
    std::string rawRequest(buffer, byteRead);
    
    // 2.1 解析请求行
    size_t firstCRLF = rawRequest.find("\r\n");
    if (firstCRLF == std::string::npos) {
        log("ERROR", RED, "Invalid HTTP request: no CRLF in request line");
        close(clientFd);
        return;
    }
    
    std::string requestLine = rawRequest.substr(0, firstCRLF);
    
    // 提取方法、URI、HTTP版本
    size_t firstSpace = requestLine.find(' ');
    size_t secondSpace = requestLine.find(' ', firstSpace + 1);
    
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        log("ERROR", RED, "Malformed request line");
        close(clientFd);
        return;
    }
    
    std::string method = requestLine.substr(0, firstSpace);
    std::string uri = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::string httpVersion = requestLine.substr(secondSpace + 1);
    
    log("DEBUG", CYAN, "Method: " + method + ", URI: " + uri);
    
    // 2.2 解析请求头
    size_t headerEnd = rawRequest.find("\r\n\r\n");
    std::string headersStr;
    std::map<std::string, std::string> headers;
    
    if (headerEnd != std::string::npos) {
        headersStr = rawRequest.substr(firstCRLF + 2, headerEnd - (firstCRLF + 2));
        headers = parseHeaders(headersStr);
    } else {
        log("WARN", YELLOW, "No header end marker found");
    }
    
    // 2.3 解析请求正文（如果有Content-Length）
    std::string requestBody;
    
    auto clIt = headers.find("content-length");
    if (clIt != headers.end()) {
        int contentLength = std::stoi(clIt->second);
        
        if (contentLength > 0) {
            size_t bodyStart = headerEnd + 4; // 跳过 \r\n\r\n
            
            // buffer中已有的正文数据
            if (bodyStart < byteRead) {
                size_t alreadyRead = byteRead - bodyStart;
                requestBody.append(buffer + bodyStart, 
                                  std::min((size_t)contentLength, alreadyRead));
            }
            
            // 如果还需要接收更多数据
            while (requestBody.length() < (size_t)contentLength) {
                char moreBuf[4096];
                size_t remaining = contentLength - requestBody.length();
                ssize_t n = recv(clientFd, moreBuf, 
                               std::min(sizeof(moreBuf), remaining), 0);
                
                if (n <= 0) {
                    log("ERROR", RED, "Failed to receive complete request body");
                    break;
                }
                
                requestBody.append(moreBuf, n);
            }
            
            log("DEBUG", CYAN, "Request body: " + requestBody);
        }
    }
    
    // 3. 根据方法分发到对应的处理函数
    if (method == "GET") {
        getHandler(clientFd, uri, headers);
    } else if (method == "POST") {
        postHandler(clientFd, uri, headers, requestBody);
    } else {
        // 不支持的方法
        log("WARN", YELLOW, "Unsupported method: " + method);
        HTTPResponse response(405, "Method Not Allowed");
        response.setHeader("Content-Type", "text/plain");
        response.setBody("");
        
        std::string responseStr = response.serialize();
        send(clientFd, responseStr.c_str(), responseStr.length(), 0);
        close(clientFd);
    }
    
    // 4. 清理全局列表
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        connectedClients.erase(clientFd);
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, exitHandler);
    signal(SIGTERM, exitHandler);
    
    showServerBanner();
    
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        log("ERROR", RED, "Kernel failed to allocate socket descriptor.");
        return -1;
    }
    
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(SERVER_PORT);
    
    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        log("ERROR", RED, "Binding failed on port " + std::to_string(SERVER_PORT));
        close(serverSocket);
        return -1;
    }
    
    if (listen(serverSocket, MAX_CLIENT_QUEUE) < 0) {
        log("ERROR", RED, "Listen failed.");
        close(serverSocket);
        return -1;
    }
    
    log("INIT", CYAN, "Orchestrator online. Listening for incoming sockets...");
    
    std::vector<std::thread> threads;
    
    while (!shouldExit) {
        struct sockaddr_in clientAddress;
        socklen_t clientAddressLength = sizeof(clientAddress);
        
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientAddressLength);
        
        if (clientSocket < 0) {
            if (shouldExit) break;
            log("ERROR", RED, "Accept failed on kernel level.");
            continue;
        }
        
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::string clientAddr = std::string(clientIP) + ":" + std::to_string(ntohs(clientAddress.sin_port));
        
        // 分发线程处理
        threads.emplace_back(handleClient, clientSocket, clientAddr);
        
        // 自动分离线程，避免主线程管理压力
        threads.back().detach(); 
    }
    
    log("EXIT", WHITE, "Finalizing kernel resources... Goodbye.");
    close(serverSocket);
    return 0;
}