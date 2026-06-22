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

    // If switching to a different session, clear history to force reload
    if (sid != agent_->session_id()) {
        agent_->ClearHistory();
    }

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

    int excess = static_cast<int>(history.size()) - max_context_messages_;
    if (excess <= 0) return;

    // Walk forward from the start, counting messages to remove.
    // When we hit an assistant(tool_calls), skip the whole group
    // (assistant + following tool messages) — never split them.
    int cut = 0;
    int removed = 0;
    int n = static_cast<int>(history.size());
    while (cut < n && removed < excess) {
        // assistant(tool_calls) + following tool messages = indivisible group
        if (history[cut].role == "assistant" && !history[cut].tool_calls.is_null()) {
            // Find end of the tool message block
            int group_end = cut + 1;
            while (group_end < n && history[group_end].role == "tool") {
                group_end++;
            }
            int group_size = group_end - cut;
            // Only remove the whole group if it fits entirely within excess
            if (removed + group_size <= excess) {
                removed += group_size;
                cut = group_end;
            } else {
                break;  // Can't split — stop trimming
            }
        } else {
            // Regular message (user, assistant without tool_calls, system, tool...)
            // Actually "tool" here shouldn't happen since they're handled above,
            // but just in case, count them
            if (history[cut].role == "tool") {
                // Orphaned tool message — should not happen, skip it
                cut++;
                removed++;
            } else {
                cut++;
                removed++;
            }
        }
    }

    if (cut > 0) {
        history.erase(history.begin(), history.begin() + cut);
        std::cout << "[memory] trimmed " << cut << " old messages, "
                  << history.size() << " remaining\n";
    }
}

std::string MemoryManager::HistoryPath(const std::string& session_id) const {
    return memory_dir_ + "/chat_" + session_id + ".json";
}

} // namespace muduo_http
