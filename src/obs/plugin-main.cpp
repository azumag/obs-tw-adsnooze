#include "obs/audio-monitor-filter.hpp"
#include "obs/plugin-runtime.hpp"

#include <curl/curl.h>
#include <obs-module.h>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("azumag")
OBS_MODULE_USE_DEFAULT_LOCALE("obs-tw-adsnooze", "en-US")

namespace {
std::unique_ptr<adsnooze::obs_plugin::PluginRuntime> runtime;
bool curl_initialized = false;
} // namespace

bool obs_module_load()
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        blog(LOG_ERROR, "[obs-tw-adsnooze] Failed to initialize HTTP client");
        return false;
    }
    curl_initialized = true;

    register_adsnooze_audio_monitor_filter();
    blog(LOG_INFO, "[obs-tw-adsnooze] Plugin loaded (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_post_load()
{
    runtime = std::make_unique<adsnooze::obs_plugin::PluginRuntime>();
    runtime->start();
}

void obs_module_unload()
{
    if (runtime) {
        runtime->stop();
        runtime.reset();
    }

    if (curl_initialized) {
        curl_global_cleanup();
        curl_initialized = false;
    }
    blog(LOG_INFO, "[obs-tw-adsnooze] Plugin unloaded");
}

const char *obs_module_description()
{
    return "Snoozes upcoming Twitch ads when selected OBS audio sources or chat activity indicate a busy moment.";
}
