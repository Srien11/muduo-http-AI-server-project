## 10. I13 MCP Server 详细设计

### 10.1 背景

MCP (Model Context Protocol) 是 Anthropic 开源的标准化工具调用协议。
你的框架通过实现 MCP Server，可以让任何 MCP 客户端（Claude Desktop、MCP Web Client 等）调用你的 C++ 函数、查你的数据库、操作你的文件系统。

### 10.2 协议基础：JSON-RPC 2.0

MCP 的所有通信基于 JSON-RPC 2.0，三种消息类型：

```
请求 (Request)：
  → {"jsonrpc":"2.0", "id":1, "method":"tools/list", "params":{}}
  ← {"jsonrpc":"2.0", "id":1, "result":{"tools":[...]}}

通知 (Notification)（无 id，无需响应）：
  → {"jsonrpc":"2.0", "method":"notifications/initialized", "params":{}}

错误 (Error)：
  ← {"jsonrpc":"2.0", "id":1, "error":{"code":-32601, "message":"Method not found"}}
```

### 10.3 传输层设计：SSE (Server-Sent Events)

MCP 的 SSE 传输使用双向通信模式：

```
客户端                          你的服务器
  │                                │
  │── GET /mcp (SSE 流) ──────────→│  客户端建立 SSE 连接
  │←─ event: endpoint ─────────────│  服务器返回消息提交端点
  │   data: /mcp/message           │
  │                                │
  │── POST /mcp/message ──────────→│  客户端提交 JSON-RPC 请求
  │   {"id":1, "method":"tools/list"} │
  │                                │
  │←─ event: message ──────────────│  服务器通过 SSE 返回响应
  │   data: {"id":1, "result":...} │
```

在 HttpServer 中注册两个路由：
- `GET /mcp` → SSE 流（长连接）
- `POST /mcp/message` → 接收客户端消息

### 10.4 模块架构

```
┌─────────────────────────────────────────────────────┐
│                    McpServer                        │
│  ┌──────────────┐  ┌──────────────┐                 │
│  │  McpSession  │  │ McpTransport │  ← SSE 收发     │
│  │  (会话状态)   │  │ (传输层)     │                 │
│  └──────┬───────┘  └──────┬───────┘                 │
│         │                  │                         │
│  ┌──────▼──────────────────▼───────┐                 │
│  │        McpRouter                │                 │
│  │  请求分发 → 对应 handler         │                 │
│  └──────┬─────────────────┬───────┘                 │
│         │                  │                         │
│  ┌──────▼──────┐  ┌───────▼────────┐                │
│  │ ToolRegistry │  │ ResourceRegistry│               │
│  │ 工具注册/执行 │  │ 资源暴露/读取  │               │
│  └──────┬───────┘  └───────┬────────┘                │
│         │                  │                         │
│  ┌──────▼──────────────────▼───────┐                 │
│  │     JsonRpcParser               │                 │
│  │     JSON-RPC 消息解析/构建       │                 │
│  └─────────────────────────────────┘                 │
└─────────────────────────────────────────────────────┘
```

### 10.5 模块详解

#### 10.5.1 JsonRpcParser — JSON-RPC 消息层

```cpp
// 消息结构
struct JsonRpcRequest {
    std::string jsonrpc;      // "2.0"
    std::string id;           // 请求ID（字符串化）
    std::string method;       // 方法名
    nlohmann::json params;    // 参数
};

struct JsonRpcResponse {
    std::string jsonrpc;
    std::string id;
    nlohmann::json result;    // 成功结果
};

struct JsonRpcError {
    std::string jsonrpc;
    std::string id;
    int code;
    std::string message;
    nlohmann::json data;      // 可选
};

// 解析器
class JsonRpcParser {
    bool ParseRequest(const std::string& raw, JsonRpcRequest& req);
    std::string BuildResponse(const JsonRpcResponse& resp);
    std::string BuildError(const JsonRpcError& err);
    std::string BuildNotification(const std::string& method, nlohmann::json params);
};
```

**为什么不自研 JSON 解析：** 用我们已有的 `AiGateway` 里的 JSON 字符串拼接方式太脆弱了，MCP 的消息结构复杂得多。这里选择引入一个轻量 JSON 库——**nlohmann/json**（单头文件，MIT 许可，仅需 `#include <json.hpp>`）。

#### 10.5.2 McpTransport — SSE 传输层

