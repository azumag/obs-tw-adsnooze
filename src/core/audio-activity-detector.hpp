#pragma once

#include "core/time.hpp"

#include <chrono>
#include <cstddef>
#include <optional>

namespace adsnooze {

struct AudioActivityConfig {
    float activation_threshold_dbfs{-35.0F};
    float deactivation_threshold_dbfs{-40.0F};
    std::chrono::milliseconds attack{80};
    std::chrono::milliseconds hold{1500};
};

struct AudioActivityResult {
    bool active{false};
    bool changed{false};
    float level_dbfs{-120.0F};
};

class AudioActivityDetector {
public:
    explicit AudioActivityDetector(AudioActivityConfig config = {});

    void configure(AudioActivityConfig config) noexcept;
    void reset() noexcept;

    [[nodiscard]] AudioActivityResult process(const float *const *channels, std::size_t channel_count,
                                               std::size_t frame_count, MonotonicTimePoint now,
                                               bool muted = false) noexcept;

    [[nodiscard]] const AudioActivityConfig &config() const noexcept { return config_; }
    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] static float calculate_rms_dbfs(const float *const *channels, std::size_t channel_count,
                                                   std::size_t frame_count) noexcept;

private:
    static AudioActivityConfig normalize(AudioActivityConfig config) noexcept;

    AudioActivityConfig config_;
    bool active_{false};
    std::optional<MonotonicTimePoint> above_since_;
    std::optional<MonotonicTimePoint> last_above_close_threshold_;
};

} // namespace adsnooze
