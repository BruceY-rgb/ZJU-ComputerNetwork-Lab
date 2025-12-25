# ifndef TOOLS_H
# define TOOLS_H

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
/**
 * 将字符串转换为小写
 */
std::string toLower(const std::string& str) {
    std::string result = str;
    for (char& c : result) {
        c = std::tolower(static_cast<unsigned char>(c));
    }
    return result;
}

/**
 * 去除字符串前后的空白字符
 */
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}


/**
 * 解析HTTP头字段
 * 输入：headers字符串（不包含请求行，只有头字段部分）
 * 输出：map，键为小写的字段名，值为字段值
 * 
 * 示例输入：
 * "Host: example.com\r\n"
 * "Content-Length: 100\r\n"
 * "Content-Type: text/plain\r\n"
 */

 std::map<std::string, std::string>parseHeaders(const std::string& headers) {
    std::map<std::string, std::string> headerMap;
    
    size_t pos = 0;

    while ( pos < headers.size() ){
        // 找到下一个\r\n
        size_t lineEnd = headers.find("\r\n", pos);
        if (lineEnd == std::string::npos) {
            // 处理最后一行结尾没有\r\n的情况
            lineEnd = headers.size();
        }

        std::string line = headers.substr(pos, lineEnd - pos);//提取一行

        //找到行分隔符
        size_t colonPos = line.find(":");
        if(colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);

            key = toLower(trim(key));
            value = trim(value);

            if(!key.empty()) {
                headerMap[key] = value;
            }
        }

        pos = lineEnd + 2;//跳过结尾的\r\n
    }
    return headerMap;
 }

/** 
 * 根据文件扩展名获取Content-Type
 */
std::string getContentType(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if(dotPos == std::string::npos) {
        return "application/octet-stream";//默认类型
    }

    std::string ext = toLower(path.substr(dotPos));
    
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".txt") return "text/plain";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    
    return "application/octet-stream";
}

/**
 * URI映射：将请求URI映射到实际文件路径
 * 根据实验要求的映射表
 */

 std::string mapUriToPath(const std::string& uri) {
    //映射表(定义在函数内部，确保不会被意外修改)
    static std::map<std::string, std::string> UriMap = {
      {"/index.html", "NetLabFramework/assets/html/test.html"},
      {"/index_noimg.html", "NetLabFramework/assets/html/noimg.html"},
      {"/pic.jpg", "NetLabFramework/assets/img/logo.jpg"},
      {"/info/server", "NetLabFramework/assets/txt/test.txt"}
  };

    auto it = UriMap.find(uri);
    if(it != UriMap.end()) {
        return it->second;
    }

    return "";
 }

/**
 * 检查文件是否存在
 */

  bool fileExists(const std::string& path){
    std::ifstream file(path);
    return file.good();
  }
 /**
  * 读取文件内容(二进制文件)
  */

std::string readFile(const std::string& path){
    std::ifstream file(path, std::ios::binary);
    if(!file) {
        return "";
    }

    // 获取文件大小
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    // 读取内容
    std::string content(size, '\0');
    file.read(&content[0], size);

    return content;
}


# endif