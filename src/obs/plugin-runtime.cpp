#include "obs/plugin-runtime.hpp"

#include "obs/audio-monitor-registry.hpp"
#include "twitch/twitch-ad-client.hpp"

#include <obs-module.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>
#include <optional>
#include <utility>

namespace adsnooze::obs_plugin {
namespace {

bool is_numeric_id(const std::string &value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isdigit(character) != 0;
           });
}

} // namespace

PluginRuntime::PluginRuntime()
    : config_(load_or_create_config()), decision_engine_(config_.policy), chat_tracker_(config_.chat)
{
}

PluginRuntime::~PluginRuntime()
{
    stop();
}

void PluginRuntime::start()
{
    if (worker_.joinable()) {
        return;
    }

    if (!config_.enabled) {
        blog(LOG_INFO,
             "[obs-tw-adsnooze] Integration is disabled; add the audio monitor filter and edit config.json to "
             "enable it");
        return;
    }

    if (!validate_configuration()) {
        return;
    }

    if (config_.policy.use_chat_activity) {
        blog(LOG_WARNING,
             "[obs-tw-adsnooze] Chat activity is configured but the EventSub transport is not enabled in this "
             "prototype");
    }

    stopping_.store(false, std::memory_order_release);
    try {
        worker_ = std::thread(&PluginRuntime::run, this);
    } catch (const std::exception &error) {
        stopping_.store(true, std::memory_order_release);
        blog(LOG_ERROR, "[obs-tw-adsnooze] Could not start ad schedule worker: %s", error.what());
        return;
    }
    blog(LOG_INFO, "[obs-tw-adsnooze] Ad schedule worker started%s", config_.dry_run ? " in dry-run mode" : "");
}

void PluginRuntime::stop()
{
    stopping_.store(true, std::memory_order_release);
    wait_condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
        blog(LOG_INFO, "[obs-tw-adsnooze] Ad schedule worker stopped");
    }
}

bool PluginRuntime::record_chat_message(std::string message_id, std::string chatter_id)
{
    std::lock_guard lock(chat_mutex_);
    return chat_tracker_.record_message(std::move(message_id), std::move(chatter_id), MonotonicClock::now());
}

CombinedActivitySnapshot PluginRuntime::collect_activity()
{
    const auto audio_states = AudioMonitorRegistry::instance().snapshot();
    const std::size_t active_audio_sources = static_cast<std::size_t>(
        std::count_if(audio_states.begin(), audio_states.end(), [](const AudioMonitorSnapshot &state) {
            return state.active;
        }));

    ChatActivitySnapshot chat;
    {
        std::lock_guard lock(chat_mutex_);
        chat = chat_tracker_.snapshot(MonotonicClock::now());
    }

    return CombinedActivitySnapshot{
        .audio_active = active_audio_sources > 0,
        .active_audio_sources = active_audio_sources,
        .chat_active = chat.active,
        .chat_messages = chat.message_count,
        .unique_chatters = chat.unique_chatter_count,
    };
}

bool PluginRuntime::validate_configuration() const
{
    if (config_.twitch.client_id.empty() || config_.twitch.access_token.empty() ||
        !is_numeric_id(config_.twitch.broadcaster_id)) {
        blog(LOG_ERROR,
             "[obs-tw-adsnooze] Enabled configuration requires client_id, access_token, and a numeric broadcaster_id");
        return false;
    }
    return true;
}

