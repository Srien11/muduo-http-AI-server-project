@echo off
chcp 65001 >nul
title 知墨 AI 助手
echo.
echo ==============================
echo    知墨 AI 助手 - 启动器
echo ==============================
echo.
docker ps >nul 2>&1
if errorlevel 1 (
    echo [错误] 请先安装 Docker Desktop
    echo https://www.docker.com/products/docker-desktop/
    pause
    exit /b
)
if not exist server.conf (
    copy server.conf.example server.conf >nul
    echo [提示] 已创建 server.conf
    echo  请用记事本打开，填入 API Key 后重试
    start notepad server.conf
    pause
    exit /b
)
echo [1/2] 拉取镜像...
docker compose pull
echo [2/2] 启动服务...
docker compose up -d
echo.
echo 完成！打开 http://localhost:8080
echo.
pause
