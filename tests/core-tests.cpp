#include "core/audio-activity-detector.hpp"
#include "core/chat-activity-tracker.hpp"
#include "core/rfc3339.hpp"
#include "core/snooze-policy.hpp"
#include "obs/audio-monitor-registry.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void test_audio_rms()
{
    std::array<float, 4> samples{0.5F, -0.5F, 0.5F, -0.5F};
    const float *channels[] = {samples.data()};
    const float dbfs = adsnooze::AudioActivityDetector::calculate_rms_dbfs(channels, 1, samples.size());
    expect(std::abs(dbfs - (-6.0206F)) < 0.01F, "0.5 amplitude should be approximately -6.02 dBFS");
}

void test_audio_attack_hold_and_hysteresis()
{
    using namespace std::chrono_literals;

    adsnooze::AudioActivityDetector detector({
        .activation_threshold_dbfs = -20.0F,
        .deactivation_threshold_dbfs = -30.0F,
        .attack = 100ms,
        .hold = 500ms,
    });

    std::array<float, 8> loud{};
    loud.fill(0.2F); // Approximately -14 dBFS.
    std::array<float, 8> quiet{};
    quiet.fill(0.001F); // Approximately -60 dBFS.
    const float *loud_channels[] = {loud.data()};
    const float *quiet_channels[] = {quiet.data()};

    const auto t0 = adsnooze::MonotonicTimePoint{};
    expect(!detector.process(loud_channels, 1, loud.size(), t0).active, "attack should suppress the first loud packet");
    expect(!detector.process(loud_channels, 1, loud.size(), t0 + 99ms).active,
           "attack should remain inactive before its duration");
    const auto opened = detector.process(loud_channels, 1, loud.size(), t0 + 100ms);
    expect(opened.active && opened.changed, "detector should activate after attack duration");

    expect(detector.process(quiet_channels, 1, quiet.size(), t0 + 400ms).active,
           "hold should keep activity after the level drops");
    expect(detector.process(quiet_channels, 1, quiet.size(), t0 + 599ms).active,
           "hold should remain active just before expiry");
    const auto closed = detector.process(quiet_channels, 1, quiet.size(), t0 + 600ms);
    expect(!closed.active && closed.changed, "detector should deactivate when hold expires");

    (void)detector.process(loud_channels, 1, loud.size(), t0 + 1s);
    expect(detector.process(loud_channels, 1, loud.size(), t0 + 1100ms).active,
           "detector should reactivate for the mute test");
    const auto muted = detector.process(loud_channels, 1, loud.size(), t0 + 1101ms, true);
    expect(!muted.active && muted.changed, "muting a source should clear activity immediately");
}

void test_chat_window_and_deduplication()
{
    using namespace std::chrono_literals;

    adsnooze::ChatActivityTracker tracker({
        .window = 30s,
        .minimum_messages = 3,
        .minimum_unique_chatters = 2,
        .hold = 5s,
    });
    const auto t0 = adsnooze::MonotonicTimePoint{};

    expect(tracker.record_message("m1", "u1", t0), "first chat message should be accepted");
    expect(!tracker.record_message("m1", "u1", t0 + 1s), "duplicate message ID should be ignored");
    tracker.record_message("m2", "u1", t0 + 2s);
    tracker.record_message("m3", "u2", t0 + 3s);

    auto active = tracker.snapshot(t0 + 3s);
    expect(active.active, "chat should be active after reaching both thresholds");
    expect(active.message_count == 3, "chat message count should exclude duplicates");
    expect(active.unique_chatter_count == 2, "chat unique chatter count should be calculated");

    auto expired = tracker.snapshot(t0 + 40s);
    expect(!expired.active, "chat should become inactive after window and hold expire");
    expect(expired.message_count == 0, "expired messages should be removed");

    adsnooze::ChatActivityTracker disabled_thresholds({
        .window = 30s,
        .minimum_messages = 0,
        .minimum_unique_chatters = 0,
        .hold = 5s,
    });
    expect(!disabled_thresholds.snapshot(t0).active,
           "disabling both chat thresholds should not make an empty chat permanently active");
}

