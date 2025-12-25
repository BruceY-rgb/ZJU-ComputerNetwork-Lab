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
#include <mutex>
#include <condition_variable>
#include <queue>
#include <csignal>
#include <iomanip>
#include <atomic>
#include <chrono>

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

// 全局变量
bool shouldExit = false;
bool isConnected = false;
std::mutex msgMutex;
std::condition_variable msgCondition;
std::queue<Packet> msgQueue;
SocketWrapper* globalSocket = nullptr;

// 用于统计时间请求响应
std::atomic<int> timeResponseCount(0);
bool isBatchTimeRequest = false;

// --- 视觉组件 ---

void showBanner() {
    std::cout << CLEAR << CYAN << BOLD;
    std::cout << "██████╗ ██╗  ██╗     ██████╗██╗     ██╗███████╗███╗   ██╗████████╗\n";
    std::cout << "██╔══██╗╚██╗██╔╝    ██╔════╝██║     ██║██╔════╝████╗  ██║╚══██╔══╝\n";
    std::cout << "██████╔╝ ╚███╔╝     ██║     ██║     ██║█████╗  ██╔██╗ ██║   ██║   \n";
    std::cout << "██╔══██╗ ██╔██╗     ██║     ██║     ██║██╔══╝  ██║╚██╗██║   ██║   \n";
    std::cout << "██████╔╝██╔╝ ██╗    ╚██████╗███████╗██║███████╗██║ ╚████║   ██║   \n";
    std::cout << "╚═════╝ ╚═╝  ╚═╝     ╚═════╝╚══════╝╚═╝╚══════╝╚═╝  ╚═══╝   ╚═╝   \n";
    std::cout << "                                     Terminal Client v2.0 | BY YSX\n" << RESET;
    std::cout << YELLOW << "------------------------------------------------------------------\n" << RESET;
}

void showMenu() {
    std::cout << "\n" << YELLOW << BOLD << "╔═══════════════ 终端控制面板 ═══════════════╗" << RESET << std::endl;
    if (!isConnected) {
        std::cout << YELLOW << "║  " << RESET << BOLD << "1. " << GREEN << "建立远程连接 (Connect)" << RESET << std::setw(15) << YELLOW << "║" << RESET << std::endl;
        std::cout << YELLOW << "║  " << RESET << BOLD << "0. " << RED << "安全退出程序 (Exit)" << RESET << std::setw(18) << YELLOW << "║" << RESET << std::endl;
    } else {
        std::cout << YELLOW << "║  " << RESET << BOLD << "1. " << RED << "断开当前连接" << RESET << std::setw(23) << YELLOW << "║" << RESET << std::endl;
        std::cout << YELLOW << "║  " << RESET << BOLD << "2. " << WHITE << "获取服务器时间" << RESET << std::setw(21) << YELLOW << "║" << RESET << std::endl;
        std::cout << YELLOW << "║  " << RESET << BOLD << "3. " << WHITE << "获取服务器名称" << RESET << std::setw(21) << YELLOW << "║" << RESET << std::endl;
        std::cout << YELLOW << "║  " << RESET << BOLD << "4. " << WHITE << "获取在线客户端列表" << RESET << std::setw(17) << YELLOW << "║" << RESET << std::endl;
        std::cout << YELLOW << "║  " << RESET << BOLD << "5. " << MAGENTA << "发送消息到其他客户端" << RESET << std::setw(13) << YELLOW << "║" << RESET << std::endl;
        std::cout << YELLOW << "║  " << RESET << BOLD << "0. " << RED << "结束会话并退出" << RESET << std::setw(21) << YELLOW << "║" << RESET << std::endl;
    }
    std::cout << YELLOW << "╚════════════════════════════════════════════╝" << RESET << std::endl;
    std::cout << CYAN << BOLD << "BX-Protocol@Client> " << RESET;
}

// 信号处理
void exitHandler(int signal) {
    std::cout << RED << "\n[!] 捕获中断信号，正在关闭..." << RESET << std::endl;
    shouldExit = true;
    if (globalSocket) {
        globalSocket->close();
    }
}

// --- 逻辑线程 ---