void PluginRuntime::run()
{
    using namespace std::chrono_literals;

    twitch::TwitchAdClient client(config_.twitch.client_id, config_.twitch.access_token,
                                  config_.twitch.broadcaster_id);
    client.set_stop_flag(stopping_);
    auto next_validation = MonotonicTimePoint::min();
    bool token_valid = false;
    std::optional<SnoozeDecisionCode> last_logged_decision;
    std::optional<WallTimePoint> last_logged_ad_at;

    auto process_iteration = [&] {
        const auto monotonic_now = MonotonicClock::now();
        if (monotonic_now >= next_validation) {
            const auto validation = client.validate_token();
            token_valid = validation.ok && validation.value.client_id == config_.twitch.client_id &&
                          validation.value.user_id == config_.twitch.broadcaster_id &&
                          validation.value.has_read_ads_scope && validation.value.has_manage_ads_scope;

            if (token_valid) {
                next_validation = monotonic_now + 55min;
                blog(LOG_INFO, "[obs-tw-adsnooze] Twitch token validated");
            } else {
                next_validation = monotonic_now + 60s;
                blog(LOG_ERROR,
                     "[obs-tw-adsnooze] Twitch token validation failed or required ad scopes are missing "
                     "(HTTP %ld): %s",
                     validation.http_status,
                     validation.error.empty() ? "identity/scope mismatch" : validation.error.c_str());
            }
        }

        if (stopping_.load(std::memory_order_acquire)) {
            return;
        }

        if (token_valid) {
            const auto schedule_result = client.get_ad_schedule();
            if (!schedule_result.ok) {
                blog(LOG_WARNING, "[obs-tw-adsnooze] Could not read ad schedule (HTTP %ld): %s",
                     schedule_result.http_status, schedule_result.error.c_str());
                if (schedule_result.http_status == 401) {
                    token_valid = false;
                    next_validation = MonotonicClock::now();
                }
            } else {
                const CombinedActivitySnapshot activity = collect_activity();
                const WallTimePoint wall_now = WallClock::now();
                const SnoozeDecision decision = decision_engine_.evaluate(wall_now, schedule_result.value, activity);

                if (!last_logged_decision || *last_logged_decision != decision.code ||
                    last_logged_ad_at != schedule_result.value.next_ad_at) {
                    const long long next_ad_epoch = schedule_result.value.next_ad_at
                                                        ? std::chrono::duration_cast<std::chrono::seconds>(
                                                              schedule_result.value.next_ad_at->time_since_epoch())
                                                              .count()
                                                        : -1LL;
                    blog(LOG_DEBUG,
                         "[obs-tw-adsnooze] Decision=%s next_ad_epoch=%lld active_audio_sources=%zu "
                         "chat_messages=%zu unique_chatters=%zu snoozes=%d",
                         to_string(decision.code), next_ad_epoch, activity.active_audio_sources,
                         activity.chat_messages, activity.unique_chatters, schedule_result.value.snooze_count);
                    last_logged_decision = decision.code;
                    last_logged_ad_at = schedule_result.value.next_ad_at;
                }

                if (decision.should_snooze() && schedule_result.value.next_ad_at) {
                    bool succeeded = false;
                    if (config_.dry_run) {
                        blog(LOG_INFO,
                             "[obs-tw-adsnooze] Dry run: would snooze upcoming ad (audio=%s, chat=%s, "
                             "active_sources=%zu)",
                             decision.audio_triggered ? "true" : "false", decision.chat_triggered ? "true" : "false",
                             activity.active_audio_sources);
                        succeeded = true;
                    } else if (!stopping_.load(std::memory_order_acquire)) {
                        const auto snooze_result = client.snooze_next_ad();
                        succeeded = snooze_result.ok;
                        if (succeeded) {
                            blog(LOG_INFO,
                                 "[obs-tw-adsnooze] Snoozed upcoming Twitch ad (audio=%s, chat=%s, remaining=%d)",
                                 decision.audio_triggered ? "true" : "false",
                                 decision.chat_triggered ? "true" : "false", snooze_result.value.snooze_count);
                        } else {
                            blog(LOG_WARNING, "[obs-tw-adsnooze] Snooze request failed (HTTP %ld): %s",
                                 snooze_result.http_status, snooze_result.error.c_str());
                            if (snooze_result.http_status == 401) {
                                token_valid = false;
                                next_validation = MonotonicClock::now();
                            }
                        }
                    }
                    decision_engine_.record_attempt(wall_now, *schedule_result.value.next_ad_at, succeeded);
                }
            }
        }
    };

    while (!stopping_.load(std::memory_order_acquire)) {
        try {
            process_iteration();
        } catch (const std::exception &error) {
            blog(LOG_ERROR, "[obs-tw-adsnooze] Schedule worker iteration failed: %s", error.what());
        } catch (...) {
            blog(LOG_ERROR, "[obs-tw-adsnooze] Schedule worker iteration failed with an unknown exception");
        }

        std::unique_lock lock(wait_mutex_);
        wait_condition_.wait_for(lock, config_.poll_interval,
                                 [this] { return stopping_.load(std::memory_order_acquire); });
    }
}

} // namespace adsnooze::obs_plugin
