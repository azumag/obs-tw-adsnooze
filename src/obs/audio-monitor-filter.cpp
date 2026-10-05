#include "obs/audio-monitor-filter.hpp"

#include "core/audio-activity-detector.hpp"
#include "obs/audio-monitor-registry.hpp"

#include <obs-module.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr const char *kSettingEnabled = "enabled";
constexpr const char *kSettingLabel = "label";
constexpr const char *kSettingActivationThreshold = "activation_threshold_dbfs";
constexpr const char *kSettingDeactivationThreshold = "deactivation_threshold_dbfs";
constexpr const char *kSettingAttackMs = "attack_ms";
constexpr const char *kSettingHoldMs = "hold_ms";

struct FilterData {
    obs_source_t *context{nullptr};
    adsnooze::AudioActivityDetector detector;
    std::shared_ptr<adsnooze::obs_plugin::AudioMonitorState> state;
    std::atomic_bool enabled{true};
    std::atomic<float> activation_threshold_dbfs{-35.0F};
    std::atomic<float> deactivation_threshold_dbfs{-40.0F};
    std::atomic<std::int64_t> attack_ms{80};
    std::atomic<std::int64_t> hold_ms{1500};
    std::atomic<std::uint64_t> config_revision{0};
    std::uint64_t applied_config_revision{0};
    std::string configured_label;
    std::string parent_name;
};

const char *filter_name(void *)
{
    return obs_module_text("AudioMonitorFilter.Name");
}

void update_state_label(FilterData &filter)
{
    if (!filter.configured_label.empty()) {
        filter.state->set_label(filter.configured_label);
    } else if (!filter.parent_name.empty()) {
        filter.state->set_label(filter.parent_name);
    } else {
        filter.state->set_label(obs_module_text("AudioMonitorFilter.UnknownSource"));
    }
}

void filter_update(void *data, obs_data_t *settings)
{
    auto *filter = static_cast<FilterData *>(data);
    if (!filter || !settings) {
        return;
    }

    filter->configured_label = obs_data_get_string(settings, kSettingLabel);
    const auto attack_ms = static_cast<std::int64_t>(obs_data_get_int(settings, kSettingAttackMs));
    const auto hold_ms = static_cast<std::int64_t>(obs_data_get_int(settings, kSettingHoldMs));
    filter->activation_threshold_dbfs.store(
        static_cast<float>(obs_data_get_double(settings, kSettingActivationThreshold)), std::memory_order_relaxed);
    filter->deactivation_threshold_dbfs.store(
        static_cast<float>(obs_data_get_double(settings, kSettingDeactivationThreshold)), std::memory_order_relaxed);
    filter->attack_ms.store(attack_ms, std::memory_order_relaxed);
    filter->hold_ms.store(hold_ms, std::memory_order_relaxed);
    filter->enabled.store(obs_data_get_bool(settings, kSettingEnabled), std::memory_order_relaxed);
    filter->state->set_stale_after(std::chrono::milliseconds{std::max<std::int64_t>(hold_ms + 500, 2000)});
    filter->config_revision.fetch_add(1, std::memory_order_release);
    filter->state->update(false, -120.0F, adsnooze::MonotonicClock::now());
    update_state_label(*filter);
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
    auto *filter = new (std::nothrow) FilterData;
    if (!filter) {
        return nullptr;
    }

    filter->context = source;
    filter->state = std::make_shared<adsnooze::obs_plugin::AudioMonitorState>();
    adsnooze::obs_plugin::AudioMonitorRegistry::instance().add(filter->state);
    filter_update(filter, settings);
    return filter;
}

void filter_destroy(void *data)
{
    auto *filter = static_cast<FilterData *>(data);
    if (!filter) {
        return;
    }

    adsnooze::obs_plugin::AudioMonitorRegistry::instance().remove(filter->state);
    delete filter;
}

void filter_add(void *data, obs_source_t *source)
{
    auto *filter = static_cast<FilterData *>(data);
    if (!filter || !source) {
        return;
    }

    const char *name = obs_source_get_name(source);
    filter->parent_name = name ? name : "";
    update_state_label(*filter);
}

void filter_remove(void *data, obs_source_t *)
{
    auto *filter = static_cast<FilterData *>(data);
    if (filter) {
        filter->state->update(false, -120.0F, adsnooze::MonotonicClock::now());
    }
}

