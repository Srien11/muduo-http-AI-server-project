#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "http/ai_gateway.h"
#include "http/config_manager.h"
#include "http/db_connection_pool.h"
#include "http/graceful_shutdown.h"
#include "http/http_response.h"
#include "http/http_server.h"
#include "http/https_server.h"
#include "http/log_manager.h"
#include "http/mcp/mcp_protocol.h"
#include "http/mcp/mcp_server.h"
#include "http/middleware.h"
#include "http/multipart_parser.h"
#include "http/static_file_handler.h"
#include "http/stream_writer.h"

int main(int argc, char* argv[]) {
    // Load config
    muduo_http::ConfigManager cfg;
    std::string config_file = "server.conf";
    if (argc > 1) config_file = argv[1];
    cfg.Load(config_file);

    // Setup logging
    auto& log = muduo_http::LogManager::Instance();
    std::string log_level = cfg.Get("log.level", "info");
    if (log_level == "debug") log.SetLevel(muduo_http::LogLevel::kDebug);
    else if (log_level == "warn") log.SetLevel(muduo_http::LogLevel::kWarn);
    else if (log_level == "error") log.SetLevel(muduo_http::LogLevel::kError);
    std::string log_file = cfg.Get("log.file", "");
    if (!log_file.empty()) log.SetFile(log_file);
    log.SetConsole(true);

    LOG_INFO("server starting...");

    // Create server from config
    int port = cfg.GetInt("server.port", 8080);
    int threads = cfg.GetInt("server.threads", 4);

    muduo_http::HttpServer server(port);
    server.set_thread_num(threads);
    server.set_max_body_size(static_cast<size_t>(cfg.GetInt("server.max_body_size", 1048576)));

    // Session middleware is registered automatically by HttpServer
    server.Use(muduo_http::CreateLoggingMiddleware());
    server.Use(muduo_http::CreateCorsMiddleware());

    // Static routes
    server.routes().Get("/", [](const muduo_http::HttpRequest&, muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("Hello from muduo_http router.\n");
    });

    server.routes().Get("/health", [](const muduo_http::HttpRequest&, muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("ok\n");
    });

    // Dynamic routes
    server.routes().Get("/users/:id", [](const muduo_http::HttpRequest& request,
                                           muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("user id: " + request.path_params.at("id") + "\n");
    });

    // POST route
    server.routes().Post("/echo", [](const muduo_http::HttpRequest& request,
                                      muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("echo: " + request.body + "\n");
    });

    // PUT route
    server.routes().Put("/users/:id", [](const muduo_http::HttpRequest& request,
                                           muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("updated user " + request.path_params.at("id") + "\n");
    });

    // DELETE route
    server.routes().Delete("/users/:id", [](const muduo_http::HttpRequest& request,
                                              muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("deleted user " + request.path_params.at("id") + "\n");
    });

    // 500 error demo
    server.routes().Get("/crash", [](const muduo_http::HttpRequest&,
                                      muduo_http::HttpResponse&) {
        throw std::runtime_error("simulated crash");
    });

    // ----- Session demo -----
    // POST /session/login - set username in session
    server.routes().Post("/session/login", [](const muduo_http::HttpRequest& request,
                                               muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        if (!request.session) {
            response.SetBody("session not available\n");
            return;
        }
        // Use the request body as username
        request.session->Set("username", request.body.empty() ? "anonymous" : request.body);
        response.SetBody("logged in as " + request.session->Get("username") + "\n");
    });

    // GET /session/me - return current user info (requires login)
    server.routes().Get("/session/me", [](const muduo_http::HttpRequest& request,
                                           muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        if (!request.session || !request.session->Has("username")) {
            response.SetStatusCode(401);
            response.SetBody("401 Unauthorized - please POST /session/login first\n");
            return;
        }
        response.SetBody("current user: " + request.session->Get("username") +
                         " (session: " + request.session->id().substr(0, 8) + "...)\n");
    });

    // POST /session/logout - clear session data
    server.routes().Post("/session/logout", [](const muduo_http::HttpRequest& request,
                                                muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        if (request.session) {
            request.session->Clear();
        }
        response.SetBody("logged out\n");
    });

    // ----- SSE / Streaming demo -----
    // GET /events - SSE stream (simulated AI token output)
    server.routes().Get("/events", [](const muduo_http::HttpRequest& request,
                                       muduo_http::HttpResponse&) {
        auto writer = std::make_shared<muduo_http::StreamWriter>(
            request.stream_conn, 200, "OK", "text/event-stream");
        request.stream = writer;

        // Send a series of SSE events
        writer->WriteSSE("connected");
        writer->WriteSSE("message", "Hello from SSE!");
        writer->WriteSSE("update", "This is a streaming response.");
        writer->WriteSSE("message", "Supports Chunked Transfer Encoding.");
        writer->WriteSSE("done");

        writer->End();
    });

    // GET /events/stream - slow simulated AI token stream
    server.routes().Get("/events/stream", [](const muduo_http::HttpRequest& request,
                                              muduo_http::HttpResponse&) {
        auto writer = std::make_shared<muduo_http::StreamWriter>(
            request.stream_conn, 200, "OK", "text/event-stream");
        request.stream = writer;

        writer->WriteSSE("stream", "Starting simulated AI output...\n");

        const char* tokens[] = {"Hello", ", ", "this", " is", " a", " simulated", " AI", " response", ".", NULL};
        for (int i = 0; tokens[i] != NULL; ++i) {
            writer->WriteSSE("token", tokens[i]);
            muduo::net::EventLoop* loop = nullptr; // Simulated delay - in real app use timer
        }

        writer->WriteSSE("done", "Stream complete.");
        writer->End();
    });

    // ----- DB demo (pool status check) -----
    server.routes().Get("/db/pool", [](const muduo_http::HttpRequest&,
                                        muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("DbConnectionPool ready (requires MySQL)\n");
    });

    // ----- AI Gateway demo -----
    muduo_http::AiConfig ai_cfg;
    ai_cfg.api_base = cfg.Get("ai.api_base", "https://api.openai.com/v1");
    ai_cfg.model = cfg.Get("ai.model", "gpt-3.5-turbo");
    ai_cfg.rate_limit_rps = cfg.GetInt("ai.rate_limit_rps", 10);
    ai_cfg.cache_enabled = cfg.GetBool("ai.cache_enabled", true);

    auto ai_gateway = std::make_shared<muduo_http::AiGateway>(ai_cfg);

    // GET /ai/health - check gateway status
    server.routes().Get("/ai/health", [](const muduo_http::HttpRequest&,
                                          muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("AI Gateway ready (set OPENAI_API_KEY to use)\n");
    });

    // POST /ai/chat - synchronous chat completion
    server.routes().Post("/ai/chat", [ai_gateway](const muduo_http::HttpRequest& req,
                                                   muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");

        if (ai_gateway->config().api_key.empty()) {
            response.SetBody("Set OPENAI_API_KEY environment variable first\n");
            return;
        }

        muduo_http::AiChatRequest chat_req;
        chat_req.messages.push_back({"user", req.body.empty() ? "Say hello" : req.body});
        chat_req.stream = false;

        auto result = ai_gateway->Chat(chat_req);
        if (result.success) {
            response.SetBody(result.content + "\n");
        } else {
            response.SetStatusCode(502);
            response.SetBody("AI error: " + result.error_message + "\n");
        }
    });

    // GET /ai/chat/stream - streaming chat via SSE
    server.routes().Get("/ai/chat/stream", [ai_gateway](const muduo_http::HttpRequest& req,
                                                         muduo_http::HttpResponse&) {
        auto writer = std::make_shared<muduo_http::StreamWriter>(
            req.stream_conn, 200, "OK", "text/event-stream");
        req.stream = writer;

        writer->WriteSSE("info", "streaming AI chat (requires OPENAI_API_KEY)");

        if (ai_gateway->config().api_key.empty()) {
            writer->WriteSSE("error", "OPENAI_API_KEY not set");
            writer->WriteSSE("done", "failed");
            writer->End();
            return;
        }

        muduo_http::AiChatRequest chat_req;
        chat_req.messages.push_back({"user", "Tell me a short joke"});
        chat_req.stream = true;

        ai_gateway->ChatStream(chat_req, *writer);
    });

    // ----- File upload demo -----
    server.routes().Post("/upload", [](const muduo_http::HttpRequest& req,
                                        muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");

        auto ct_it = req.headers.find("Content-Type");
        if (ct_it == req.headers.end()) {
            response.SetBody("Missing Content-Type\n");
            return;
        }

        std::string boundary = muduo_http::ExtractBoundary(ct_it->second);
        if (boundary.empty()) {
            response.SetBody("Invalid Content-Type (not multipart)\n");
            return;
        }

        muduo_http::MultipartParser parser;
        if (!parser.Parse(req.body, boundary)) {
            response.SetBody("Failed to parse multipart data\n");
            return;
        }

        std::string result;
        result += "Parsed " + std::to_string(parser.fields().size()) + " parts:\n\n";

        for (const auto& field : parser.fields()) {
            result += "  field: " + field.name;
            if (!field.filename.empty()) {
                result += " (file: " + field.filename + ", " +
                          std::to_string(field.data.size()) + " bytes)";
            } else {
                result += " = " + field.value;
            }
            result += "\n";
        }

        response.SetBody(result);
    });

    // ----- Static file service -----
    auto file_handler = std::make_shared<muduo_http::StaticFileHandler>("www");

    // Catch-all: serve static files for non-API paths
    // Must be registered after all API routes
    server.routes().Get("/:path", [file_handler](const muduo_http::HttpRequest& req,
                                                  muduo_http::HttpResponse& response) {
        std::string path = req.path_params.at("path");
        if (!file_handler->Serve("/" + path, response)) {
            response.SetStatusCode(404);
            response.SetStatusMessage("Not Found");
            response.SetBody("404 Not Found\n");
        }
    });

    // ----- Start HTTPS server -----
    std::string https_cert = cfg.Get("https.cert_file", "certs/server.crt");
    std::string https_key = cfg.Get("https.key_file", "certs/server.key");
    int https_port = cfg.GetInt("https.port", 8443);

    muduo_http::HttpsServer https_server(https_port, https_cert, https_key);
    https_server.Use(muduo_http::CreateLoggingMiddleware());
    https_server.Use(muduo_http::CreateCorsMiddleware());

    https_server.routes().Get("/", [](const muduo_http::HttpRequest&, muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("Hello from muduo_https router.\n");
    });
    https_server.routes().Get("/health", [](const muduo_http::HttpRequest&, muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("ok\n");
    });

    https_server.Start();

    // ----- MCP Server -----
    auto mcp_server = std::make_shared<muduo_http::mcp::McpServer>();
    mcp_server->set_event_loop(server.get_loop());

    // Register built-in tools
    // echo - test tool
    mcp_server->RegisterTool(
        {"echo", "Echo back the input message",
         {{"message", "Message to echo back", "string", true}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            std::string msg = args.value("message", "");
            auto result = muduo_http::mcp::ToolResult{};
            result.content.push_back(
                muduo_http::mcp::McpProtocol::TextContent(msg));
            return result;
        });

    // system_info - get system information
    mcp_server->RegisterTool(
        {"system_info", "Get system information (OS, CPU, memory, uptime)",
         {}},
        [](const nlohmann::json&) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            result.content.push_back(
                muduo_http::mcp::McpProtocol::TextContent(
                    "OS: Linux (WSL)\nCPU: x86_64\nRAM: N/A\nUptime: N/A\n"));
            return result;
        });

    // read_file - read file contents
    mcp_server->RegisterTool(
        {"read_file", "Read the contents of a file",
         {{"path", "Absolute path to the file", "string", true}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string path = args.value("path", "");
            // Security: restrict to home directory
            if (path.find("..") != std::string::npos || path[0] != '/') {
                result.content.push_back(
                    muduo_http::mcp::McpProtocol::TextContent("Error: invalid path"));
                result.is_error = true;
                return result;
            }
            std::ifstream file(path);
            if (!file.is_open()) {
                result.content.push_back(
                    muduo_http::mcp::McpProtocol::TextContent("Error: cannot open file"));
                result.is_error = true;
                return result;
            }
            std::ostringstream buf;
            buf << file.rdbuf();
            result.content.push_back(
                muduo_http::mcp::McpProtocol::TextContent(buf.str()));
            return result;
        });

    // list_directory - list files in a directory
    mcp_server->RegisterTool(
        {"list_directory", "List files in a directory",
         {{"path", "Absolute path to the directory", "string", true}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string path = args.value("path", "");
            if (path.find("..") != std::string::npos || path[0] != '/') {
                result.content.push_back(
                    muduo_http::mcp::McpProtocol::TextContent("Error: invalid path"));
                result.is_error = true;
                return result;
            }
            std::string cmd = "ls -la " + path + " 2>&1";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) {
                result.content.push_back(
                    muduo_http::mcp::McpProtocol::TextContent("Error: cannot list directory"));
                result.is_error = true;
                return result;
            }
            char buf[1024];
            std::string output;
            while (fgets(buf, sizeof(buf), pipe)) output += buf;
            pclose(pipe);
            result.content.push_back(
                muduo_http::mcp::McpProtocol::TextContent(output));
            return result;
        });

    // Mount MCP server routes to HTTP server
    // GET /mcp - SSE stream (for persistent MCP connections)
    server.routes().Get("/mcp", [mcp_server](const muduo_http::HttpRequest& req,
                                              muduo_http::HttpResponse&) {
        auto writer = std::make_shared<muduo_http::StreamWriter>(
            req.stream_conn, 200, "OK", "text/event-stream");
        req.stream = writer;
        writer->WriteSSE("endpoint", "/mcp/message");
    });

    // POST /mcp/message - Process JSON-RPC and return response directly
    server.routes().Post("/mcp/message", [mcp_server](const muduo_http::HttpRequest& req,
                                                       muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        response.SetHeader("Cache-Control", "no-cache");

        // Parse JSON-RPC request
        muduo_http::mcp::JsonRpcRequest json_req;
        if (!muduo_http::mcp::McpProtocol::ParseRequest(req.body, json_req)) {
            std::string err = muduo_http::mcp::McpProtocol::MakeError(
                "", muduo_http::mcp::ErrorCode::kParseError, "Parse error");
            response.SetBody(err);
            return;
        }

        // Process based on method
        std::string resp;
        if (json_req.method == "initialize") {
            // Build initialize response
            nlohmann::json result = {
                {"protocolVersion", "2024-11-05"},
                {"serverInfo", {{"name", "muduo_mcp"}, {"version", "0.1.0"}}},
                {"capabilities", {{"tools", true}, {"logging", true}}}
            };
            resp = muduo_http::mcp::McpProtocol::MakeResponse(json_req.id, result);
        } else if (json_req.method == "ping") {
            resp = muduo_http::mcp::McpProtocol::MakeResponse(
                json_req.id, nlohmann::json::object());
        } else if (json_req.method == "tools/list") {
            auto tools = mcp_server->tools().ListTools();
            nlohmann::json tools_json = nlohmann::json::array();
            for (const auto& tool : tools) {
                tools_json.push_back(tool.ToJson());
            }
            resp = muduo_http::mcp::McpProtocol::MakeResponse(
                json_req.id, {{"tools", tools_json}});
        } else if (json_req.method == "tools/call") {
            std::string name = json_req.params.value("name", "");
            nlohmann::json args = json_req.params.value("arguments", nlohmann::json::object());
            auto result = mcp_server->tools().CallTool(name, args);
            nlohmann::json content_json = nlohmann::json::array();
            for (const auto& c : result.content) {
                content_json.push_back(c);
            }
            resp = muduo_http::mcp::McpProtocol::MakeResponse(
                json_req.id, {{"content", content_json}, {"isError", result.is_error}});
        } else if (json_req.method == "notifications/initialized") {
            resp = muduo_http::mcp::McpProtocol::MakeResponse(
                json_req.id, nlohmann::json::object());
        } else {
            resp = muduo_http::mcp::McpProtocol::MakeError(
                json_req.id, muduo_http::mcp::ErrorCode::kMethodNotFound,
                "Method not found: " + json_req.method);
        }

        response.SetBody(resp);
    });

    // Graceful shutdown
    int session_timeout = cfg.GetInt("session.timeout", 3600);
    if (session_timeout > 0) {
        server.set_session_timeout(session_timeout);
    }

    LOG_INFO("server ready on port " + std::to_string(port) +
             " (https:" + std::to_string(https_port) +
             ", threads:" + std::to_string(threads) + ")");

    // Register cleanup and install signal handler
    auto& shutdown = muduo_http::GracefulShutdown::Instance();
    shutdown.Register([&https_server]() { https_server.Stop(); });
    shutdown.Install(server.get_loop());

    server.Start();
    return 0;
}
