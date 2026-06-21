cmake_minimum_required(VERSION 3.16)
project(muduo-http-AI-server-project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)
link_directories(/usr/local/lib)

add_executable(muduo_http_server
    src/main.cpp
    src/http/ai_gateway.cpp
    src/http/chat_agent.cpp
    src/http/config_manager.cpp
    src/http/db_connection_pool.cpp
    src/http/graceful_shutdown.cpp
    src/http/http_context.cpp
    src/http/https_server.cpp
    src/http/log_manager.cpp
    src/http/mcp/mcp_protocol.cpp
    src/http/mcp/mcp_server.cpp
    src/http/mcp/mcp_session.cpp
    src/http/mcp/mcp_tool.cpp
    src/http/mcp/mcp_transport.cpp
    src/http/middleware.cpp
    src/http/model_router.cpp
    src/http/multipart_parser.cpp
    src/http/rag_pipeline.cpp
    src/http/rate_limiter.cpp
    src/http/router.cpp
    src/http/session.cpp
    src/http/ssl_context.cpp
    src/http/static_file_handler.cpp
    src/http/stream_writer.cpp
    src/http/http_server.cpp
    src/http/http_response.cpp
)

target_link_libraries(muduo_http_server
    muduo_net
    muduo_base
    pthread
    mysqlclient
    ssl
    crypto
    curl
)