#include "http/graceful_shutdown.h"

#include <csignal>
#include <iostream>

namespace muduo_http {

GracefulShutdown& GracefulShutdown::Instance() {
    static GracefulShutdown instance;
    return instance;
}

void GracefulShutdown::Register(ShutdownCallback callback) {
    callbacks_.push_back(std::move(callback));
}

namespace {
// signal handler - static to avoid capturing lambda issues
volatile sig_atomic_t g_shutdown_requested = 0;
extern "C" void SignalHandler(int) {
    g_shutdown_requested = 1;
}
}

void GracefulShutdown::Install(muduo::net::EventLoop* loop) {
    struct sigaction sa;
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    if (loop) {
        // Periodically check the signal flag
        loop->runEvery(1.0, [this, loop]() {
            if (g_shutdown_requested && !shutting_down_) {
                shutting_down_ = true;
                std::cout << "\n[server] shutdown requested, cleaning up...\n";
                Shutdown();
                loop->quit();
            }
        });
    }
}

void GracefulShutdown::Shutdown() {
    if (shutting_down_) return;
    shutting_down_ = true;

    std::cout << "[server] running " << callbacks_.size() << " cleanup callbacks...\n";
    for (auto& cb : callbacks_) {
        if (cb) cb();
    }
    std::cout << "[server] shutdown complete.\n";
}

} // namespace muduo_http
