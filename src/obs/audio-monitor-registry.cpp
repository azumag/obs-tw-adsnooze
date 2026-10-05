#include "obs/audio-monitor-registry.hpp"

#include <algorithm>
#include <utility>

namespace adsnooze::obs_plugin {

void AudioMonitorState::update(bool active, float level_dbfs, MonotonicTimePoint now) noexcept
{
    last_update_ticks_.store(now.time_since_epoch().count(), std::memory_order_relaxed);
    level_dbfs_.store(level_dbfs, std::memory_order_relaxed);
    active_.store(active, std::memory_order_release);
    has_update_.store(true, std::memory_order_release);
    sequence_.fetch_add(1, std::memory_order_relaxed);
}

void AudioMonitorState::set_stale_after(std::chrono::milliseconds timeout) noexcept
{
    timeout = std::max(timeout, std::chrono::milliseconds::zero());
    stale_after_ticks_.store(std::chrono::duration_cast<MonotonicClock::duration>(timeout).count(),
                             std::memory_order_release);
}

bool AudioMonitorState::fresh(MonotonicTimePoint now) const noexcept
{
    if (!has_update_.load(std::memory_order_acquire)) {
        return false;
    }

    const TickRep last_update = last_update_ticks_.load(std::memory_order_relaxed);
    const TickRep current = now.time_since_epoch().count();
    if (current < last_update) {
        return false;
    }

    return current - last_update <= stale_after_ticks_.load(std::memory_order_acquire);
}

bool AudioMonitorState::active(MonotonicTimePoint now) const noexcept
{
    return active_.load(std::memory_order_acquire) && fresh(now);
}

void AudioMonitorState::set_label(std::string label)
{
    std::lock_guard lock(label_mutex_);
    label_ = std::move(label);
}

std::string AudioMonitorState::label() const
{
    std::lock_guard lock(label_mutex_);
    return label_;
}

AudioMonitorRegistry &AudioMonitorRegistry::instance()
{
    static AudioMonitorRegistry registry;
    return registry;
}

void AudioMonitorRegistry::add(const std::shared_ptr<AudioMonitorState> &state)
{
    std::lock_guard lock(mutex_);
    states_.push_back(state);
}

void AudioMonitorRegistry::remove(const std::shared_ptr<AudioMonitorState> &state)
{
    std::lock_guard lock(mutex_);
    states_.erase(std::remove_if(states_.begin(), states_.end(), [&state](const auto &candidate) {
                      const auto locked = candidate.lock();
                      return !locked || locked == state;
                  }),
                  states_.end());
}

std::vector<AudioMonitorSnapshot> AudioMonitorRegistry::snapshot() const
{
    std::vector<std::shared_ptr<AudioMonitorState>> active_states;
    {
        std::lock_guard lock(mutex_);
        active_states.reserve(states_.size());
        for (const auto &candidate : states_) {
            if (auto state = candidate.lock()) {
                active_states.push_back(std::move(state));
            }
        }
    }

    const MonotonicTimePoint now = MonotonicClock::now();
    std::vector<AudioMonitorSnapshot> result;
    result.reserve(active_states.size());
    for (const auto &state : active_states) {
        const bool is_fresh = state->fresh(now);
        result.push_back(AudioMonitorSnapshot{
            .label = state->label(),
            .active = is_fresh && state->active(now),
            .fresh = is_fresh,
            .level_dbfs = is_fresh ? state->level_dbfs() : -120.0F,
            .sequence = state->sequence(),
        });
    }
    return result;
}

} // namespace adsnooze::obs_plugin
