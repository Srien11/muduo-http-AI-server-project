#pragma once

#include <string>

namespace muduo_http {
namespace mcp {

struct ClientCapabilities {
    bool tools = false;
    bool resources = false;
    bool prompts = false;
    bool logging = false;
};

class McpSession {
public:
    enum State { kNew, kInitializing, kInitialized, kClosed };

    State state() const { return state_; }

    void SetInitialized();
    bool IsInitialized() const { return state_ == kInitialized; }

    // Client info
    ClientCapabilities client_caps;
    std::string client_name;
    std::string client_version;
    std::string protocol_version;

    // Server capabilities - what we support
    struct {
        bool tools = true;
        bool resources = true;
        bool prompts = false;
        bool logging = true;
    } server_caps;

private:
    State state_{kNew};
};

} // namespace mcp
} // namespace muduo_http
