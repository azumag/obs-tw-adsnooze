#include "core/chat-activity-tracker.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace adsnooze {

ChatActivityTracker::ChatActivityTracker(ChatActivityConfig config) : config_(normalize(config)) {}

void ChatActivityTracker::configure(ChatActivityConfig config)
{
    config_ = normalize(config);
    reset();
}

void ChatActivityTracker::reset()
{
    messages_.clear();
    seen_message_ids_.clear();
    last_active_at_ = MonotonicTimePoint{};
    has_been_active_ = false;
}

bool ChatActivityTracker::record_message(std::string message_id, std::string chatter_id, MonotonicTimePoint now)
{
    trim(now);

    if (!message_id.empty() && seen_message_ids_.contains(message_id)) {
        return false;
    }

    if (!message_id.empty()) {
        seen_message_ids_.insert(message_id);
    }

    messages_.push_back(Message{
        .id = std::move(message_id),
        .chatter_id = std::move(chatter_id),
        .received_at = now,
    });
    return true;
}

ChatActivitySnapshot ChatActivityTracker::snapshot(MonotonicTimePoint now)
{
    trim(now);

    std::unordered_set<std::string> unique_chatters;
    unique_chatters.reserve(messages_.size());
    for (const Message &message : messages_) {
        if (!message.chatter_id.empty()) {
            unique_chatters.insert(message.chatter_id);
        }
    }

    const bool enough_messages = config_.minimum_messages == 0 || messages_.size() >= config_.minimum_messages;
    const bool enough_chatters =
        config_.minimum_unique_chatters == 0 || unique_chatters.size() >= config_.minimum_unique_chatters;
    // Zero disables an individual threshold. Requiring at least one enabled
    // threshold prevents a misconfiguration of 0/0 from making chat appear
    // permanently busy even when no messages have arrived.
    const bool has_activity_threshold = config_.minimum_messages > 0 || config_.minimum_unique_chatters > 0;
    const bool raw_active = has_activity_threshold && enough_messages && enough_chatters;

    if (raw_active) {
        last_active_at_ = now;
        has_been_active_ = true;
    }

    const bool held_active = has_been_active_ && now - last_active_at_ <= config_.hold;
    return ChatActivitySnapshot{
        .active = raw_active || held_active,
        .message_count = messages_.size(),
        .unique_chatter_count = unique_chatters.size(),
    };
}

ChatActivityConfig ChatActivityTracker::normalize(ChatActivityConfig config)
{
    config.window = std::max(config.window, std::chrono::seconds::zero());
    config.hold = std::max(config.hold, std::chrono::seconds::zero());
    return config;
}

void ChatActivityTracker::trim(MonotonicTimePoint now)
{
    const MonotonicTimePoint cutoff = now - config_.window;
    while (!messages_.empty() && messages_.front().received_at < cutoff) {
        if (!messages_.front().id.empty()) {
            seen_message_ids_.erase(messages_.front().id);
        }
        messages_.pop_front();
    }
}

} // namespace adsnooze
