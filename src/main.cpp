#include <iostream>

#include "http/http_response.h"
#include "http/http_server.h"

int main() {
    muduo_http::HttpServer server(8080);

    server.routes().Get("/", [](const muduo_http::HttpRequest&, muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("Hello from muduo_http router.\n");
    });

    server.routes().Get("/health", [](const muduo_http::HttpRequest&, muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("ok\n");
    });

    std::cout << "Starting HTTP server on port 8080..." << std::endl;
    server.Start();
    return 0;
}
