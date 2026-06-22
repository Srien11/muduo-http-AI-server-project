# Muduo HTTP 框架技术文档与迭代计划

## 1. 文档目标
本文件聚焦两件事：
1. 明确项目技术栈与模块边界；
2. 给出可执行、可提交、可验收的迭代计划。


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

## 6. 里程碑视图

### Phase 1：HTTP 生产化 ✅ 已完成
| 里程碑 | 迭代 | 状态 |
|--------|------|------|
| M1 稳定 HTTP 服务 | I1 Muduo网络 + I2 协议健壮性 | ✅ |
| M2 可扩展路由与中间件 | I3 动态路由 + I4 中间件链 | ✅ |
| M3 会话与持久化 | I5 Session + I6 连接池 | ✅ |
| M4 HTTPS 与文档 | I7 HTTPS + I8 文档收口 | ✅ |

### Phase 2：AI 应用核心 ⏳ 进行中
| 里程碑 | 迭代 | 状态 |
|--------|------|------|
| M5 AI 流式与推理网关 | I9 SSE + I10 推理网关 | ✅ |
| M6 生产化补全 | I11 静态文件/上传 + I12 配置/日志/优雅退出 | ⏳ |
| M7 MCP 协议引擎 | I13 MCP Server（JSON-RPC + 工具注册 + SSE 传输） | ⏳ |
| M8 AI 引擎完成 | I14 多模型路由 + I15 RAG管道 + I16 长期记忆 + I17 记忆管理 | ⏳ |

### Phase 3：高级能力 ⏳
| 里程碑 | 迭代 | 状态 |
|--------|------|------|
| M9 跨语言推理 | I18 Python进程桥 | ⏳ |
| M10 标准协议与部署 | I19 MCP + I20 前端 + I21 Docker/CI | ⏳ |

---

## 7. 每次迭代执行规范

- 每次只做一个主题（一个 iteration）
- 每次结束必须满足：
  - 可编译
  - 可运行
  - 可提交
- 提交信息明确”范围 + 结果”

推荐格式：`type(scope): message`

---

## 8. 下一步建议（立刻开始）

优先执行 **Iteration 1**：
1. 在 `HttpServer::Start()` 接入 Muduo 网络逻辑；
2. 跑通 `curl` 请求；
3. 完成一次提交。

---

## 9. 整体架构设计（分层）

### 9.1 架构总览

```
┌──────────────────────────────────────────────────┐
│                  客户端层                         │
│     浏览器 │ curl │ 第三方服务 │ 移动端            │
└─────────────────────┬────────────────────────────┘
                      │ HTTP/HTTPS
┌─────────────────────▼────────────────────────────┐
│  1. 网关层 (Gateway Layer)                       │
│                                                  │
│  HttpServer (Muduo EventLoop)                    │
│  ├── StaticFileHandler   ← 静态文件服务          │
│  ├── Router              ← 路由分发              │
│  │   ├── ApiRouter       ← /api/* 接口路由       │
│  │   └── PageRouter      ← 页面路由              │
│  ├── MiddlewareChain                              │
│  │   ├── Logging         ← 请求日志              │
│  │   ├── CORS            ← 跨域                  │
│  │   ├── RateLimit       ← IP级限流              │
│  │   ├── Auth            ← 认证鉴权              │
│  │   └── Session         ← 会话管理              │
│  └── ErrorHandler        ← 统一错误页            │
├─────────────────────┬────────────────────────────┤
│  2. AI 引擎层 (AI Engine Layer)                  │
│                                                  │
│  AiGateway                                       │
│  ├── ModelRouter       ← 多模型路由 + 熔断       │
│  │   ├── OpenAI Provider                          │
│  │   ├── Claude Provider                          │
│  │   └── Local Provider                          │
│  ├── ToolExecutor      ← Function Calling 引擎   │
│  │   ├── Tool Registry  ← 工具注册中心           │
│  │   └── Tool Loop      ← 调用-回填循环          │
│  ├── RAGPipeline       ← 检索增强生成           │
│  │   ├── DocumentLoader  ← 文档加载/切分         │
│  │   ├── Embedder        ← 向量化 (调API)         │
│  │   └── VectorStore     ← 向量检索              │
│  └── MemoryManager     ← 记忆管理               │
│      ├── ShortTerm       ← Session (已有)        │
│      └── LongTerm        ← MySQL持久化           │
├─────────────────────┬────────────────────────────┤
│  3. 基础设施层 (Infrastructure Layer)            │
│                                                  │
│  ├── DbConnectionPool   ← MySQL连接池 (已有)     │
│  ├── ConfigManager      ← 配置管理 (JSON/YAML)   │
│  ├── LogManager         ← 结构化日志             │
│  ├── CacheManager       ← 多级缓存 (内存+Redis)  │
│  ├── PythonBridge       ← C++↔Python进程桥       │
│  └── GracefulShutdown   ← 优雅退出               │
├─────────────────────┬────────────────────────────┤
│  4. 部署层 (Deployment Layer)                    │
│                                                  │
│  ├── Dockerfile + docker-compose                 │
│  ├── GitHub Actions CI/CD                        │
│  └── 前端静态页 (Vue/HTML)                       │
└──────────────────────────────────────────────────┘
```

