#pragma once

#include "core/time.hpp"

#include <chrono>
#include <cstddef>
#include <optional>

namespace adsnooze {

struct AdSchedule {
    std::optional<WallTimePoint> next_ad_at;
    int snooze_count{0};
    std::chrono::seconds duration{0};
};

struct CombinedActivitySnapshot {
    bool audio_active{false};
    std::size_t active_audio_sources{0};
    bool chat_active{false};
    std::size_t chat_messages{0};
    std::size_t unique_chatters{0};
};

struct SnoozePolicyConfig {
    bool enabled{false};
    bool use_audio_activity{true};
    bool use_chat_activity{true};
    std::chrono::seconds lead_time{90};
    std::chrono::seconds past_due_grace{15};
    std::chrono::seconds retry_cooldown{30};
    int reserve_snoozes{0};
};

enum class SnoozeDecisionCode {
    should_snooze,
    disabled,
    no_conditions_enabled,
    no_upcoming_ad,
    stale_schedule,
    too_early,
    no_snoozes_available,
    no_activity,
    retry_cooldown,
    already_handled,
};

struct SnoozeDecision {
    SnoozeDecisionCode code{SnoozeDecisionCode::disabled};
    bool audio_triggered{false};
    bool chat_triggered{false};

    [[nodiscard]] bool should_snooze() const noexcept { return code == SnoozeDecisionCode::should_snooze; }
};

class SnoozeDecisionEngine {
public:
    explicit SnoozeDecisionEngine(SnoozePolicyConfig config = {});

    void configure(SnoozePolicyConfig config) noexcept;
    void reset() noexcept;

    [[nodiscard]] SnoozeDecision evaluate(WallTimePoint now, const AdSchedule &schedule,
                                           const CombinedActivitySnapshot &activity) const noexcept;

    void record_attempt(WallTimePoint now, WallTimePoint target_ad_at, bool succeeded) noexcept;

    [[nodiscard]] const SnoozePolicyConfig &config() const noexcept { return config_; }

private:
    static SnoozePolicyConfig normalize(SnoozePolicyConfig config) noexcept;

    SnoozePolicyConfig config_;
    std::optional<WallTimePoint> last_attempt_at_;
    std::optional<WallTimePoint> last_attempted_ad_at_;
    std::optional<WallTimePoint> handled_ad_at_;
};

[[nodiscard]] const char *to_string(SnoozeDecisionCode code) noexcept;

} // namespace adsnooze
