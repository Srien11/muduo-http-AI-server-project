# Muduo HTTP 框架技术文档与迭代计划

## 1. 文档目标
本文件聚焦两件事：
1. 明确项目技术栈与模块边界；
2. 给出可执行、可提交、可验收的迭代计划。

> 说明：以当前代码现状为起点，不展开“未来可选优化项”的细节设计。

---

## 2. 技术栈（按层次）

## 2.1 基础层（当前必须）
- **C++17**：核心实现语言
- **CMake**：构建系统
- **Muduo**：高性能网络通信（事件驱动 Reactor）

## 2.2 框架能力层（阶段性接入）
- **HTTP 解析与响应序列化**（自研）
- **路由系统**（静态 + 动态）
- **中间件链**（日志、鉴权、跨域、限流等）
- **会话管理**（Session）

## 2.3 安全与数据层（按里程碑引入）
- **OpenSSL**：HTTPS 能力
- **MySQL + 连接池**：持久化
- （可选）**Redis**：会话/缓存/限流计数

---

## 3. 模块划分（核心）

## 3.1 协议与上下文模块
### `HttpRequest`
- 表示请求数据：method/path/version/headers/body
- 提供后续路由、处理中间件的统一输入

### `HttpResponse`
- 构建响应：status/header/body
- 负责序列化输出为 HTTP 文本

### `HttpContext`
- 管理请求解析状态
- 将原始文本解析为结构化 `HttpRequest`

## 3.2 服务与路由模块
### `HttpServer`
- 核心入口：监听端口、接收连接、读写回调、发送响应
- 对上层暴露注册处理器和中间件的接口

### `Router`
- 路由注册：GET/POST/PUT/DELETE
- 路由匹配：静态路径 + 动态路径（如 `/users/:id`）
- 路由分发：调用对应 handler

## 3.3 可扩展模块（按阶段接入）
### 会话模块
- `Session`、`SessionManager`、`SessionStorage`
- 先内存实现，再扩展 Redis/MySQL

### 中间件模块
- `Middleware`、`MiddlewareChain`
- 典型中间件：日志、CORS、认证、限流、压缩

### 数据库连接池模块
- `DbConnection`、`DbConnectionPool`
- 提供获取连接、回收连接、健康检查接口

### HTTPS 模块
- `SslConfig`、`SslContext`、`SslConnection`
- 在 `HttpServer` 层集成 TLS 握手与加密读写

---

## 4. 当前代码基线（已完成）

当前仓库已具备：
- `HttpResponse` 序列化
- `Router` 静态路由分发（GET/POST + 404）
- `HttpContext` 基础请求解析
- `main.cpp` 演示闭环（构造请求 -> 路由 -> 输出响应）

这意味着项目处于：**“HTTP 核心逻辑已打底，网络层待接入”**。

---

## 5. 迭代计划（建议 8 个迭代）

## Iteration 1：接入 Muduo 网络主链路
**目标**：从“本地字符串演示”升级到“真实 HTTP 服务”。

- 在 `HttpServer::Start()` 接入 Muduo TCP server
- 完成 onConnection / onMessage 回调
- 收包 -> `HttpContext` 解析 -> `Router` -> `HttpResponse` 回包

**验收**：
- `curl http://127.0.0.1:8080/` 可返回有效响应
- 服务稳定运行，不崩溃

**建议提交**：
- `feat(server): 接入 muduo 网络回调并打通请求响应链路`

## Iteration 2：协议健壮性
**目标**：解析可靠、错误可控。

- 完善请求行/请求头/Body 边界处理
- 支持 `Content-Length` 校验
- 标准错误响应：400、404、405、413、500

**验收**：
- 畸形请求不会导致崩溃
- 错误码与错误信息符合预期

**建议提交**：
- `feat(http): 完善请求解析与标准错误响应`

## Iteration 3：路由增强
**目标**：从静态路由扩展到动态路由。

- 支持 `/users/:id` 形式的动态路径
- 提供路径参数提取接口
- 方法不匹配返回 405 + Allow

**验收**：
- 静态与动态路由共存
- 参数在 handler 可读取

**建议提交**：
- `feat(router): 支持动态路由与路径参数提取`

## Iteration 4：中间件机制
**目标**：建立可扩展处理链。

- 设计 `Middleware` 与 `MiddlewareChain`
- 先落地：日志中间件、CORS 中间件
- 支持中间件短路（直接返回响应）

**验收**：
- 多中间件按顺序执行
- 可在中间件中拦截请求

**建议提交**：
- `feat(middleware): 新增中间件链并接入日志与CORS`

## Iteration 5：会话管理
**目标**：支持用户状态保持。

- 实现 `Session` / `SessionManager` / `MemorySessionStorage`
- 集成到 `HttpServer` 请求上下文
- 示例：登录后访问受限接口

**验收**：
- 会话创建、读取、销毁可用
- 登录态可持续

**建议提交**：
- `feat(session): 实现内存会话管理并接入服务链路`

## Iteration 6：数据库连接池
**目标**：具备可扩展持久化基础。

- 实现 `DbConnection` / `DbConnectionPool`
- 支持连接获取、归还、连接健康检查
- 在业务示例中完成最小 CRUD

**验收**：
- 多次请求下连接复用稳定
- 数据写读正确

**建议提交**：
- `feat(db): 新增mysql连接池并完成最小持久化示例`

## Iteration 7：HTTPS
**目标**：支持加密传输。

- 配置 OpenSSL 环境
- 封装 `SslContext` 与 `SslConnection`
- 集成到 `HttpServer` 收发流程

**验收**：
- `https://127.0.0.1:8443` 可访问
- 证书加载与握手成功

**建议提交**：
- `feat(https): 集成openssl并支持https服务`

## Iteration 8：统一收口与文档化
**目标**：形成可展示的完整项目版本。

- 补齐模块 README 与示例
- 补齐基础压测/功能测试说明
- 输出架构图、调用流程图、接口清单

**验收**：
- 一键构建运行
- 文档可指导他人复现

**建议提交**：
- `docs(project): 完善架构文档与模块使用说明`

---

## 6. 里程碑视图（简版）

- **M1（I1~I2）**：可对外提供稳定 HTTP 服务
- **M2（I3~I4）**：具备可扩展路由与中间件体系
- **M3（I5~I6）**：具备会话与数据持久化能力
- **M4（I7~I8）**：具备 HTTPS 与完整交付文档

---

## 7. 每次迭代执行规范

- 每次只做一个主题（一个 iteration）
- 每次结束必须满足：
  - 可编译
  - 可运行
  - 可提交
- 提交信息明确“范围 + 结果”

推荐格式：`type(scope): message`

---

## 8. 下一步建议（立刻开始）

优先执行 **Iteration 1**：
1. 在 `HttpServer::Start()` 接入 Muduo 网络逻辑；
2. 跑通 `curl` 请求；
3. 完成一次提交。

这是当前最关键的分水岭：从“模块演示”进入“真实服务”。
