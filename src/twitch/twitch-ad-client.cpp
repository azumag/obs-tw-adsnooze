#include "twitch/twitch-ad-client.hpp"

#include "core/rfc3339.hpp"

#include <curl/curl.h>
#include <obs-module.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace adsnooze::twitch {
namespace {

constexpr std::size_t kMaximumResponseBytes = 1024 * 1024;

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "0.1.0"
#endif

struct HttpResponse {
    bool transport_ok{false};
    long status{0};
    std::string body;
    std::string error;
};

std::size_t write_response(char *data, std::size_t size, std::size_t count, void *user_data)
{
    auto *body = static_cast<std::string *>(user_data);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        return 0;
    }
    const std::size_t bytes = size * count;
    if (bytes > kMaximumResponseBytes || body->size() > kMaximumResponseBytes - bytes) {
        return 0;
    }
    body->append(data, bytes);
    return bytes;
}

bool append_header(curl_slist *&headers, const std::string &value)
{
    curl_slist *updated = curl_slist_append(headers, value.c_str());
    if (!updated) {
        return false;
    }
    headers = updated;
    return true;
}

HttpResponse request(std::string url, const std::string &access_token, const std::string *client_id, bool post)
{
    HttpResponse response;
    CURL *curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    curl_slist *headers = nullptr;
    const std::string authorization = "Authorization: Bearer " + access_token;
    if (!append_header(headers, authorization) || !append_header(headers, "Accept: application/json")) {
        response.error = "Could not allocate HTTP headers";
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }
    if (client_id) {
        const std::string client_id_header = "Client-Id: " + *client_id;
        if (!append_header(headers, client_id_header)) {
            response.error = "Could not allocate HTTP headers";
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return response;
        }
    }

    char curl_error[CURL_ERROR_SIZE]{};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "obs-tw-adsnooze/" PLUGIN_VERSION);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
    if (post) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    }

    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    response.transport_ok = code == CURLE_OK;
    if (!response.transport_ok) {
        response.error = curl_error[0] ? curl_error : curl_easy_strerror(code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

std::string api_error_message(const HttpResponse &response)
{
    if (!response.transport_ok) {
        return response.error.empty() ? "network request failed" : response.error;
    }

    obs_data_t *root = obs_data_create_from_json(response.body.c_str());
    if (!root) {
        return "Twitch returned an unreadable response";
    }
    const char *message = obs_data_get_string(root, "message");
    std::string result = message && *message ? message : "Twitch API request failed";
    obs_data_release(root);
    return result;
}

ApiResult<AdSchedule> parse_schedule_response(const HttpResponse &response, bool includes_duration)
{
    ApiResult<AdSchedule> result;
    result.http_status = response.status;
    if (!response.transport_ok || response.status < 200 || response.status >= 300) {
        result.error = api_error_message(response);
        return result;
    }

    obs_data_t *root = obs_data_create_from_json(response.body.c_str());
    if (!root) {
        result.error = "Twitch returned invalid JSON";
        return result;
    }

    obs_data_array_t *data = obs_data_get_array(root, "data");
    if (!data || obs_data_array_count(data) == 0) {
        result.error = "Twitch response did not contain ad schedule data";
        obs_data_array_release(data);
        obs_data_release(root);
        return result;
    }

    obs_data_t *item = obs_data_array_item(data, 0);
    if (!item) {
        result.error = "Twitch response contained an unreadable ad schedule item";
        obs_data_array_release(data);
        obs_data_release(root);
        return result;
    }
    const char *next_ad_at = obs_data_get_string(item, "next_ad_at");
    if (next_ad_at && *next_ad_at) {
        result.value.next_ad_at = parse_rfc3339(next_ad_at);
        if (!result.value.next_ad_at) {
            result.error = "Twitch returned an invalid next_ad_at timestamp";
        }
    }
    result.value.snooze_count = static_cast<int>(std::clamp<long long>(
        obs_data_get_int(item, "snooze_count"), 0, std::numeric_limits<int>::max()));
    if (includes_duration) {
        result.value.duration = std::chrono::seconds{static_cast<std::chrono::seconds::rep>(
            std::max<long long>(obs_data_get_int(item, "duration"), 0))};
    }

    obs_data_release(item);
    obs_data_array_release(data);
    obs_data_release(root);

    if (!result.error.empty()) {
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace

TwitchAdClient::TwitchAdClient(std::string client_id, std::string access_token, std::string broadcaster_id)
    : client_id_(std::move(client_id)), access_token_(std::move(access_token)),
      broadcaster_id_(std::move(broadcaster_id))
{
}

ApiResult<TokenValidation> TwitchAdClient::validate_token() const
{
    const HttpResponse response = request("https://id.twitch.tv/oauth2/validate", access_token_, nullptr, false);
    ApiResult<TokenValidation> result;
    result.http_status = response.status;
    if (!response.transport_ok || response.status < 200 || response.status >= 300) {
        result.error = api_error_message(response);
        return result;
    }

    obs_data_t *root = obs_data_create_from_json(response.body.c_str());
    if (!root) {
        result.error = "Twitch returned invalid token validation JSON";
        return result;
    }

    const char *client_id = obs_data_get_string(root, "client_id");
    const char *user_id = obs_data_get_string(root, "user_id");
    result.value.client_id = client_id ? client_id : "";
    result.value.user_id = user_id ? user_id : "";
    obs_data_release(root);

    // obs_data_t intentionally ignores arrays of primitive values, so inspect the
    // original JSON for the two exact scope strings returned by Twitch.
    result.value.has_read_ads_scope = response.body.find("\"channel:read:ads\"") != std::string::npos;
    result.value.has_manage_ads_scope = response.body.find("\"channel:manage:ads\"") != std::string::npos;
    result.ok = true;
    return result;
}

ApiResult<AdSchedule> TwitchAdClient::get_ad_schedule() const
{
    const std::string url =
        "https://api.twitch.tv/helix/channels/ads?broadcaster_id=" + broadcaster_id_;
    return parse_schedule_response(request(url, access_token_, &client_id_, false), true);
}

ApiResult<AdSchedule> TwitchAdClient::snooze_next_ad() const
{
    const std::string url =
        "https://api.twitch.tv/helix/channels/ads/schedule/snooze?broadcaster_id=" + broadcaster_id_;
    return parse_schedule_response(request(url, access_token_, &client_id_, true), false);
}

} // namespace adsnooze::twitch
