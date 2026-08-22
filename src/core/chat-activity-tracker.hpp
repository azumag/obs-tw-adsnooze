#pragma once

#include "core/time.hpp"

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <unordered_set>

namespace adsnooze {

struct ChatActivityConfig {
    std::chrono::seconds window{30};
    std::size_t minimum_messages{12};
    std::size_t minimum_unique_chatters{4};
    std::chrono::seconds hold{10};
};

struct ChatActivitySnapshot {
    bool active{false};
    std::size_t message_count{0};
    std::size_t unique_chatter_count{0};
};

class ChatActivityTracker {
public:
    explicit ChatActivityTracker(ChatActivityConfig config = {});

    void configure(ChatActivityConfig config);
    void reset();

    // Returns false when a duplicate message ID was ignored.
    bool record_message(std::string message_id, std::string chatter_id, MonotonicTimePoint now);
    [[nodiscard]] ChatActivitySnapshot snapshot(MonotonicTimePoint now);

private:
    struct Message {
        std::string id;
        std::string chatter_id;
        MonotonicTimePoint received_at;
    };

    static ChatActivityConfig normalize(ChatActivityConfig config);
    void trim(MonotonicTimePoint now);

    ChatActivityConfig config_;
    std::deque<Message> messages_;
    std::unordered_set<std::string> seen_message_ids_;
    MonotonicTimePoint last_active_at_{};
    bool has_been_active_{false};
};

} // namespace adsnooze
