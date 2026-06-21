#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "http/ai_gateway.h"
#include "http/chat_agent.h"
#include "http/config_manager.h"
#include "http/model_router.h"
#include "http/rag_pipeline.h"
#include "http/db_connection_pool.h"
#include "http/graceful_shutdown.h"
#include "http/http_response.h"
#include "http/http_server.h"
#include "http/https_server.h"
#include "http/log_manager.h"
#include "http/mcp/mcp_protocol.h"
#include "http/mcp/mcp_server.h"
#include "http/memory_manager.h"
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

    // ----- Model Router (multi-provider with fallback) -----
    auto model_router = std::make_shared<muduo_http::ModelRouter>();
    model_router->AddProvider({"openai",
                               cfg.Get("ai.api_base", "https://api.openai.com/v1"),
                               cfg.Get("ai.api_key", ""),
                               cfg.Get("ai.model", "gpt-3.5-turbo"),
                               cfg.GetInt("ai.timeout_seconds", 60),
                               100});

    // GET /router/status - show provider status
    server.routes().Get("/router/status", [model_router](const muduo_http::HttpRequest&,
                                                          muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        auto statuses = model_router->GetStatus();
        nlohmann::json j = nlohmann::json::array();
        for (const auto& s : statuses) {
            j.push_back({{"name", s.name}, {"model", s.model},
                         {"state", s.state}, {"enabled", s.enabled}});
        }
        response.SetBody(j.dump(2));
    });

    // POST /router/chat - chat via router (with fallback)
    server.routes().Post("/router/chat", [model_router](const muduo_http::HttpRequest& req,
                                                         muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string msg = body.value("message", "");
            if (msg.empty()) {
                response.SetBody(nlohmann::json({{"error", "message required"}}).dump());
                return;
            }
            muduo_http::AiChatRequest chat_req;
            chat_req.messages.push_back({"user", msg});
            auto result = model_router->Chat(chat_req);
            if (result.success) {
                response.SetBody(nlohmann::json({{"response", result.content}}).dump());
            } else {
                response.SetStatusCode(502);
                response.SetBody(nlohmann::json({{"error", result.error_message}}).dump());
            }
        } catch (const std::exception& e) {
            response.SetStatusCode(400);
            response.SetBody(nlohmann::json({{"error", std::string("parse: ") + e.what()}}).dump());
        }
    });

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

    // ----- API Stats -----
    server.routes().Get("/api/stats", [&server](const muduo_http::HttpRequest&,
                                                 muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        auto now = std::chrono::system_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - std::chrono::system_clock::from_time_t(server.start_time())).count();

        nlohmann::json j = {
            {"uptime_seconds", uptime},
            {"requests", server.request_count()},
            {"responses", server.response_count()},
            {"active_connections", server.active_connections()},
            {"avg_response_ms", server.avg_response_time_ms()}
        };
        response.SetBody(j.dump(2));
    });

    // ----- API Config (settings) -----
    server.routes().Get("/api/config", [](const muduo_http::HttpRequest&,
                                           muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        std::ifstream conf_file("server.conf");
        muduo_http::ConfigManager live_cfg;
        if (conf_file.is_open()) {
            live_cfg.LoadString(std::string((std::istreambuf_iterator<char>(conf_file)),
                                             std::istreambuf_iterator<char>()));
        }
        nlohmann::json j = {
            {"api_key", live_cfg.Get("ai.api_key", "")},
            {"model", live_cfg.Get("ai.model", "gpt-3.5-turbo")},
            {"api_base", live_cfg.Get("ai.api_base", "https://api.openai.com/v1")}
        };
        response.SetBody(j.dump(2));
    });

    server.routes().Post("/api/config", [ai_gateway](const muduo_http::HttpRequest& req,
                                                      muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        try {
            auto body = nlohmann::json::parse(req.body);
            std::ifstream in("server.conf");
            std::string content((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
            in.close();

            if (body.contains("api_key")) {
                std::string key = body["api_key"].get<std::string>();
                auto pos = content.find("[ai]");
                if (pos != std::string::npos) {
                    auto key_pos = content.find("api_key", pos);
                    auto next_sec = content.find("[", pos + 4);
                    if (key_pos != std::string::npos && key_pos < next_sec) {
                        auto eol = content.find('\n', key_pos);
                        content.replace(key_pos, eol - key_pos, "api_key = " + key);
                    } else {
                        auto ai_eol = content.find('\n', pos) + 1;
                        content.insert(ai_eol, "api_key = " + key + "\n");
                    }
                }
            }

            std::ofstream out("server.conf");
            out << content;

            // Update running gateway
            if (body.contains("api_key")) {
                ai_gateway->SetApiKey(body["api_key"].get<std::string>());
            }
            if (body.contains("model")) {
                ai_gateway->SetModel(body["model"].get<std::string>());
            }

            nlohmann::json resp = {{"status", "saved"}};
            response.SetBody(resp.dump());
        } catch (const std::exception& e) {
            response.SetBody(nlohmann::json({{"error", e.what()}}).dump());
        }
    });

    // ----- Chat Sessions -----
    server.routes().Get("/api/sessions", [](const muduo_http::HttpRequest&,
                                             muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        nlohmann::json sessions = nlohmann::json::array();
        // Scan memory/ directory for saved chat sessions
        DIR* dir = opendir("memory");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name.size() > 5 && name.substr(name.size() - 5) == ".json" &&
                    name.substr(0, 5) == "chat_") {
                    std::string sid = name.substr(5, name.size() - 10);
                    sessions.push_back({{"id", sid}});
                }
            }
            closedir(dir);
        }
        response.SetBody(sessions.dump(2));
    });

    // ----- RAG Pipeline -----
    auto embedder = std::make_shared<muduo_http::Embedder>(ai_gateway);
    auto vector_store = std::make_shared<muduo_http::VectorStore>();
    auto rag_pipeline = std::make_shared<muduo_http::RAGPipeline>(
        embedder, vector_store, ai_gateway);

    // Try to load persisted vectors
    vector_store->Load("vectors.json");

    // POST /rag/query - ask a question with RAG context
    server.routes().Post("/rag/query", [rag_pipeline](const muduo_http::HttpRequest& req,
                                                       muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string question = body.value("question", "");
            if (question.empty()) {
                response.SetBody(nlohmann::json({{"error", "question required"}}).dump());
                return;
            }
            std::string answer = rag_pipeline->Query(question);
            response.SetBody(nlohmann::json({{"answer", answer}}).dump());
        } catch (const std::exception& e) {
            response.SetStatusCode(400);
            response.SetBody(nlohmann::json({{"error", e.what()}}).dump());
        }
    });

    // POST /rag/upload - add document to knowledge base
    server.routes().Post("/rag/upload", [rag_pipeline, vector_store](
        const muduo_http::HttpRequest& req, muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string text = body.value("text", "");
            std::string source = body.value("source", "web");
            if (text.empty()) {
                response.SetBody(nlohmann::json({{"error", "text required"}}).dump());
                return;
            }
            rag_pipeline->AddDocumentChunked(text, source);
            vector_store->Save("vectors.json");
            response.SetBody(nlohmann::json({{"status", "ok"},
                                            {"chunks", vector_store->size()}}).dump());
        } catch (const std::exception& e) {
            response.SetStatusCode(400);
            response.SetBody(nlohmann::json({{"error", e.what()}}).dump());
        }
    });

    // GET /rag/stats - knowledge base statistics
    server.routes().Get("/rag/stats", [vector_store](const muduo_http::HttpRequest&,
                                                      muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        response.SetBody(nlohmann::json({
            {"chunks", vector_store->size()},
            {"dimension", 1536}
        }).dump());
    });

    // ----- AI Chat Agent (LLM + MCP tools loop) -----
    // Build MCP tool definitions as OpenAI tools format
    nlohmann::json agent_tools = nlohmann::json::array();
    auto mcp_tools = mcp_server->tools().ListTools();
    for (const auto& tool : mcp_tools) {
        agent_tools.push_back(tool.ToOpenAITool());
    }

    // Create tool executor that calls MCP tools
    auto tool_executor = [&mcp_server](const std::string& name,
                                        const std::string& args_json) -> std::string {
        try {
            auto args = nlohmann::json::parse(args_json);
            auto result = mcp_server->tools().CallTool(name, args);
            std::string output;
            for (const auto& c : result.content) {
                if (c.contains("text")) {
                    output += c["text"].get<std::string>();
                }
            }
            return output;
        } catch (const std::exception& e) {
            return std::string("Error: ") + e.what();
        }
    };

    // Create chat agent
    auto chat_agent = std::make_shared<muduo_http::ChatAgent>(
        ai_gateway, tool_executor,
        "You are a helpful assistant with access to tools. "
        "Use tools when needed to answer the user's questions. "
        "The available tools are: read_file, list_directory, echo, system_info."
    );

    if (!agent_tools.empty()) {
        chat_agent->SetTools(agent_tools);
    }

    // POST /chat - send a message to the AI agent
    server.routes().Post("/chat", [chat_agent](const muduo_http::HttpRequest& req,
                                                muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        response.SetHeader("Cache-Control", "no-cache");

        try {
            auto body = nlohmann::json::parse(req.body);
            std::string message = body.value("message", "");
            bool clear = body.value("clear", false);

            if (clear) {
                chat_agent->ClearHistory();
                nlohmann::json resp = {{"response", "History cleared."}};
                response.SetBody(resp.dump());
                return;
            }

            if (message.empty()) {
                nlohmann::json resp = {{"error", "Message is required"}};
                response.SetBody(resp.dump());
                return;
            }

            auto result = chat_agent->Process(message);
            if (result.success) {
                nlohmann::json resp = {{"response", result.content}};
                response.SetBody(resp.dump());
            } else {
                nlohmann::json resp = {{"error", result.error_message}};
                response.SetStatusCode(502);
                response.SetBody(resp.dump());
            }
        } catch (const std::exception& e) {
            nlohmann::json resp = {{"error", std::string("parse error: ") + e.what()}};
            response.SetStatusCode(400);
            response.SetBody(resp.dump());
        }
    });

    // ----- Memory-backed Chat (long-term memory) -----
    auto memory_manager = std::make_shared<muduo_http::MemoryManager>(
        chat_agent, ai_gateway, "memory");

    // POST /chat/memory - chat with long-term memory persistence
    server.routes().Post("/chat/memory", [memory_manager, ai_gateway](const muduo_http::HttpRequest& req,
                                                                       muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");

        // Reload config from server.conf (user may have updated via settings UI)
        muduo_http::ConfigManager live_cfg;
        live_cfg.Load("server.conf");
        std::string api_key = live_cfg.Get("ai.api_key", "");
        if (!api_key.empty()) {
            ai_gateway->SetApiKey(api_key);
            ai_gateway->SetModel(live_cfg.Get("ai.model", "gpt-3.5-turbo"));
            std::string base = live_cfg.Get("ai.api_base", "https://api.openai.com/v1");
            // Auto-add /v1 if missing
            if (base.find("/v1") == std::string::npos && base.find("localhost") == std::string::npos) {
                if (base.back() == '/') base += "v1";
                else base += "/v1";
            }
            ai_gateway->SetApiBase(base);
        }

        try {
            auto body = nlohmann::json::parse(req.body);
            std::string message = body.value("message", "");
            std::string session = body.value("session_id", "default");

            if (body.value("clear", false)) {
                memory_manager->ClearSession(session);
                response.SetBody(nlohmann::json({{"response", "Memory cleared."}}).dump());
                return;
            }

            if (message.empty()) {
                response.SetBody(nlohmann::json({{"error", "Message required"}}).dump());
                return;
            }

            auto result = memory_manager->Process(message, session);
            if (result.success) {
                response.SetBody(nlohmann::json({{"response", result.content}}).dump());
            } else {
                response.SetStatusCode(502);
                response.SetBody(nlohmann::json({{"error", result.error_message}}).dump());
            }
        } catch (const std::exception& e) {
            response.SetStatusCode(400);
            response.SetBody(nlohmann::json({{"error", e.what()}}).dump());
        }
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
