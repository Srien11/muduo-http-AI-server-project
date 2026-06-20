# Muduo HTTP Server (AI Extension Project)

从零实现的模块化 C++ HTTP 框架，基于 Muduo 网络库，逐步扩展 AI 应用接入能力。

## 技术栈

| 层级 | 技术 |
|------|------|
| 构建系统 | CMake 3.16+ |
| 语言标准 | C++17 |
| 网络层 | Muduo (Reactor 事件驱动) |
| 数据库 | MySQL 8.0 (libmysqlclient) |
| 传输加密 | OpenSSL 3.0 (TLS 1.2+) |
| 会话存储 | 内存 (`std::unordered_map`) |
| 运行时 | Linux / WSL |

## 项目结构

```
├── CMakeLists.txt           # 构建配置
├── HTTP框架项目技术栈与实施方案.md  # 完整迭代计划与架构文档
├── README.md
├── certs/                   # TLS 证书（自签名）
│   ├── server.crt
│   └── server.key
├── include/http/
│   ├── db_connection_pool.h  # MySQL 连接池
│   ├── http_context.h        # HTTP 请求解析
│   ├── http_request.h        # 请求数据结构
│   ├── http_response.h       # 响应构建与序列化
│   ├── http_server.h         # HTTP 服务端（Muduo TcpServer）
│   ├── https_server.h        # HTTPS 服务端（OpenSSL + 独立线程）
│   ├── middleware.h           # 中间件链
│   ├── router.h               # 路由注册与分发
│   ├── session.h              # 会话管理
│   └── ssl_context.h          # SSL/TLS 上下文
└── src/http/
    ├── db_connection_pool.cpp
    ├── http_context.cpp
    ├── http_response.cpp
    ├── http_server.cpp
    ├── https_server.cpp
    ├── middleware.cpp
    ├── router.cpp
    ├── session.cpp
    └── ssl_context.cpp
```

## 构建与运行

### 依赖安装（Ubuntu / WSL）

```bash
sudo apt-get install -y libmysqlclient-dev libssl-dev
```

### 编译

```bash
cmake -S . -B build
cmake --build build
```

### 启动

```bash
./build/muduo_http_server
```

默认监听：
- HTTP: `0.0.0.0:8080`
- HTTPS: `0.0.0.0:8443`（自签名证书）

### 测试命令

```bash
# HTTP
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/users/42

# HTTPS
curl -sk https://127.0.0.1:8443/
curl -sk https://127.0.0.1:8443/health

# POST / PUT / DELETE
curl -X POST -d "hello" http://127.0.0.1:8080/echo
curl -X PUT http://127.0.0.1:8080/users/7
curl -X DELETE http://127.0.0.1:8080/users/3

# Session (带 Cookie)
curl -c /tmp/cookies.txt -X POST -d "alice" http://127.0.0.1:8080/session/login
curl -b /tmp/cookies.txt http://127.0.0.1:8080/session/me

# 错误码
curl http://127.0.0.1:8080/nonexist          # 404
curl -X POST http://127.0.0.1:8080/health    # 405 + Allow: GET
curl -d "$(dd if=/dev/zero bs=1k count=2048)" http://127.0.0.1:8080/echo  # 413
curl http://127.0.0.1:8080/crash             # 500
```

## API 概览

### HTTP 状态码支持

| 状态码 | 触发条件 |
|--------|----------|
| 200 OK | 正常响应 |
| 204 No Content | CORS OPTIONS 预检 |
| 400 Bad Request | 请求格式错误、非法字符、重复 Content-Length |
| 401 Unauthorized | 会话认证失败 |
| 404 Not Found | 路径不存在 |
| 405 Method Not Allowed | 路径存在但方法不允许（含 Allow 头） |
| 413 Payload Too Large | 请求体超过 1MB（可配置） |
| 500 Internal Server Error | Handler 抛出异常 |
| 505 HTTP Version Not Supported | 非 HTTP/1.0 或 HTTP/1.1 |

### 功能清单

- 静态路由 + 动态路由（`/users/:id`）
- GET / POST / PUT / DELETE
- 中间件链（日志、CORS、Session）
- Session 管理（Cookie-based，内存存储）
- Keep-Alive（HTTP/1.1 默认）
- 请求体大小限制（可配置）
- MySQL 连接池（DBCP 风格）
- HTTPS（OpenSSL TLS 1.2+，独立线程）
- 流式连接管理（Muduo EventLoop）

## 迭代计划

完整计划见 `HTTP框架项目技术栈与实施方案.md`。

已完成里程碑：
- **M1** 可对外提供稳定 HTTP 服务
- **M2** 具备可扩展路由与中间件体系
- **M3** 具备会话与数据持久化能力（Session + DB 连接池）
- **M4** 具备 HTTPS 与完整交付文档

待推进：
- **M5~M6** AI 接入层（SSE 流式、推理网关、多模型路由）

## 开发规范

- 每次只做一个主题
- 每次结束必须保证：可编译、可运行、可提交
- 提交格式：`type(scope): message`
