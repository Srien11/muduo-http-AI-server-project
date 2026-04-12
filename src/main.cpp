#include <iostream>

#include "http/http_request.h"
#include "http/http_response.h"
#include "http/http_server.h"

int main() {
    muduo_http::HttpServer server(8080);

    server.routes().Get("/", [](const muduo_http::HttpRequest&, muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("Hello from muduo_http router.\n");
    });

    muduo_http::HttpRequest request;
    request.method = "GET";
    request.path = "/";
    request.version = "HTTP/1.1";

    muduo_http::HttpResponse response;
    server.routes().Route(request, response);

    std::cout << response.ToString() << std::endl;
    return 0;
}
