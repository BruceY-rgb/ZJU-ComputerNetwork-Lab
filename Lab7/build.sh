#!/bin/bash

# 编译服务端
echo "Compiling server..."
g++ -std=c++17 -o server src/server.cpp -pthread
if [ $? -eq 0 ]; then
    echo "✓ Server compiled successfully"
else
    echo "✗ Server compilation failed"
    exit 1
fi

# 编译客户端
echo "Compiling client..."
g++ -std=c++17 -o client src/client.cpp -pthread
if [ $? -eq 0 ]; then
    echo "✓ Client compiled successfully"
else
    echo "✗ Client compilation failed"
    exit 1
fi

echo ""
echo "Build complete!"
echo "Run './server' to start server"
echo "Run './client' to start client"