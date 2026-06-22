#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

#include <muduo/base/ThreadPool.h>

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
    // Fall back to example if no config file exists
    std::ifstream conf_test(config_file);
    if (!conf_test.is_open()) {
        config_file = "server.conf.example";
    } else {
        conf_test.close();
    }
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
    // read_file - read file contents
    mcp_server->RegisterTool(
        {"read_file", "读取文件内容并返回文本。用于查看代码文件、配置、文档等。不能读取二进制文件。",
         {{"path", "文件路径，支持 /linux/path 或 D:/windows/path 格式", "string", true}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string path = args.value("path", "");
            if (path.find("..") != std::string::npos) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：路径不能包含 .."));
                result.is_error = true; return result;
            }
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：无法打开文件 " + path));
                result.is_error = true; return result;
            }
            std::ostringstream buf; buf << file.rdbuf();
            std::string content = buf.str();
            bool is_binary = false;
            for (size_t i = 0; i < content.size() && i < 4096; i++)
                if ((unsigned char)content[i] == 0) { is_binary = true; break; }
            if (is_binary) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent(
                    "错误：文件是二进制文件（" + std::to_string(content.size()) + " 字节），无法显示"));
                result.is_error = true; return result;
            }
            result.content.push_back(muduo_http::mcp::McpProtocol::TextContent(content));
            return result;
        });

    // write_file - create or overwrite a file
    mcp_server->RegisterTool(
        {"write_file", "写入内容到文件。创建新文件或覆盖已有文件。用于保存代码、配置、文档等。",
         {{"path", "文件路径", "string", true},
          {"content", "要写入的文本内容", "string", true}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string path = args.value("path", "");
            std::string content = args.value("content", "");
            if (path.find("..") != std::string::npos) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：路径不能包含 .."));
                result.is_error = true; return result;
            }
            std::ofstream file(path);
            if (!file.is_open()) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：无法写入文件 " + path));
                result.is_error = true; return result;
            }
            file << content; file.close();
            result.content.push_back(muduo_http::mcp::McpProtocol::TextContent(
                "已写入 " + std::to_string(content.size()) + " 字节到 " + path));
            return result;
        });

    // edit_file - targeted edits in existing file
    mcp_server->RegisterTool(
        {"edit_file", "对已存在的文件进行精确编辑。支持两种模式：1) old_text→new_text 替换 2) 在指定行插入/追加。比 write_file 更适合修改大文件中的局部内容。",
         {{"path", "文件路径", "string", true},
          {"old_text", "要被替换的原文（唯一匹配）", "string", true},
          {"new_text", "替换后的新文本", "string", true}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string path = args.value("path", "");
            std::string old_text = args.value("old_text", "");
            std::string new_text = args.value("new_text", "");
            if (path.find("..") != std::string::npos) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：路径不能包含 .."));
                result.is_error = true; return result;
            }
            std::ifstream fin(path);
            if (!fin.is_open()) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：无法打开文件 " + path));
                result.is_error = true; return result;
            }
            std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
            fin.close();
            auto pos = content.find(old_text);
            if (pos == std::string::npos) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：在文件中未找到匹配的原文。请确保 old_text 是精确匹配。"));
                result.is_error = true; return result;
            }
            content.replace(pos, old_text.size(), new_text);
            std::ofstream fout(path);
            fout << content; fout.close();
            result.content.push_back(muduo_http::mcp::McpProtocol::TextContent(
                "已替换 " + std::to_string(old_text.size()) + " 字节为 " + std::to_string(new_text.size()) + " 字节"));
            return result;
        });

    // execute_command - run a shell command
    mcp_server->RegisterTool(
        {"execute_command", "在服务器上执行 shell 命令并返回输出。可用于运行脚本、编译代码、安装包、操作 git、启动服务等。注意：命令在 WSL Ubuntu 环境中执行。",
         {{"command", "要执行的 shell 命令", "string", true},
          {"timeout_seconds", "超时秒数（默认 30，最大 120）", "number", false}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string cmd = args.value("command", "");
            int timeout = args.value("timeout_seconds", 30);
            if (timeout < 1 || timeout > 120) timeout = 30;
            cmd += " 2>&1"; // merge stderr into stdout
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：无法执行命令"));
                result.is_error = true; return result;
            }
            std::string output;
            char buf[4096];
            time_t start = time(nullptr);
            while (fgets(buf, sizeof(buf), pipe)) {
                output += buf;
                if (output.size() > 100000) { output += "\n...（输出过长已截断）"; break; }
                if (time(nullptr) - start > timeout) { output += "\n...（命令执行超时）"; break; }
            }
            int status = pclose(pipe);
            result.content.push_back(muduo_http::mcp::McpProtocol::TextContent(
                output.empty() ? "(无输出)" : output));
            if (status != 0 && !result.is_error) {
                // Non-zero exit code but we still got output
            }
            return result;
        });

    // search_web - search the internet
    mcp_server->RegisterTool(
        {"search_web", "在互联网上搜索信息。当用户问到实时信息、新闻、你不知道的内容时使用。返回搜索结果标题、摘要和链接。",
         {{"query", "搜索关键词", "string", true},
          {"count", "返回结果数（默认5，最多10）", "number", false}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string query = args.value("query", "");
            int count = args.value("count", 5);
            if (count < 1) count = 1;
            if (count > 10) count = 10;

            // Use DuckDuckGo lite HTML search (no API key required)
            // Escape query for shell
            std::string safe_query;
            for (char c : query) {
                if (c == '\'') safe_query += "'\\''";
                else safe_query += c;
            }

            std::string cmd = "curl -sL -A 'Mozilla/5.0' --max-time 15 "
                "-d 'q=" + safe_query + "' 'https://lite.duckduckgo.com/lite/' 2>&1";

            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：搜索请求失败"));
                result.is_error = true; return result;
            }

            std::string html;
            char buf[8192];
            while (fgets(buf, sizeof(buf), pipe)) html += buf;
            pclose(pipe);

            if (html.empty()) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("搜索无结果"));
                return result;
            }

            // Parse HTML to extract search results
            // DuckDuckGo lite format: <a href="url" class="result-link">title</a>
            std::string output;
            int found = 0;
            size_t pos = 0;
            while (found < count) {
                // Find result link
                auto link_start = html.find("<a ", pos);
                if (link_start == std::string::npos) break;

                auto href_pos = html.find("href=\"", link_start);
                if (href_pos == std::string::npos || href_pos > link_start + 100) { pos = link_start + 1; continue; }
                href_pos += 6;
                auto href_end = html.find("\"", href_pos);
                if (href_end == std::string::npos) break;
                std::string url = html.substr(href_pos, href_end - href_pos);

                // Skip ads and irrelevant links
                if (url.find("//") == std::string::npos || url.find("duckduckgo") != std::string::npos) {
                    pos = href_end; continue;
                }

                // Find title between <a ...> and </a>
                auto title_start = html.find(">", href_end) + 1;
                auto title_end = html.find("</a>", title_start);
                if (title_end == std::string::npos) break;
                std::string title = html.substr(title_start, title_end - title_start);

                // Find snippet after the link — supports both ' and " quotes
                auto snippet_pos = html.find("class='result-snippet'", title_end);
                if (snippet_pos == std::string::npos)
                    snippet_pos = html.find("class=\"result-snippet\"", title_end);
                std::string snippet;
                if (snippet_pos != std::string::npos && snippet_pos < title_end + 1000) {
                    auto sn_start = html.find(">", snippet_pos) + 1;
                    auto sn_end = html.find("</td>", sn_start);
                    if (sn_end != std::string::npos)
                        snippet = html.substr(sn_start, sn_end - sn_start);
                }

                // Strip HTML tags and decode entities
                auto strip_tags = [](std::string s) -> std::string {
                    std::string out;
                    bool in_tag = false;
                    for (char c : s) {
                        if (c == '<') in_tag = true;
                        else if (c == '>') in_tag = false;
                        else if (!in_tag) out += c;
                    }
                    return out;
                };
                auto decode = [](std::string s) -> std::string {
                    auto replace = [](std::string& str, const std::string& from, const std::string& to) {
                        size_t p = 0;
                        while ((p = str.find(from, p)) != std::string::npos) {
                            str.replace(p, from.length(), to);
                            p += to.length();
                        }
                    };
                    replace(s, "&amp;", "&");
                    replace(s, "&lt;", "<");
                    replace(s, "&gt;", ">");
                    replace(s, "&quot;", "\"");
                    replace(s, "&#x27;", "'");
                    return s;
                };

                found++;
                output += std::to_string(found) + ". " + decode(strip_tags(title)) + "\n";
                output += "   " + url + "\n";
                if (!snippet.empty()) output += "   " + decode(strip_tags(snippet)) + "\n";
                output += "\n";

                pos = title_end + 4;
            }

            if (output.empty()) output = "(未找到结果)";
            result.content.push_back(muduo_http::mcp::McpProtocol::TextContent(output));
            return result;
        });

    // search_files - search for text in files
    mcp_server->RegisterTool(
        {"search_files", "在文件或目录中搜索文本内容。支持 glob 模式匹配文件名（如 *.cpp, *.py, *）。",
         {{"pattern", "要搜索的文本模式（支持基本正则）", "string", true},
          {"path", "搜索路径（默认当前目录）", "string", false},
          {"glob", "文件名过滤，如 *.cpp、*.py、*（默认所有文件）", "string", false}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string pattern = args.value("pattern", "");
            std::string path = args.value("path", ".");
            std::string glob = args.value("glob", "");
            if (path.find("..") != std::string::npos) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：路径不能包含 .."));
                result.is_error = true; return result;
            }
            std::string cmd = "grep -rn --binary-files=without-match";
            if (!glob.empty()) cmd += " --include='" + glob + "'";
            cmd += " '" + pattern + "' " + path + " 2>&1 | head -200";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：搜索失败"));
                result.is_error = true; return result;
            }
            std::string output;
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe)) output += buf;
            pclose(pipe);
            if (output.empty()) output = "(未找到匹配结果)";
            result.content.push_back(muduo_http::mcp::McpProtocol::TextContent(output));
            return result;
        });

    // list_directory - list files in a directory
    mcp_server->RegisterTool(
        {"list_directory", "列出目录中的文件和子目录。显示文件名、大小、修改时间。",
         {{"path", "目录路径", "string", true}}},
        [](const nlohmann::json& args) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string path = args.value("path", "");
            if (path.find("..") != std::string::npos) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：路径不能包含 .."));
                result.is_error = true; return result;
            }
            std::string cmd = "ls -lah " + path + " 2>&1";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) {
                result.content.push_back(muduo_http::mcp::McpProtocol::TextContent("错误：无法列出目录"));
                result.is_error = true; return result;
            }
            std::string output;
            char buf[1024];
            while (fgets(buf, sizeof(buf), pipe)) output += buf;
            pclose(pipe);
            result.content.push_back(muduo_http::mcp::McpProtocol::TextContent(output));
            return result;
        });

    // system_info - get system information
    mcp_server->RegisterTool(
        {"system_info", "获取服务器系统信息：操作系统、CPU、内存、磁盘、运行时间。",
         {}},
        [](const nlohmann::json&) -> muduo_http::mcp::ToolResult {
            auto result = muduo_http::mcp::ToolResult{};
            std::string info;
            auto run = [](const char* cmd) -> std::string {
                FILE* pipe = popen(cmd, "r"); if (!pipe) return "N/A";
                char buf[256]; std::string r;
                if (fgets(buf, sizeof(buf), pipe)) r = buf;
                pclose(pipe);
                if (!r.empty() && r.back() == '\n') r.pop_back();
                return r;
            };
            info += "系统: " + run("uname -a") + "\n";
            info += "CPU: " + run("nproc") + " 核\n";
            info += "内存: " + run("free -h | grep Mem | awk '{print $3\"/\"$2}'") + "\n";
            info += "磁盘: " + run("df -h / | tail -1 | awk '{print $3\"/\"$2\" (\"$5\")\"}'") + "\n";
            info += "运行时间: " + run("uptime -p");
            result.content.push_back(muduo_http::mcp::McpProtocol::TextContent(info));
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
            {"api_base", live_cfg.Get("ai.api_base", "https://api.openai.com/v1")},
            {"rate_limit_rps", live_cfg.GetInt("ai.rate_limit_rps", 10)},
            {"cache_enabled", live_cfg.GetBool("ai.cache_enabled", true)},
            {"workspace", live_cfg.Get("ai.workspace", ".")}
        };
        response.SetBody(j.dump(2));
    });

    server.routes().Post("/api/config", [ai_gateway, &server](const muduo_http::HttpRequest& req,
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
                        content.replace(key_pos, eol - key_pos + 1, "api_key = " + key + "\n");
                    } else {
                        auto ai_eol = content.find('\n', pos) + 1;
                        content.insert(ai_eol, "api_key = " + key + "\n");
                    }
                }
            }

            if (body.contains("model")) {
                std::string model = body["model"].get<std::string>();
                auto pos = content.find("[ai]");
                auto m_pos = content.find("model", pos);
                auto next_sec = content.find("[", pos + 4);
                if (m_pos != std::string::npos && m_pos < next_sec) {
                    auto eol = content.find('\n', m_pos);
                    content.replace(m_pos, eol - m_pos + 1, "model = " + model + "\n");
                } else {
                    auto ai_eol = content.find('\n', pos) + 1;
                    content.insert(ai_eol, "model = " + model + "\n");
                }
            }

            if (body.contains("api_base")) {
                std::string base = body["api_base"].get<std::string>();
                auto pos = content.find("[ai]");
                auto b_pos = content.find("api_base", pos);
                auto next_sec = content.find("[", pos + 4);
                if (b_pos != std::string::npos && b_pos < next_sec) {
                    auto eol = content.find('\n', b_pos);
                    content.replace(b_pos, eol - b_pos + 1, "api_base = " + base + "\n");
                } else {
                    auto ai_eol = content.find('\n', pos) + 1;
                    content.insert(ai_eol, "api_base = " + base + "\n");
                }
            }

            if (body.contains("rate_limit_rps")) {
                std::string val = std::to_string(body["rate_limit_rps"].get<int>());
                auto pos = content.find("[ai]");
                auto r_pos = content.find("rate_limit_rps", pos);
                auto next_sec = content.find("[", pos + 4);
                if (r_pos != std::string::npos && r_pos < next_sec) {
                    auto eol = content.find('\n', r_pos);
                    content.replace(r_pos, eol - r_pos + 1, "rate_limit_rps = " + val + "\n");
                } else {
                    auto ai_eol = content.find('\n', pos) + 1;
                    content.insert(ai_eol, "rate_limit_rps = " + val + "\n");
                }
            }

            if (body.contains("cache_enabled")) {
                std::string val = body["cache_enabled"].get<bool>() ? "true" : "false";
                auto pos = content.find("[ai]");
                auto c_pos = content.find("cache_enabled", pos);
                auto next_sec = content.find("[", pos + 4);
                if (c_pos != std::string::npos && c_pos < next_sec) {
                    auto eol = content.find('\n', c_pos);
                    content.replace(c_pos, eol - c_pos + 1, "cache_enabled = " + val + "\n");
                } else {
                    auto ai_eol = content.find('\n', pos) + 1;
                    content.insert(ai_eol, "cache_enabled = " + val + "\n");
                }
            }

            std::ofstream out("server.conf");
            out << content;

            // Update running gateway
            if (body.contains("api_key"))
                ai_gateway->SetApiKey(body["api_key"].get<std::string>());
            if (body.contains("model"))
                ai_gateway->SetModel(body["model"].get<std::string>());
            if (body.contains("api_base")) {
                std::string base = body["api_base"].get<std::string>();
                if (base.find("/v1") == std::string::npos && base.find("localhost") == std::string::npos) {
                    if (base.back() == '/') base += "v1"; else base += "/v1";
                }
                ai_gateway->SetApiBase(base);
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
        DIR* dir = opendir("memory");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name.size() > 5 && name.substr(name.size() - 5) == ".json" &&
                    name.substr(0, 5) == "chat_") {
                    std::string sid = name.substr(5, name.size() - 10);
                    // Read first user message as preview
                    std::string preview = sid.substr(0, 12);
                    try {
                        std::ifstream f(std::string("memory/") + name);
                        nlohmann::json hist;
                        f >> hist;
                        for (auto& msg : hist) {
                            if (msg["role"] == "user" && !msg["content"].empty()) {
                                std::string c = msg["content"];
                                if (c.size() > 40) c = c.substr(0, 40) + "...";
                                preview = c;
                                break;
                            }
                        }
                    } catch (...) {}
                    sessions.push_back({{"id", sid}, {"preview", preview}});
                }
            }
            closedir(dir);
        }
        response.SetBody(sessions.dump(2));
    });

    // GET /api/sessions/:id/messages - return full history for a session
    server.routes().Get("/api/sessions/:id/messages", [](const muduo_http::HttpRequest& req,
                                                          muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        std::string sid = req.path_params.at("id");
        std::string path = "memory/chat_" + sid + ".json";
        std::ifstream f(path);
        if (!f.is_open()) {
            response.SetBody(nlohmann::json({{"error", "session not found"}}).dump());
            return;
        }
        try {
            nlohmann::json hist;
            f >> hist;
            // Return only user + assistant messages (skip system)
            nlohmann::json msgs = nlohmann::json::array();
            for (auto& msg : hist) {
                if (msg["role"] == "user" || msg["role"] == "assistant") {
                    msgs.push_back({{"role", msg["role"]}, {"content", msg.value("content", "")}});
                }
            }
            response.SetBody(msgs.dump(2));
        } catch (...) {
            response.SetBody(nlohmann::json({{"error", "parse error"}}).dump());
        }
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

    // Create chat agent — system prompt reads model name from config
    std::string model_name = cfg.Get("ai.model", "deepseek-v4-flash");
    std::string workspace_path = cfg.Get("ai.workspace", ".");
    std::string system_prompt =
        "你是知墨（ZhiMo），一个自建的 AI 助手。"
        "当前底层调用 " + model_name + " 模型的 API，但你不是该模型的官方产品，也不代表任何公司。\n"
        "当前工作目录：" + workspace_path + "。默认在此目录下操作文件。\n\n"

        "== 工具使用规则 ==\n"
        "你有以下工具可用，在处理用户请求时必须优先使用工具而非仅用文本回答：\n\n"

        "【读写文件】\n"
        "- read_file(path): 读取文件内容。当用户想查看代码、配置、文档时使用。\n"
        "- write_file(path, content): 写入/覆盖文件。当用户要求创建、编辑或保存文件时使用。\n"
        "  不得仅用文本模拟写文件，必须调用此工具完成。\n"
        "- edit_file(path, old_text, new_text): 精确替换文件中的局部内容。"
        "适合修改大文件中的某几行，不需要重写整个文件。使用前先 read_file 了解文件内容。\n\n"

        "【文件搜索】\n"
        "- search_files(pattern, path, glob): 在文件中搜索文本。用于查找代码中的函数定义、引用、关键词等。\n"
        "- list_directory(path): 列出目录内容。当用户想了解项目结构时使用。\n\n"

        "【命令执行】\n"
        "- execute_command(command, timeout_seconds): 执行 shell 命令。"
        "可用于编译、运行脚本、安装包、git 操作、启动服务等。\n\n"

        "【网络搜索】\n"
        "- search_web(query, count): 在互联网上搜索信息。"
        "当用户问到新闻、实时数据、你不知道的内容时使用。"
        "返回搜索结果的标题、链接和摘要。\n\n"

        "【系统信息】\n"
        "- system_info(): 查看操作系统、CPU、内存、磁盘等信息。\n\n"

        "== 行为规范 ==\n"
        "1. 当用户请求涉及文件操作（创建、编辑、查看、搜索），必须使用对应的文件工具，不能仅用文本模拟。\n"
        "2. 写代码或脚本时，使用 write_file 保存到文件，让用户可以直接运行。\n"
        "3. 需要确认项目结构时，先 list_directory 或 search_files 了解情况，再 read_file 查看具体文件。\n"
        "4. 编译或运行代码时，使用 execute_command 执行，并把输出返回给用户。\n"
        "5. 如果工具调用出错，向用户解释错误并尝试其他方法。\n"
        "6. 你有长期记忆能力，每次对话都会自动保存，下次可以继续。\n"
        "7. 回复时使用自然语言，不要用 markdown 格式包裹普通文字。"
        "代码块可以用 ``` 标注，但普通回答不要用 markdown 标题、列表符号等。\n"
        "8. 当用户问你是谁时，回答你是知墨，一个自建的 AI 助手。";
    auto chat_agent = std::make_shared<muduo_http::ChatAgent>(
        ai_gateway, tool_executor, system_prompt
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

    // Available providers: name → {api_base, model}
    std::map<std::string, std::pair<std::string, std::string>> providers = {
        {"deepseek", {"https://api.deepseek.com/v1", "deepseek-v4-flash"}},
        {"openai",   {"https://api.openai.com/v1",    "gpt-4o"}},
    };

    // GET /api/providers - list available providers
    server.routes().Get("/api/providers", [&providers](const muduo_http::HttpRequest&,
                                                        muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "application/json");
        nlohmann::json j = nlohmann::json::array();
        for (const auto& [name, cfg] : providers) {
            j.push_back({{"name", name}, {"api_base", cfg.first}, {"model", cfg.second}});
        }
        response.SetBody(j.dump(2));
    });

    // Worker thread pool — AI requests run here, not blocking IO threads
    muduo::ThreadPool worker_pool("ai-workers");
    worker_pool.start(8);
    auto* loop = server.get_loop();

    // POST /chat/memory - async chat with long-term memory + provider switching
    server.routes().Post("/chat/memory", [memory_manager, ai_gateway, &providers, &worker_pool, loop]
    (const muduo_http::HttpRequest& req, muduo_http::HttpResponse&) {
        // Parse request on IO thread
        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); } catch (...) {
            // Send parse error synchronously
            std::string err = nlohmann::json({{"error", "JSON parse error"}}).dump();
            std::ostringstream h;
            h << "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: "
              << err.size() << "\r\nConnection: close\r\n\r\n" << err;
            req.stream_conn->send(h.str());
            req.stream_conn->shutdown();
            return;
        }

        // Signal HttpServer: we'll handle the response ourselves
        req.stream = std::make_shared<muduo_http::StreamWriter>(
            req.stream_conn, 200, "OK", "application/json");

        std::string message = body.value("message", "");
        std::string session = body.value("session_id", "default");
        std::string provider = body.value("provider", "deepseek");

        if (body.value("clear", false)) {
            memory_manager->ClearSession(session);
            nlohmann::json resp = {{"response", "已清除"}};
            std::string json = resp.dump();
            std::ostringstream h;
            h << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
              << json.size() << "\r\nConnection: close\r\n\r\n" << json;
            req.stream_conn->send(h.str());
            req.stream_conn->shutdown();
            return;
        }
        if (message.empty()) {
            nlohmann::json resp = {{"error", "消息不能为空"}};
            std::string json = resp.dump();
            std::ostringstream h;
            h << "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: "
              << json.size() << "\r\nConnection: close\r\n\r\n" << json;
            req.stream_conn->send(h.str());
            req.stream_conn->shutdown();
            return;
        }

        // Capture connection for async response
        auto conn = req.stream_conn;

        // Post blocking work to thread pool
        worker_pool.run([memory_manager, ai_gateway, message, session, provider, &providers, loop, conn]() {
            // ---- This runs on a worker thread ----

            // Apply provider config to gateway
            muduo_http::ConfigManager live_cfg;
            live_cfg.Load("server.conf");
            std::string prov_key = live_cfg.Get("ai." + provider + ".api_key", "");
            if (prov_key.empty()) prov_key = live_cfg.Get("ai.api_key", "");
            if (!prov_key.empty()) {
                ai_gateway->SetApiKey(prov_key);
                auto it = providers.find(provider);
                std::string model = it != providers.end() ? it->second.second : "deepseek-v4-flash";
                std::string base = it != providers.end() ? it->second.first : "https://api.deepseek.com/v1";
                std::string cfg_model = live_cfg.Get("ai." + provider + ".model", "");
                if (!cfg_model.empty()) model = cfg_model;
                std::string cfg_base = live_cfg.Get("ai." + provider + ".api_base", "");
                if (!cfg_base.empty()) base = cfg_base;
                ai_gateway->SetModel(model);
                if (base.find("/v1") == std::string::npos && base.find("localhost") == std::string::npos) {
                    if (base.back() == '/') base += "v1"; else base += "/v1";
                }
                ai_gateway->SetApiBase(base);
            }

            auto start_time = std::chrono::steady_clock::now();
            auto result = memory_manager->Process(message, session);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();

            // Build response JSON
            nlohmann::json resp;
            if (result.success) {
                resp["response"] = result.content;
                resp["elapsed_ms"] = elapsed;
                resp["provider"] = provider;
                if (!result.reasoning_content.empty())
                    resp["reasoning"] = result.reasoning_content;
                if (result.prompt_tokens > 0 || result.completion_tokens > 0)
                    resp["usage"] = {{"prompt_tokens", result.prompt_tokens},
                                     {"completion_tokens", result.completion_tokens}};
            } else {
                resp["error"] = result.error_message;
            }

            std::string json = resp.dump();
            int status = result.success ? 200 : 502;

            // Send response back on IO thread
            loop->runInLoop([conn, json, status]() {
                std::ostringstream h;
                h << "HTTP/1.1 " << status << " " << (status == 200 ? "OK" : "Bad Gateway")
                  << "\r\nContent-Type: application/json\r\nContent-Length: "
                  << json.size() << "\r\nConnection: close\r\n\r\n" << json;
                conn->send(h.str());
                conn->shutdown();
            });
        });
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
