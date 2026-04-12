#include <iostream>

#include "http/http_server.h"
#include "http/http_response.h"

int main() {
    muduo_http::HttpServer server(8080);

    muduo_http::HttpResponse response;
    response.SetHeader("Content-Type", "text/plain; charset=utf-8");
    response.SetBody("muduo-http-AI-server-project started on port 8080.\n");

    std::cout << response.ToString() << std::endl;
    return 0;
}
