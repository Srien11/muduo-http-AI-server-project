#pragma once

#include <memory>
#include <string>

#include "http/ai_gateway.h"
#include "http/chat_agent.h"

namespace muduo_http {

class MemoryManager {
public:
    MemoryManager(std::shared_ptr<ChatAgent> agent,
                  std::shared_ptr<AiGateway> gateway,
                  const std::string& memory_dir = "memory/");

    // Process a message with full memory support
    AiChatResponse Process(const std::string& user_message,
                           const std::string& session_id = "");

    // Clear all memory for a session
    void ClearSession(const std::string& session_id);

    // Configuration
    void set_max_context_messages(int n) { max_context_messages_ = n; }
    int max_context_messages() const { return max_context_messages_; }

    // Access agent for dynamic prompt support
    std::shared_ptr<ChatAgent> agent() const { return agent_; }

private:
    std::string HistoryPath(const std::string& session_id) const;
    void TrimContext();

    std::shared_ptr<ChatAgent> agent_;
    std::shared_ptr<AiGateway> gateway_;
    std::string memory_dir_;
    int max_context_messages_{20};   // Keep last 20 messages before trimming
    int trim_threshold_{30};         // Start trimming at 30
};

} // namespace muduo_http