void test_snooze_policy()
{
    using namespace std::chrono_literals;

    adsnooze::SnoozeDecisionEngine engine({
        .enabled = true,
        .use_audio_activity = true,
        .use_chat_activity = true,
        .lead_time = 90s,
        .past_due_grace = 15s,
        .retry_cooldown = 30s,
        .reserve_snoozes = 1,
    });

    const auto now = adsnooze::WallTimePoint{1000s};
    const adsnooze::CombinedActivitySnapshot active_audio{
        .audio_active = true,
        .active_audio_sources = 1,
    };

    adsnooze::AdSchedule schedule{
        .next_ad_at = now + 60s,
        .snooze_count = 2,
        .duration = 60s,
    };

    const auto decision = engine.evaluate(now, schedule, active_audio);
    expect(decision.should_snooze(), "active audio near an ad should request a snooze");
    expect(decision.audio_triggered, "decision should identify the audio reason");

    engine.record_attempt(now, *schedule.next_ad_at, true);
    expect(engine.evaluate(now + 1s, schedule, active_audio).code == adsnooze::SnoozeDecisionCode::already_handled,
           "a successfully handled schedule should not be snoozed twice");

    schedule.next_ad_at = now + 5min;
    expect(engine.evaluate(now + 2s, schedule, active_audio).code == adsnooze::SnoozeDecisionCode::too_early,
           "an ad outside the lead window should not be snoozed");

    schedule.next_ad_at = now + 60s;
    schedule.snooze_count = 1;
    engine.reset();
    expect(engine.evaluate(now, schedule, active_audio).code == adsnooze::SnoozeDecisionCode::no_snoozes_available,
           "reserved snoozes should be preserved");

    schedule.snooze_count = 2;
    engine.reset();
    engine.record_attempt(now, *schedule.next_ad_at, false);
    expect(engine.evaluate(now + 1s, schedule, active_audio).code == adsnooze::SnoozeDecisionCode::retry_cooldown,
           "a failed request should enter retry cooldown");
    expect(engine.evaluate(now + 30s, schedule, active_audio).should_snooze(),
           "the same ad may be retried when cooldown expires");

    const adsnooze::CombinedActivitySnapshot active_chat{
        .chat_active = true,
        .chat_messages = 20,
        .unique_chatters = 8,
    };
    engine.reset();
    const auto chat_decision = engine.evaluate(now, schedule, active_chat);
    expect(chat_decision.should_snooze() && chat_decision.chat_triggered,
           "chat activity should independently trigger an ANY policy");

    engine.reset();
    schedule.next_ad_at = now - 16s;
    expect(engine.evaluate(now, schedule, active_audio).code == adsnooze::SnoozeDecisionCode::stale_schedule,
           "an ad outside the past-due grace should be rejected");
}

void test_audio_monitor_stale_protection()
{
    using namespace std::chrono_literals;

    adsnooze::obs_plugin::AudioMonitorState state;
    state.set_stale_after(2s);
    const auto t0 = adsnooze::MonotonicTimePoint{};
    state.update(true, -12.0F, t0);

    expect(state.active(t0 + 1999ms), "a recently updated active source should remain active");
    expect(!state.active(t0 + 2001ms), "a source that stopped producing packets should become stale");

    state.update(false, -120.0F, t0 + 3s);
    expect(!state.active(t0 + 3s), "an explicit inactive update should remain inactive");
}

void test_rfc3339_parser()
{
    using namespace std::chrono_literals;

    const auto utc = adsnooze::parse_rfc3339("1970-01-01T00:00:00Z");
    expect(utc.has_value() && utc->time_since_epoch() == 0s, "Unix epoch should parse");

    const auto offset = adsnooze::parse_rfc3339("1970-01-01T09:00:00+09:00");
    expect(offset.has_value() && offset->time_since_epoch() == 0s, "timezone offset should be normalized to UTC");

    const auto fractional = adsnooze::parse_rfc3339("1970-01-01T00:00:00.123456789Z");
    expect(fractional.has_value() && fractional->time_since_epoch() == 123456789ns,
           "fractional seconds should retain nanosecond precision");

    expect(!adsnooze::parse_rfc3339("2026-02-30T00:00:00Z").has_value(), "invalid civil dates should fail");
    expect(!adsnooze::parse_rfc3339("2026-08-21T00:00:00.Z").has_value(),
           "fractional seconds require at least one digit");
}

} // namespace

int main()
{
    test_audio_rms();
    test_audio_attack_hold_and_hysteresis();
    test_chat_window_and_deduplication();
    test_snooze_policy();
    test_audio_monitor_stale_protection();
    test_rfc3339_parser();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
