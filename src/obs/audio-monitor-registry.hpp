#pragma once

#include "core/time.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace adsnooze::obs_plugin {

class AudioMonitorState {
public:
    void update(bool active, float level_dbfs, MonotonicTimePoint now) noexcept;
    void set_stale_after(std::chrono::milliseconds timeout) noexcept;
    void set_label(std::string label);

    [[nodiscard]] bool active(MonotonicTimePoint now) const noexcept;
    [[nodiscard]] bool fresh(MonotonicTimePoint now) const noexcept;
    [[nodiscard]] float level_dbfs() const noexcept { return level_dbfs_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::string label() const;

private:
    using TickRep = MonotonicClock::duration::rep;

    std::atomic_bool active_{false};
    std::atomic<float> level_dbfs_{-120.0F};
    std::atomic<TickRep> last_update_ticks_{0};
    std::atomic<TickRep> stale_after_ticks_{
        std::chrono::duration_cast<MonotonicClock::duration>(std::chrono::seconds{2}).count()};
    std::atomic_bool has_update_{false};
    std::atomic<std::uint64_t> sequence_{0};
    mutable std::mutex label_mutex_;
    std::string label_;
};

struct AudioMonitorSnapshot {
    std::string label;
    bool active{false};
    bool fresh{false};
    float level_dbfs{-120.0F};
    std::uint64_t sequence{0};
};

class AudioMonitorRegistry {
public:
    static AudioMonitorRegistry &instance();

    void add(const std::shared_ptr<AudioMonitorState> &state);
    void remove(const std::shared_ptr<AudioMonitorState> &state);
    [[nodiscard]] std::vector<AudioMonitorSnapshot> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::weak_ptr<AudioMonitorState>> states_;
};

} // namespace adsnooze::obs_plugin
