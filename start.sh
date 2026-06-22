#!/bin/bash
echo ""
echo "=============================="
echo "   知墨 AI 助手 - 启动器"
echo "=============================="
echo ""
if ! command -v docker &>/dev/null; then
    echo "请先安装 Docker"
    exit 1
fi
if [ ! -f server.conf ]; then
    cp server.conf.example server.conf
    echo "已创建 server.conf，请编辑后重试"
    exit 1
fi
docker compose pull && docker compose up -d
echo "完成！访问 http://localhost:8080"
