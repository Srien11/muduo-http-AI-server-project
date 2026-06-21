#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "http/db_connection_pool.h"
#include "http/http_response.h"
#include "http/http_server.h"
#include "http/https_server.h"
#include "http/middleware.h"
#include "http/stream_writer.h"

int main() {
    muduo_http::HttpServer server(8080);

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
