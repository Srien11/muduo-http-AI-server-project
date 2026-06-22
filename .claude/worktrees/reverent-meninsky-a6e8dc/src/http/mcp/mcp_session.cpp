#include "http/mcp/mcp_session.h"

namespace muduo_http {
namespace mcp {

void McpSession::SetInitialized() {
    state_ = kInitialized;
}

} // namespace mcp
} // namespace muduo_http
