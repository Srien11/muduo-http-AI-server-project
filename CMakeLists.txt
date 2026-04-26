cmake_minimum_required(VERSION 3.16)
project(muduo-http-AI-server-project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

add_executable(muduo_http_server
    src/main.cpp
    src/http/http_context.cpp
    src/http/middleware.cpp
    src/http/router.cpp
    src/http/http_server.cpp
    src/http/http_response.cpp
)