```cpp
class McpTransport {
public:
    // 绑定到 SSE 连接
    void Bind(const muduo::net::TcpConnectionPtr& conn);
    
    // 发送 JSON-RPC 消息给客户端
    void SendMessage(const std::string& json_message);
    void SendEvent(const std::string& event, const std::string& data);
    
    // 接收客户端 POST 来的消息
    void OnMessage(const std::string& raw_request);
    
    // 心跳（每 30 秒 ping 一次）
    void StartHeartbeat(muduo::net::EventLoop* loop);
    
    bool IsOpen() const;
    void Close();
};
```

SSE 事件格式：
```
event: message
data: {"jsonrpc":"2.0","id":1,"result":...}
\n
```

#### 10.5.3 McpSession — 会话管理

```cpp
class McpSession {
public:
    enum State { kNew, kInitializing, kInitialized, kClosed };
    
    State state() const;
    void SetInitialized();
    
    // 客户端能力声明（来自 initialize 请求）
    struct ClientCapabilities {
        bool tools = false;
        bool resources = false;
        bool prompts = false;
        bool logging = false;
    };
    ClientCapabilities client_caps;
    std::string client_name;
    std::string client_version;
    
    // 服务端能力
    struct ServerCapabilities {
        bool tools = true;      // 我们会实现这个
        bool resources = true;  // 我们会实现这个
        bool prompts = false;
        bool logging = true;
    };
};
```

#### 10.5.4 ToolRegistry — 工具注册中心

```cpp
// 工具参数定义（JSON Schema）
struct ToolParameter {
    std::string name;
    std::string description;
    std::string type;        // "string", "number", "boolean", "array", "object"
    bool required = false;
    std::vector<std::string> enum_values;  // 可选枚举
};

// 工具定义
struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParameter> parameters;
    
    // 转为 JSON Schema 格式
    nlohmann::json ToJsonSchema() const;
};

// 工具执行结果
struct ToolResult {
    bool success = true;
    std::string content;      // 文本结果
    std::string mime_type = "text/plain";
    bool is_error = false;
};

// 工具执行函数
using ToolHandler = std::function<ToolResult(const nlohmann::json& args)>;

class ToolRegistry {
public:
    void Register(const ToolDefinition& def, ToolHandler handler);
    bool Unregister(const std::string& name);
    
    // 列出所有工具（用于 tools/list）
    std::vector<ToolDefinition> ListTools() const;
    
    // 调用工具（用于 tools/call）
    ToolResult CallTool(const std::string& name, const nlohmann::json& args);
    
    // 检查工具是否存在
    bool HasTool(const std::string& name) const;
};
```

#### 10.5.5 McpServer — 主控器

```cpp
class McpServer {
public:
    // 初始化
    void Init(const std::string& server_name, const std::string& server_version);
    
    // 工具注册（代理到 ToolRegistry）
    void RegisterTool(const ToolDefinition& def, ToolHandler handler);
    
    // 路由注册到 HttpServer
    void Mount(muduo_http::HttpServer& http_server);
    // 会在 HttpServer 上注册：
    //   GET /mcp  → SSE 长连接
    //   POST /mcp/message → 接收客户端消息
    
private:
    // JSON-RPC 方法分发
    void HandleInitialize(McpTransport& transport, const JsonRpcRequest& req);
    void HandleToolsList(McpTransport& transport, const JsonRpcRequest& req);
    void HandleToolsCall(McpTransport& transport, const JsonRpcRequest& req);
    void HandleResourcesList(McpTransport& transport, const JsonRpcRequest& req);
    void HandleResourcesRead(McpTransport& transport, const JsonRpcRequest& req);
    void HandlePing(McpTransport& transport, const JsonRpcRequest& req);
    void HandleUnknown(McpTransport& transport, const JsonRpcRequest& req);
};
```

### 10.6 MCP 协议方法清单

初始化阶段：
| 方法 | 方向 | 说明 |
|------|------|------|
| `initialize` | Client → Server | 握手，交换版本和能力 |
| `notifications/initialized` | Client → Server | 客户端初始化完成 |

工具（我们重点实现）：
| 方法 | 方向 | 说明 |
|------|------|------|
| `tools/list` | Client → Server | 列出所有可用工具 |
| `tools/call` | Client → Server | 调用指定工具 |

