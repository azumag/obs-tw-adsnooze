#include "core/snooze-policy.hpp"

#include <algorithm>

namespace adsnooze {

SnoozeDecisionEngine::SnoozeDecisionEngine(SnoozePolicyConfig config) : config_(normalize(config)) {}

void SnoozeDecisionEngine::configure(SnoozePolicyConfig config) noexcept
{
    config_ = normalize(config);
    reset();
}

void SnoozeDecisionEngine::reset() noexcept
{
    last_attempt_at_.reset();
    last_attempted_ad_at_.reset();
    handled_ad_at_.reset();
}

SnoozeDecision SnoozeDecisionEngine::evaluate(WallTimePoint now, const AdSchedule &schedule,
                                               const CombinedActivitySnapshot &activity) const noexcept
{
    if (!config_.enabled) {
        return {.code = SnoozeDecisionCode::disabled};
    }

    if (!config_.use_audio_activity && !config_.use_chat_activity) {
        return {.code = SnoozeDecisionCode::no_conditions_enabled};
    }

    if (!schedule.next_ad_at) {
        return {.code = SnoozeDecisionCode::no_upcoming_ad};
    }

    const WallTimePoint next_ad_at = *schedule.next_ad_at;
    if (next_ad_at < now - config_.past_due_grace) {
        return {.code = SnoozeDecisionCode::stale_schedule};
    }

    if (next_ad_at > now + config_.lead_time) {
        return {.code = SnoozeDecisionCode::too_early};
    }

    if (schedule.snooze_count <= config_.reserve_snoozes) {
        return {.code = SnoozeDecisionCode::no_snoozes_available};
    }

    const bool audio_triggered = config_.use_audio_activity && activity.audio_active;
    const bool chat_triggered = config_.use_chat_activity && activity.chat_active;
    if (!audio_triggered && !chat_triggered) {
        return {
            .code = SnoozeDecisionCode::no_activity,
            .audio_triggered = false,
            .chat_triggered = false,
        };
    }

    if (handled_ad_at_ && *handled_ad_at_ == next_ad_at) {
        return {
            .code = SnoozeDecisionCode::already_handled,
            .audio_triggered = audio_triggered,
            .chat_triggered = chat_triggered,
        };
    }

    if (last_attempt_at_ && last_attempted_ad_at_ && *last_attempted_ad_at_ == next_ad_at &&
        now - *last_attempt_at_ < config_.retry_cooldown) {
        return {
            .code = SnoozeDecisionCode::retry_cooldown,
            .audio_triggered = audio_triggered,
            .chat_triggered = chat_triggered,
        };
    }

    return {
        .code = SnoozeDecisionCode::should_snooze,
        .audio_triggered = audio_triggered,
        .chat_triggered = chat_triggered,
    };
}

void SnoozeDecisionEngine::record_attempt(WallTimePoint now, WallTimePoint target_ad_at, bool succeeded) noexcept
{
    last_attempt_at_ = now;
    last_attempted_ad_at_ = target_ad_at;
    if (succeeded) {
        handled_ad_at_ = target_ad_at;
    }
}

SnoozePolicyConfig SnoozeDecisionEngine::normalize(SnoozePolicyConfig config) noexcept
{
    config.lead_time = std::max(config.lead_time, std::chrono::seconds::zero());
    config.past_due_grace = std::max(config.past_due_grace, std::chrono::seconds::zero());
    config.retry_cooldown = std::max(config.retry_cooldown, std::chrono::seconds::zero());
    config.reserve_snoozes = std::max(config.reserve_snoozes, 0);
    return config;
}

const char *to_string(SnoozeDecisionCode code) noexcept
{
    switch (code) {
    case SnoozeDecisionCode::should_snooze:
        return "should_snooze";
    case SnoozeDecisionCode::disabled:
        return "disabled";
    case SnoozeDecisionCode::no_conditions_enabled:
        return "no_conditions_enabled";
    case SnoozeDecisionCode::no_upcoming_ad:
        return "no_upcoming_ad";
    case SnoozeDecisionCode::stale_schedule:
        return "stale_schedule";
    case SnoozeDecisionCode::too_early:
        return "too_early";
    case SnoozeDecisionCode::no_snoozes_available:
        return "no_snoozes_available";
    case SnoozeDecisionCode::no_activity:
        return "no_activity";
    case SnoozeDecisionCode::retry_cooldown:
        return "retry_cooldown";
    case SnoozeDecisionCode::already_handled:
        return "already_handled";
    }
    return "unknown";
}

} // namespace adsnooze