// 消息呈现线程：负责将接收到的 Packet 渲染到屏幕
void messagePresenter() {
    while (!shouldExit) {
        std::unique_lock<std::mutex> lock(msgMutex);
        msgCondition.wait(lock, []{ return !msgQueue.empty() || shouldExit; });
        
        if (shouldExit && msgQueue.empty()) break;
        
        while (!msgQueue.empty()) {
            Packet pkt = msgQueue.front();
            msgQueue.pop();
            lock.unlock();
            
            // 根据消息类型着色输出
            switch (pkt.getType()) {
                case MessageType::RESP_CONNECT:
                    std::cout << "\n" << GREEN << BOLD << "✔ [Server] " << RESET << pkt.data << std::endl;
                    break;
                    
                case MessageType::RESP_TIME:
                    if (isBatchTimeRequest) {
                        timeResponseCount++;
                        // 批量模式：只显示部分响应，避免刷屏
                        if (timeResponseCount <= 5 || timeResponseCount % 20 == 0 || timeResponseCount >= 96) {
                            std::cout << "\n" << BLUE << BOLD << "🕒 [Time #" << timeResponseCount << "] "
                                     << RESET << pkt.data << std::endl;
                        }
                    } else {
                        std::cout << "\n" << BLUE << BOLD << "🕒 [Time] " << RESET << pkt.data << std::endl;
                    }
                    break;
                    
                case MessageType::RESP_NAME:
                    std::cout << "\n" << MAGENTA << BOLD << "🏷 [Name] " << RESET << pkt.data << std::endl;
                    break;
                    
                case MessageType::RESP_CLIENTS:
                    std::cout << "\n" << CYAN << BOLD << "👥 [Client List]" << RESET << "\n" << pkt.data << std::endl;
                    break;

                case MessageType::RESP_SEND_RESULT:
                    std::cout << "\n" << GREEN << BOLD << "📤 [Send Result] " << RESET << pkt.data << std::endl;
                    break;

                case MessageType::NOTIFY_MSG:
                    std::cout << "\n" << YELLOW << BOLD << "💬 [New Message] " << RESET << pkt.data << std::endl;
                    break;

                case MessageType::NOTIFY_DISCONNECT:
                    std::cout << "\n" << RED << BOLD << "✘ [System] " << RESET << "服务器强制断开连接" << std::endl;
                    isConnected = false;
                    break;
                
                    
                default:
                    std::cout << "\n" << RED << "[?] 收到未知协议包" << RESET << std::endl;
                    break;
            }
            
            std::cout << CYAN << BOLD << "BX-Protocol@Client> " << RESET << std::flush;
            lock.lock();
        }
    }
}

// 接收消息线程：负责从 Socket 读取数据并放入队列
void receiveMessages(SocketWrapper* sock) {
    while (!shouldExit && isConnected) {
        Packet pkt;
        if (!sock->recv(pkt)) {
            if (!shouldExit) {
                std::cout << RED << "\n[!] 错误：与服务器的连接已意外中断" << RESET << std::endl;
                isConnected = false;
            }
            break;
        }
        
        {
            std::lock_guard<std::mutex> lock(msgMutex);
            msgQueue.push(pkt);
        }
        msgCondition.notify_all();
    }
}

// --- 主程序 ---

