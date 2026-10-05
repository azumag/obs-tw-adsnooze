#include "core/audio-activity-detector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace adsnooze {
namespace {
constexpr float kFloorDbfs = -120.0F;
constexpr double kMinimumRms = 1.0e-6;
} // namespace

AudioActivityDetector::AudioActivityDetector(AudioActivityConfig config) : config_(normalize(config)) {}

void AudioActivityDetector::configure(AudioActivityConfig config) noexcept
{
    config_ = normalize(config);
    reset();
}

void AudioActivityDetector::reset() noexcept
{
    active_ = false;
    above_since_.reset();
    last_above_close_threshold_.reset();
}

AudioActivityResult AudioActivityDetector::process(const float *const *channels, std::size_t channel_count,
                                                    std::size_t frame_count, MonotonicTimePoint now,
                                                    bool muted) noexcept
{
    const float level_dbfs = muted ? kFloorDbfs : calculate_rms_dbfs(channels, channel_count, frame_count);
    const bool was_active = active_;

    // A muted OBS source is not audible to the audience. Clear immediately
    // instead of applying the normal release hold, which is intended only to
    // smooth short gaps in audible material.
    if (muted) {
        reset();
        return AudioActivityResult{
            .active = false,
            .changed = was_active,
            .level_dbfs = level_dbfs,
        };
    }

    if (!active_) {
        const bool above_open_threshold = level_dbfs >= config_.activation_threshold_dbfs;
        if (!above_open_threshold) {
            above_since_.reset();
        } else {
            if (!above_since_) {
                above_since_ = now;
            }

            if (now - *above_since_ >= config_.attack) {
                active_ = true;
                last_above_close_threshold_ = now;
            }
        }
    } else {
        const bool above_close_threshold = level_dbfs >= config_.deactivation_threshold_dbfs;
        if (above_close_threshold) {
            last_above_close_threshold_ = now;
        } else if (!last_above_close_threshold_ || now - *last_above_close_threshold_ >= config_.hold) {
            active_ = false;
            above_since_.reset();
            last_above_close_threshold_.reset();
        }
    }

    return AudioActivityResult{
        .active = active_,
        .changed = active_ != was_active,
        .level_dbfs = level_dbfs,
    };
}

float AudioActivityDetector::calculate_rms_dbfs(const float *const *channels, std::size_t channel_count,
                                                  std::size_t frame_count) noexcept
{
    if (!channels || channel_count == 0 || frame_count == 0) {
        return kFloorDbfs;
    }

    long double sum_squares = 0.0L;
    std::size_t sample_count = 0;

    for (std::size_t channel = 0; channel < channel_count; ++channel) {
        const float *samples = channels[channel];
        if (!samples) {
            continue;
        }

        for (std::size_t frame = 0; frame < frame_count; ++frame) {
            const long double sample = static_cast<long double>(samples[frame]);
            sum_squares += sample * sample;
        }
        sample_count += frame_count;
    }

    if (sample_count == 0) {
        return kFloorDbfs;
    }

    const double mean_square = static_cast<double>(sum_squares / static_cast<long double>(sample_count));
    const double rms = std::sqrt(std::max(mean_square, 0.0));
    if (!std::isfinite(rms) || rms < kMinimumRms) {
        return kFloorDbfs;
    }

    const double dbfs = 20.0 * std::log10(rms);
    if (!std::isfinite(dbfs)) {
        return kFloorDbfs;
    }

    return static_cast<float>(std::max(dbfs, static_cast<double>(kFloorDbfs)));
}

AudioActivityConfig AudioActivityDetector::normalize(AudioActivityConfig config) noexcept
{
    config.activation_threshold_dbfs = std::clamp(config.activation_threshold_dbfs, -120.0F, 0.0F);
    config.deactivation_threshold_dbfs = std::clamp(config.deactivation_threshold_dbfs, -120.0F, 0.0F);
    if (config.deactivation_threshold_dbfs > config.activation_threshold_dbfs) {
        config.deactivation_threshold_dbfs = config.activation_threshold_dbfs;
    }
    config.attack = std::max(config.attack, std::chrono::milliseconds::zero());
    config.hold = std::max(config.hold, std::chrono::milliseconds::zero());
    return config;
}

} // namespace adsnooze