资源（可选）：
| 方法 | 方向 | 说明 |
|------|------|------|
| `resources/list` | Client → Server | 列出所有资源 |
| `resources/read` | Client → Server | 读取指定资源 |
| `resources/subscribe` | Client → Server | 订阅资源变更 |
| `notifications/resources/list_changed` | Server → Client | 资源列表变更通知 |

其他：
| `ping` | 双向 | 心跳检查 |
| `logging/setLevel` | Client → Server | 设置日志级别 |

### 10.7 内置工具（预注册）

| 工具名 | 功能 | 参数 |
|--------|------|------|
| `echo` | 测试回显 | `message: string` |
| `read_file` | 读取文件 | `path: string` |
| `write_file` | 写入文件 | `path: string, content: string` |
| `list_directory` | 列出目录 | `path: string, pattern: string(可选)` |
| `file_info` | 文件信息 | `path: string` |
| `system_info` | 系统信息 | 无 |

### 10.8 完整请求/响应示例

```
客户端 → 服务器（POST /mcp/message）：
{
  "jsonrpc": "2.0",
  "id": "1",
  "method": "tools/call",
  "params": {
    "name": "read_file",
    "arguments": {
      "path": "/home/user/notes.txt"
    }
  }
}

服务器 → 客户端（SSE event: message）：
{
  "jsonrpc": "2.0",
  "id": "1",
  "result": {
    "content": [
      {
        "type": "text",
        "text": "文件内容：\n这是今天的备忘录..."
      }
    ],
    "isError": false
  }
}
```

### 10.9 需要引入的依赖

| 依赖 | 原因 | 替代方案 |
|------|------|----------|
| **nlohmann/json**（单头文件） | JSON-RPC 消息构建/解析 | 自解析（脆弱、易错） |
| (无其他依赖) | | |

### 10.10 文件清单

```
新文件：
  include/http/mcp/mcp_server.h        ← MCP 主控器
  include/http/mcp/mcp_transport.h     ← SSE 传输层
  include/http/mcp/mcp_protocol.h      ← JSON-RPC 消息定义
  include/http/mcp/mcp_session.h       ← 会话状态
  include/http/mcp/mcp_tool.h          ← 工具定义 + 注册中心
  include/http/mcp/mcp_resource.h      ← 资源定义（可选）
  src/http/mcp/mcp_server.cpp
  src/http/mcp/mcp_transport.cpp
  src/http/mcp/mcp_protocol.cpp
  src/http/mcp/mcp_session.cpp
  src/http/mcp/mcp_tool.cpp
  src/http/mcp/mcp_resource.cpp

修改文件：
  CMakeLists.txt           ← 添加新文件 + 链接 nlohmann/json
  src/main.cpp             ← 注册 MCP Server + 注册内置工具
```

### 10.11 实现顺序

```
Step 1: 引入 nlohmann/json 单头文件
Step 2: McpProtocol - JSON-RPC 消息解析/构建
Step 3: McpSession - 会话状态管理
Step 4: McpTool - 工具定义 + ToolRegistry
Step 5: McpTransport - SSE 传输层
Step 6: McpServer - 主控制器（方法分发 + 协议握手）
Step 7: 内置工具注册（echo, read_file, write_file, list_directory...）
Step 8: main.cpp 挂载到 HttpServer
Step 9: 端到端测试
```

### 10.12 测试方法

```
# 用 curl 模拟 MCP 客户端
# 1. 连接 SSE
curl -N http://localhost:8080/mcp

# 2. 另一个终端发 initialize
curl -X POST http://localhost:8080/mcp/message \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"init","method":"initialize",\
       "params":{"protocolVersion":"2024-11-05",\
       "capabilities":{}}}'

# 3. 列出工具
curl -X POST http://localhost:8080/mcp/message \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"2","method":"tools/list","params":{}}'

# 4. 调用工具
curl -X POST http://localhost:8080/mcp/message \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"3","method":"tools/call",\
       "params":{"name":"echo","arguments":{"message":"hello"}}}'
```

### 10.13 MCP 后续扩展方向

完成 I13 基础 MCP Server 后，你可以：
1. 注册任意 C++ 函数为工具（结合 I14 多模型路由）
2. 通过 MCP 暴露 RAG 查询能力（结合 I15）
3. 通过 MCP 暴露长期记忆（结合 I16）
4. 写一个 Web MCP Client（I19），浏览器直接连你的 MCP Server