int main(int argc, char* argv[]) {
    signal(SIGINT, exitHandler);
    signal(SIGTERM, exitHandler);
    
    showBanner();
    
    std::thread presenterThread(messagePresenter);
    std::thread* receiverThread = nullptr;
    SocketWrapper* clientSocket = nullptr;
    
    while (!shouldExit) {
        showMenu();
        
        std::string input;
        if (!std::getline(std::cin, input)) break;
        if (shouldExit) break;
        
        int choice = -1;
        try {
            choice = std::stoi(input);
        } catch (...) {
            std::cout << RED << " [!] 输入非法：请输入菜单对应的数字。" << RESET << std::endl;
            continue;
        }
        
        if (!isConnected) {
            if (choice == 1) {
                std::cout << BOLD << "请输入服务器 IP [" << WHITE << "127.0.0.1" << RESET << BOLD << "]: " << RESET;
                std::string ip;
                std::getline(std::cin, ip);
                if (ip.empty()) ip = "127.0.0.1";
                
                std::cout << BOLD << "请输入端口 [" << WHITE << "4703" << RESET << BOLD << "]: " << RESET;
                std::string portStr;
                std::getline(std::cin, portStr);
                int port = portStr.empty() ? 4703 : std::stoi(portStr);
                
                std::cout << YELLOW << " [*] 正在尝试建立 TCP 连接..." << RESET << std::endl;

                int sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    std::cerr << RED << " [Error] 无法创建 Socket 句柄。" << RESET << std::endl;
                    continue;
                }
                
                struct sockaddr_in serverAddress;
                memset(&serverAddress, 0, sizeof(serverAddress));
                serverAddress.sin_family = AF_INET;
                serverAddress.sin_port = htons(port);
                
                if (inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr) <= 0) {
                    std::cerr << RED << " [Error] IP 地址格式错误。" << RESET << std::endl;
                    close(sockfd);
                    continue;
                }
                
                if (connect(sockfd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
                    std::cerr << RED << " [Error] 连接失败，请检查服务器是否开启。" << RESET << std::endl;
                    close(sockfd);
                    continue;
                }
                
                clientSocket = new SocketWrapper(sockfd);
                globalSocket = clientSocket;
                isConnected = true;
                
                std::cout << GREEN << BOLD << " [+] 成功连接至集群: " << ip << ":" << port << RESET << std::endl;
                
                receiverThread = new std::thread(receiveMessages, clientSocket);
            } else if (choice == 0) {
                shouldExit = true;
            }
        } else {
            Packet request;
            switch (choice) {
                case 1: // 断开连接
                    request = Packet(MessageType::REQ_DISCONNECT);
                    clientSocket->send(request);
                    isConnected = false;
                    
                    if (receiverThread && receiverThread->joinable()) {
                        receiverThread->join();
                        delete receiverThread;
                        receiverThread = nullptr;
                    }
                    delete clientSocket;
                    clientSocket = nullptr;
                    globalSocket = nullptr;
                    std::cout << YELLOW << " [-] 已安全断开连接。" << RESET << std::endl;
                    break;
                    
                case 2: { // 获取时间
                    std::cout << BOLD << "是否批量发送100次请求? (y/n) [n]: " << RESET;
                    std::string batchInput;
                    std::getline(std::cin, batchInput);

                    if (batchInput == "y" || batchInput == "Y") {
                        // 批量模式
                        isBatchTimeRequest = true;
                        timeResponseCount = 0;

                        std::cout << YELLOW << " [*] 正在发送100次时间请求..." << RESET << std::endl;
                        auto startTime = std::chrono::high_resolution_clock::now();

                        for (int i = 0; i < 100; i++) {
                            clientSocket->send(Packet(MessageType::REQ_GET_TIME));
                        }

                        auto endTime = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

                        std::cout << GREEN << " [+] 已发送100次请求，耗时 " << duration.count() << " ms" << RESET << std::endl;
                        std::cout << YELLOW << " [*] 等待响应中..." << RESET << std::endl;

                        // 等待所有响应（最多等待5秒）
                        std::this_thread::sleep_for(std::chrono::seconds(5));

                        std::cout << CYAN << BOLD << "\n========== 统计结果 ==========" << RESET << std::endl;
                        std::cout << GREEN << " 发送请求数: 100" << RESET << std::endl;
                        std::cout << GREEN << " 收到响应数: " << timeResponseCount << RESET << std::endl;

                        if (timeResponseCount == 100) {
                            std::cout << GREEN << BOLD << " ✓ 所有响应均已收到！" << RESET << std::endl;
                        } else {
                            std::cout << YELLOW << " ⚠ 响应数量不完整 (丢失 " << (100 - timeResponseCount.load()) << " 个)" << RESET << std::endl;
                        }
                        std::cout << CYAN << BOLD << "=============================" << RESET << std::endl;

                        isBatchTimeRequest = false;
                    } else {
                        // 单次模式
                        clientSocket->send(Packet(MessageType::REQ_GET_TIME));
                    }
                    break;
                }
                case 3:
                    clientSocket->send(Packet(MessageType::REQ_GET_NAME));
                    break;
                case 4:
                    clientSocket->send(Packet(MessageType::REQ_GET_CLIENTS));
                    break;

                case 5: { // 发送消息
                    std::cout << BOLD << "请输入目标客户端编号: " << RESET;
                    std::string targetIdStr;
                    std::getline(std::cin, targetIdStr);

                    if (targetIdStr.empty()) {
                        std::cout << RED << " [!] 编号不能为空。" << RESET << std::endl;
                        break;
                    }

                    std::cout << BOLD << "请输入要发送的消息: " << RESET;
                    std::string message;
                    std::getline(std::cin, message);

                    if (message.empty()) {
                        std::cout << RED << " [!] 消息不能为空。" << RESET << std::endl;
                        break;
                    }

                    // 组装数据：编号|消息内容
                    std::string payload = targetIdStr + "|" + message;
                    clientSocket->send(Packet(MessageType::REQ_SEND_MSG, payload));
                    std::cout << YELLOW << " [*] 消息已发送，等待确认..." << RESET << std::endl;
                    break;
                }

                case 0:
                    if (isConnected) {
                        clientSocket->send(Packet(MessageType::REQ_DISCONNECT));
                    }
                    shouldExit = true;
                    break;
                default:
                    std::cout << RED << " [!] 选项无效。" << RESET << std::endl;
                    break;
            }
        }
    }
    
    // 退出清理
    if (receiverThread && receiverThread->joinable()) {
        receiverThread->join();
        delete receiverThread;
    }
    if (clientSocket) delete clientSocket;
    
    msgCondition.notify_all();
    if (presenterThread.joinable()) presenterThread.join();
    
    std::cout << CYAN << BOLD << "\n[Terminated] 感谢使用，再见！" << RESET << std::endl;
    return 0;
}