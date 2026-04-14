#include <iostream>
#include <string>

#include "http/http_context.h"
#include "http/http_response.h"
#include "http/http_server.h"

int main() {
    muduo_http::HttpServer server(8080);

    server.routes().Get("/", [](const muduo_http::HttpRequest&, muduo_http::HttpResponse& response) {
        response.SetHeader("Content-Type", "text/plain; charset=utf-8");
        response.SetBody("Hello from muduo_http router.\n");
    });

    const std::string raw_request =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "User-Agent: demo-client\r\n"
        "\r\n";

    muduo_http::HttpContext context;
    if (!context.ParseRequest(raw_request)) {
        std::cerr << "Parse request failed\n";
        return 1;
    }

    muduo_http::HttpResponse response;
    server.routes().Route(context.request(), response);

    std::cout << response.ToString() << std::endl;
    return 0;
}
