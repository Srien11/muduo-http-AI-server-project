#include <iostream>

#include "http/http_server.h"

int main() {
    muduo_http::HttpServer server(8080);

    std::cout << "muduo-http-AI-server-project started on port 8080." << std::endl;
    return 0;
}
