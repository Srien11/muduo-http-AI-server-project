#include <iostream>
#include <stdexcept>
#include <string>

#include "http/http_response.h"
#include "http/http_server.h"
#include "http/middleware.h"

int main() {
    muduo_http::HttpServer server(8080);

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

    std::cout << "Starting HTTP server on port 8080..." << std::endl;
    server.Start();
    return 0;
}
