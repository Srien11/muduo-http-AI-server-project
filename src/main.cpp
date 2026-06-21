#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "http/ai_gateway.h"
#include "http/db_connection_pool.h"
#include "http/http_response.h"
#include "http/http_server.h"
#include "http/https_server.h"
#include "http/middleware.h"
#include "http/multipart_parser.h"
#include "http/static_file_handler.h"
#include "http/stream_writer.h"

int main() {
    muduo_http::HttpServer server(8080);
    server.set_thread_num(4);  // Prevent AI calls from blocking all connections

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
    // ai_cfg.api_key = std::getenv("OPENAI_API_KEY") ?: "";
    ai_cfg.api_base = "https://api.openai.com/v1";
    ai_cfg.model = "gpt-3.5-turbo";
    ai_cfg.rate_limit_rps = 5;
    ai_cfg.cache_enabled = true;

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
    muduo_http::HttpsServer https_server(8443, "certs/server.crt", "certs/server.key");
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

    std::cout << "Starting HTTP server on port 8080..." << std::endl;
    std::cout << "HTTPS server on port 8443 (self-signed cert)" << std::endl;
    server.Start();
    return 0;
}
