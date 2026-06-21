#include "http/memory_manager.h"

#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>

namespace muduo_http {

MemoryManager::MemoryManager(std::shared_ptr<ChatAgent> agent,
                             std::shared_ptr<AiGateway> gateway,
                             const std::string& memory_dir)
    : agent_(std::move(agent)),
      gateway_(std::move(gateway)),
      memory_dir_(memory_dir) {
    // Create memory directory if it doesn't exist
    mkdir(memory_dir_.c_str(), 0755);
}

AiChatResponse MemoryManager::Process(const std::string& user_message,
                                       const std::string& session_id) {
    std::string sid = session_id.empty() ? "default" : session_id;
    agent_->set_session_id(sid);

    // Load long-term memory (past conversations from disk)
    std::string history_file = HistoryPath(sid);
    if (agent_->history().empty()) {
        agent_->LoadHistory(history_file);
    }

    // Trim context if too long
    TrimContext();

    // Process through the agent
    auto result = agent_->Process(user_message);

    // Save long-term memory
    agent_->SaveHistory(history_file);

    return result;
}

void MemoryManager::ClearSession(const std::string& session_id) {
    std::string sid = session_id.empty() ? "default" : session_id;
    agent_->ClearHistory();
    // Also delete the file
    std::remove(HistoryPath(sid).c_str());
}

void MemoryManager::TrimContext() {
    auto& history = agent_->history();
    if (static_cast<int>(history.size()) <= trim_threshold_) return;

    // Count messages and trim oldest first
    // Keep the system prompt (if any) and the most recent messages
    int excess = static_cast<int>(history.size()) - max_context_messages_;
    if (excess <= 0) return;

    // Remove oldest messages, keep the most recent
    history.erase(history.begin(), history.begin() + excess);

    std::cout << "[memory] trimmed " << excess << " old messages, "
              << history.size() << " remaining\n";
}

std::string MemoryManager::HistoryPath(const std::string& session_id) const {
    return memory_dir_ + "/chat_" + session_id + ".json";
}

} // namespace muduo_http
