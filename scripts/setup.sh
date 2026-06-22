#!/bin/bash
# One-click setup script for Ubuntu/Debian/WSL
set -e

echo "=== Installing dependencies ==="
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake \
    libssl-dev \
    libcurl4-openssl-dev \
    libmysqlclient-dev \
    wget

echo "=== Building Muduo from source ==="
cd /tmp
wget -q https://github.com/chenshuo/muduo/archive/refs/tags/v2.0.2.tar.gz -O muduo.tar.gz
tar xzf muduo.tar.gz
cd muduo-2.0.2
cmake -S . -B build
cmake --build build -j$(nproc)
sudo cmake --install build
sudo ldconfig

echo "=== Building project ==="
cd /home/siren/muduo-http-AI-server-project
cp -n server.conf.example server.conf 2>/dev/null || true
cmake -S . -B build
cmake --build build -j$(nproc)

echo "=== Done! ==="
echo "Run: ./build/muduo_http_server"