obs_audio_data *filter_audio(void *data, obs_audio_data *audio)
{
    auto *filter = static_cast<FilterData *>(data);
    if (!filter || !audio) {
        return audio;
    }

    const auto now = adsnooze::MonotonicClock::now();
    const std::uint64_t revision = filter->config_revision.load(std::memory_order_acquire);
    if (revision != filter->applied_config_revision) {
        filter->detector.configure({
            .activation_threshold_dbfs = filter->activation_threshold_dbfs.load(std::memory_order_relaxed),
            .deactivation_threshold_dbfs = filter->deactivation_threshold_dbfs.load(std::memory_order_relaxed),
            .attack = std::chrono::milliseconds{filter->attack_ms.load(std::memory_order_relaxed)},
            .hold = std::chrono::milliseconds{filter->hold_ms.load(std::memory_order_relaxed)},
        });
        filter->applied_config_revision = revision;
    }

    if (!filter->enabled.load(std::memory_order_relaxed)) {
        filter->state->update(false, -120.0F, now);
        return audio;
    }

    const float *channels[MAX_AV_PLANES]{};
    std::size_t channel_count = 0;
    for (; channel_count < MAX_AV_PLANES && audio->data[channel_count]; ++channel_count) {
        channels[channel_count] = reinterpret_cast<const float *>(audio->data[channel_count]);
    }

    obs_source_t *parent = obs_filter_get_parent(filter->context);
    const bool muted = parent && obs_source_muted(parent);
    const auto result = filter->detector.process(channels, channel_count, audio->frames, now, muted);
    filter->state->update(result.active, result.level_dbfs, now);
    return audio;
}

void filter_defaults(obs_data_t *settings)
{
    obs_data_set_default_bool(settings, kSettingEnabled, true);
    obs_data_set_default_string(settings, kSettingLabel, "");
    obs_data_set_default_double(settings, kSettingActivationThreshold, -35.0);
    obs_data_set_default_double(settings, kSettingDeactivationThreshold, -40.0);
    obs_data_set_default_int(settings, kSettingAttackMs, 80);
    obs_data_set_default_int(settings, kSettingHoldMs, 1500);
}

obs_properties_t *filter_properties(void *)
{
    obs_properties_t *properties = obs_properties_create();
    obs_properties_add_bool(properties, kSettingEnabled, obs_module_text("AudioMonitorFilter.Enabled"));
    obs_properties_add_text(properties, kSettingLabel, obs_module_text("AudioMonitorFilter.Label"), OBS_TEXT_DEFAULT);

    obs_property_t *activation = obs_properties_add_float_slider(
        properties, kSettingActivationThreshold, obs_module_text("AudioMonitorFilter.ActivationThreshold"), -90.0,
        0.0, 0.5);
    obs_property_float_set_suffix(activation, " dBFS");

    obs_property_t *deactivation = obs_properties_add_float_slider(
        properties, kSettingDeactivationThreshold, obs_module_text("AudioMonitorFilter.DeactivationThreshold"),
        -90.0, 0.0, 0.5);
    obs_property_float_set_suffix(deactivation, " dBFS");

    obs_property_t *attack = obs_properties_add_int_slider(properties, kSettingAttackMs,
                                                            obs_module_text("AudioMonitorFilter.Attack"), 0, 2000, 10);
    obs_property_int_set_suffix(attack, " ms");

    obs_property_t *hold = obs_properties_add_int_slider(properties, kSettingHoldMs,
                                                          obs_module_text("AudioMonitorFilter.Hold"), 0, 10000, 50);
    obs_property_int_set_suffix(hold, " ms");
    return properties;
}

} // namespace

void register_adsnooze_audio_monitor_filter()
{
    static obs_source_info info{};
    info.id = "obs_tw_adsnooze_audio_monitor";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = filter_name;
    info.create = filter_create;
    info.destroy = filter_destroy;
    info.update = filter_update;
    info.get_defaults = filter_defaults;
    info.get_properties = filter_properties;
    info.filter_audio = filter_audio;
    info.filter_add = filter_add;
    info.filter_remove = filter_remove;
    obs_register_source(&info);
}
