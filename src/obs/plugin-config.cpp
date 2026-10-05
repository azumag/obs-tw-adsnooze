#include "obs/plugin-config.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace adsnooze::obs_plugin {
namespace {

bool get_bool(obs_data_t *data, const char *key, bool fallback)
{
    return data && obs_data_has_user_value(data, key) ? obs_data_get_bool(data, key) : fallback;
}

long long get_int(obs_data_t *data, const char *key, long long fallback)
{
    return data && obs_data_has_user_value(data, key) ? obs_data_get_int(data, key) : fallback;
}

std::string get_string(obs_data_t *data, const char *key)
{
    if (!data || !obs_data_has_user_value(data, key)) {
        return {};
    }
    const char *value = obs_data_get_string(data, key);
    return value ? value : "";
}

void ensure_parent_directory(char *path)
{
    if (!path) {
        return;
    }

#ifdef _WIN32
    char *backslash = std::strrchr(path, '\\');
    if (backslash) {
        *backslash = '/';
    }
#endif

    char *slash = std::strrchr(path, '/');
    if (slash) {
        *slash = '\0';
        os_mkdirs(path);
        *slash = '/';
    }

#ifdef _WIN32
    if (backslash) {
        *backslash = '\\';
    }
#endif
}

obs_data_t *make_default_data()
{
    obs_data_t *root = obs_data_create();
    obs_data_set_int(root, "schema_version", 1);
    obs_data_set_bool(root, "enabled", false);
    obs_data_set_bool(root, "dry_run", true);
    obs_data_set_int(root, "poll_interval_seconds", 15);

    obs_data_t *twitch = obs_data_create();
    obs_data_set_string(twitch, "client_id", "");
    obs_data_set_string(twitch, "access_token", "");
    obs_data_set_string(twitch, "broadcaster_id", "");
    obs_data_set_obj(root, "twitch", twitch);
    obs_data_release(twitch);

    obs_data_t *policy = obs_data_create();
    obs_data_set_bool(policy, "use_audio_activity", true);
    obs_data_set_bool(policy, "use_chat_activity", false);
    obs_data_set_int(policy, "lead_time_seconds", 90);
    obs_data_set_int(policy, "past_due_grace_seconds", 15);
    obs_data_set_int(policy, "retry_cooldown_seconds", 30);
    obs_data_set_int(policy, "reserve_snoozes", 0);
    obs_data_set_obj(root, "policy", policy);
    obs_data_release(policy);

    obs_data_t *chat = obs_data_create();
    obs_data_set_int(chat, "window_seconds", 30);
    obs_data_set_int(chat, "minimum_messages", 12);
    obs_data_set_int(chat, "minimum_unique_chatters", 4);
    obs_data_set_int(chat, "hold_seconds", 10);
    obs_data_set_obj(root, "chat", chat);
    obs_data_release(chat);

    return root;
}

void write_default_config(const char *path)
{
    obs_data_t *root = make_default_data();
    if (!obs_data_save_json_pretty_safe(root, path, "tmp", "bak")) {
        blog(LOG_ERROR, "[obs-tw-adsnooze] Failed to create default config file");
    } else {
        blog(LOG_INFO, "[obs-tw-adsnooze] Created default config file; integration remains disabled");
    }
    obs_data_release(root);
}

} // namespace

PluginConfig load_or_create_config()
{
    PluginConfig config;
    config.policy.enabled = false;
    config.policy.use_audio_activity = true;
    config.policy.use_chat_activity = false;

    char *path = obs_module_config_path("config.json");
    if (!path) {
        blog(LOG_ERROR, "[obs-tw-adsnooze] Could not resolve plugin config path");
        return config;
    }

    obs_data_t *root = obs_data_create_from_json_file_safe(path, "bak");
    if (!root) {
        ensure_parent_directory(path);
        write_default_config(path);
        bfree(path);
        return config;
    }

    if (obs_data_get_int(root, "schema_version") > 1) {
        blog(LOG_WARNING,
             "[obs-tw-adsnooze] Config schema version is newer than supported; unknown settings are ignored");
    }

    config.enabled = get_bool(root, "enabled", false);
    config.dry_run = get_bool(root, "dry_run", true);
    config.poll_interval = std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
        std::clamp<long long>(get_int(root, "poll_interval_seconds", 15), 5, 300))};

    obs_data_t *twitch = obs_data_get_obj(root, "twitch");
    config.twitch.client_id = get_string(twitch, "client_id");
    config.twitch.access_token = get_string(twitch, "access_token");
    config.twitch.broadcaster_id = get_string(twitch, "broadcaster_id");
    obs_data_release(twitch);

    obs_data_t *policy = obs_data_get_obj(root, "policy");
    config.policy.enabled = config.enabled;
    config.policy.use_audio_activity = get_bool(policy, "use_audio_activity", true);
    config.policy.use_chat_activity = get_bool(policy, "use_chat_activity", false);
    config.policy.lead_time =
        std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
            std::clamp<long long>(get_int(policy, "lead_time_seconds", 90), 0, 600))};
    config.policy.past_due_grace =
        std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
            std::clamp<long long>(get_int(policy, "past_due_grace_seconds", 15), 0, 120))};
    config.policy.retry_cooldown =
        std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
            std::clamp<long long>(get_int(policy, "retry_cooldown_seconds", 30), 0, 600))};
    config.policy.reserve_snoozes =
        static_cast<int>(std::clamp<long long>(get_int(policy, "reserve_snoozes", 0), 0, 10));
    obs_data_release(policy);

    obs_data_t *chat = obs_data_get_obj(root, "chat");
    config.chat.window =
        std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
            std::clamp<long long>(get_int(chat, "window_seconds", 30), 1, 600))};
    config.chat.minimum_messages =
        static_cast<std::size_t>(std::clamp<long long>(get_int(chat, "minimum_messages", 12), 0, 10000));
    config.chat.minimum_unique_chatters =
        static_cast<std::size_t>(std::clamp<long long>(get_int(chat, "minimum_unique_chatters", 4), 0, 10000));
    config.chat.hold = std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
        std::clamp<long long>(get_int(chat, "hold_seconds", 10), 0, 600))};
    obs_data_release(chat);

    obs_data_release(root);
    bfree(path);
    return config;
}

} // namespace adsnooze::obs_plugin