### 9.2 依赖方向（防止石山代码）

**核心规则：上层可以调用下层，下层绝不能调用上层。**

```
客户端 → 网关层 → AI引擎层 → 基础设施层
              ↘        ↘
              基础设施层  基础设施层
```

**具体约束：**
- `HttpServer` 不直接持有 AI 模块，只通过 Router 分发请求到 handler
- `AiGateway` 内部是插件化的，`ModelRouter` / `ToolExecutor` / `RAGPipeline` 彼此独立
- 基础设施层所有模块无业务逻辑，纯工具性质
- 每个模块的头文件只暴露最少接口，实现细节全在 .cpp

### 9.3 模块依赖矩阵

| 模块 | 依赖谁 | 被谁依赖 |
|------|--------|----------|
| HttpServer | muduo, Router, MiddlewareChain | 无（最上层） |
| Router | HttpRequest, HttpResponse | HttpServer |
| MiddlewareChain | HttpRequest, HttpResponse | HttpServer |
| Session | muduo::Timestamp | HttpServer, 中间件 |
| AiGateway | libcurl, Config, Cache | 业务 handler |
| ModelRouter | AiGateway (接口), Config | AiGateway |
| McpServer | JSON-RPC, AiGateway, Tool Registry | 业务 handler, 外部 MCP 客户端 |
| ModelRouter | AiGateway (接口), Config | AiGateway |
| RAGPipeline | Embedder, VectorStore | 业务 handler |
| PythonBridge | Unix Socket | 业务 handler |
| DbConnectionPool | MySQL | 业务层 |
| ConfigManager | JSON/YAML | 所有 |
| CacheManager | std::unordered_map | AiGateway |

### 9.4 目录结构规划

```
include/http/           ← 全部对外头文件
src/http/               ← 全部实现文件
├── gateway/            ← 网关层 (HttpServer, Router, Middleware)
├── ai/                 ← AI 引擎层 (AiGateway, ModelRouter, RAG)
├── infra/              ← 基础设施层 (Config, Log, Cache, PythonBridge)
└── deploy/             ← 部署配置
```

### 9.5 迭代路线图（优先序）

根据"先能用、再优化、再智能"的原则：

**Phase 1：生产化补全（HTTP 能上线）**
| 迭代 | 内容 | 原因 |
|------|------|------|
| I11 | 静态文件服务 + Multipart 文件上传 | 没有这些就不能叫 Web 服务器 |
| I12 | 配置文件 + 结构日志 + 优雅退出 | 没有这些就不能上线 |

**Phase 2：AI 核心（能用 AI）**
| I13 | MCP Server 实现 | 标准 MCP 协议（JSON-RPC + 工具注册 + SSE 传输） |
| I14 | 多模型路由 + 熔断降级 | 不绑死单一供应商 |
| I15 | RAG 管道（文档→向量→检索） | 知识库问答 |
| I16 | 长期记忆（对话持久化 MySQL） | 跨会话记忆 |
| I17 | 记忆管理 | 短时+长时融合，上下文窗口管理 |

**Phase 3：高级能力**
| I18 | C++-Python 进程桥 | 绕开 HTTP，本地推理 |
| I19 | MCP Web Client | 浏览器连接 MCP Server |
| I20 | 前端管理页面 | 可视化使用 |
| I21 | Docker + CI/CD 部署 | 让别人也能跑 |

### 9.6 架构保障措施

1. **头文件隔离**：`include/http/gateway/` 中的头文件不引用 `include/http/ai/` 中的任何内容
2. **接口而非实现**：模块间通过抽象接口（纯虚类）通信，通过依赖注入组装
3. **独立编译验证**：每个模块至少一个单元测试，确保改动不破坏其他模块
4. **不可逆依赖**：如果发现下层引用了上层的头文件，立即重构


这是当前最关键的分水岭：从“模块演示”进入“真实服务”。
