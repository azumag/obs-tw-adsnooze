#pragma once

#include "core/chat-activity-tracker.hpp"
#include "core/snooze-policy.hpp"

#include <chrono>
#include <string>

namespace adsnooze::obs_plugin {

struct TwitchCredentials {
    std::string client_id;
    std::string access_token;
    std::string broadcaster_id;
};

struct PluginConfig {
    bool enabled{false};
    bool dry_run{true};
    std::chrono::seconds poll_interval{15};
    TwitchCredentials twitch;
    SnoozePolicyConfig policy;
    ChatActivityConfig chat;
};

[[nodiscard]] PluginConfig load_or_create_config();

} // namespace adsnooze::obs_plugin
