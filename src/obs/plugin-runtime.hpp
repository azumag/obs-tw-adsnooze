#pragma once

#include "core/chat-activity-tracker.hpp"
#include "core/snooze-policy.hpp"
#include "obs/plugin-config.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace adsnooze::obs_plugin {

class PluginRuntime {
public:
    PluginRuntime();
    ~PluginRuntime();

    PluginRuntime(const PluginRuntime &) = delete;
    PluginRuntime &operator=(const PluginRuntime &) = delete;

    void start();
    void stop();

    // EventSub transport will call this in the next implementation phase.
    bool record_chat_message(std::string message_id, std::string chatter_id);

private:
    void run();
    [[nodiscard]] CombinedActivitySnapshot collect_activity();
    [[nodiscard]] bool validate_configuration() const;

    PluginConfig config_;
    SnoozeDecisionEngine decision_engine_;
    ChatActivityTracker chat_tracker_;
    std::mutex chat_mutex_;

    std::atomic_bool stopping_{false};
    std::mutex wait_mutex_;
    std::condition_variable wait_condition_;
    std::thread worker_;
};

} // namespace adsnooze::obs_plugin
