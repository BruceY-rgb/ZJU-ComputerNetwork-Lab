#include "../include/protocol.h"
#include "../include/socket_wrapper.h"
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

void handleClient(int clientFd, std::string clientAddr) {
    SocketWrapper clientSock(clientFd);
    log("CONN", GREEN, "Accepted new peer: " + clientAddr);
    
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        connectedClients[clientFd] = clientAddr;
    }
    
    // 发送带有样式的欢迎语
    Packet welcomePkt(MessageType::RESP_CONNECT, "Connected to Lab7-Advanced-Server v2.0");
    clientSock.send(welcomePkt);
    
    while (!shouldExit) {
        Packet request;
        if (!clientSock.recv(request)) break;
        
        Packet response;
        switch (request.getType()) {
            case MessageType::REQ_GET_TIME:
                log("TASK", BLUE, "Query: TIME | Client: " + clientAddr);
                response = Packet(MessageType::RESP_TIME, getCurrentTime());
                clientSock.send(response);
                break;
                
            case MessageType::REQ_GET_NAME:
                log("TASK", BLUE, "Query: NAME | Client: " + clientAddr);
                response = Packet(MessageType::RESP_NAME, "ZJU-BS-Experimental-Server-4703");
                clientSock.send(response);
                break;
                
            case MessageType::REQ_GET_CLIENTS:
                log("TASK", BLUE, "Query: LIST | Client: " + clientAddr);
                response = Packet(MessageType::RESP_CLIENTS, getClientList());
                clientSock.send(response);
                break;

            case MessageType::REQ_SEND_MSG: {
                log("TASK", MAGENTA, "Message Forward Request | From: " + clientAddr);

                // 解析数据：编号|消息内容
                std::string data = request.data;
                size_t delimPos = data.find('|');

                if (delimPos == std::string::npos) {
                    response = Packet(MessageType::RESP_SEND_RESULT, "Error: Invalid message format");
                    clientSock.send(response);
                    break;
                }

                std::string targetIdStr = data.substr(0, delimPos);
                std::string message = data.substr(delimPos + 1);

                int targetIndex = 0;
                try {
                    targetIndex = std::stoi(targetIdStr);
                } catch (...) {
                    response = Packet(MessageType::RESP_SEND_RESULT, "Error: Invalid target ID");
                    clientSock.send(response);
                    break;
                }

                // 查找目标客户端
                int targetFd = -1;
                std::string targetAddr;
                {
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    if (targetIndex < 1 || targetIndex > (int)connectedClients.size()) {
                        response = Packet(MessageType::RESP_SEND_RESULT,
                                        "Error: Client #" + targetIdStr + " not found");
                        clientSock.send(response);
                        break;
                    }

                    // 根据编号找到对应的socket fd
                    auto it = connectedClients.begin();
                    std::advance(it, targetIndex - 1);
                    targetFd = it->first;
                    targetAddr = it->second;
                }

                // 向目标客户端发送消息通知
                std::string notifyData = "From " + clientAddr + ": " + message;
                Packet notifyPkt(MessageType::NOTIFY_MSG, notifyData);

                // 直接使用socket fd发送
                ssize_t sent = ::send(targetFd, notifyPkt.serialize().c_str(),
                                     notifyPkt.serialize().length(), 0);

                if (sent > 0) {
                    log("TASK", GREEN, "Message forwarded: " + clientAddr + " -> " + targetAddr);
                    response = Packet(MessageType::RESP_SEND_RESULT,
                                    "Success: Message sent to " + targetAddr);
                } else {
                    log("TASK", RED, "Message forward failed: " + clientAddr + " -> " + targetAddr);
                    response = Packet(MessageType::RESP_SEND_RESULT,
                                    "Error: Failed to send message to client #" + targetIdStr);
                }

                clientSock.send(response);
                break;
            }

            case MessageType::REQ_DISCONNECT:
                log("CONN", YELLOW, "Peer requested termination: " + clientAddr);
                goto cleanup;
                
            default:
                log("WARN", MAGENTA, "Unknown opcode from " + clientAddr);
                break;
        }
    }
    
cleanup:
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        connectedClients.erase(clientFd);
    }
    log("CONN", RED, "Peer disconnected: " + clientAddr);
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