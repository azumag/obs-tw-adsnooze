#include "twitch/twitch-ad-client.hpp"

#include "core/rfc3339.hpp"

#include <curl/curl.h>
#include <obs-module.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

int transfer_progress(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto *stop_flag = static_cast<const std::atomic_bool *>(clientp);
    if (stop_flag != nullptr && stop_flag->load(std::memory_order_acquire)) {
        return 1;
    }
    return 0;
}

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

HttpResponse request(std::string url, const std::string &access_token, const std::string *client_id, bool post,
                     const std::atomic_bool *stop_flag)
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
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, stop_flag);
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

std::vector<std::string> extract_json_string_array(const std::string &json, const char *key)
{
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t key_position = json.find(needle);
    if (key_position == std::string::npos) {
        return {};
    }

    const std::size_t array_start = json.find('[', key_position + needle.size());
    if (array_start == std::string::npos) {
        return {};
    }

    std::vector<std::string> values;
    std::size_t position = array_start + 1;
    bool in_string = false;
    std::string current;
    while (position < json.size()) {
        const char character = json[position];
        if (in_string) {
            if (character == '\\' && position + 1 < json.size()) {
                current.push_back(json[position + 1]);
                position += 2;
                continue;
            }
            if (character == '"') {
                values.push_back(current);
                current.clear();
                in_string = false;
            } else {
                current.push_back(character);
            }
        } else if (character == '"') {
            in_string = true;
            current.clear();
        } else if (character == ']') {
            break;
        }
        ++position;
    }
    return values;
}

bool contains_json_string_array_value(const std::string &json, const char *key, std::string_view value)
{
    for (const std::string &entry : extract_json_string_array(json, key)) {
        if (entry == value) {
            return true;
        }
    }
    return false;
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
    const HttpResponse response =
        request("https://id.twitch.tv/oauth2/validate", access_token_, nullptr, false, stop_flag_);
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

    // obs_data_t intentionally ignores arrays of primitive values, so scan the raw
    // JSON scopes array instead of matching scope text anywhere in the body.
    result.value.has_read_ads_scope = contains_json_string_array_value(response.body, "scopes", "channel:read:ads");
    result.value.has_manage_ads_scope = contains_json_string_array_value(response.body, "scopes", "channel:manage:ads");
    result.ok = true;
    return result;
}

ApiResult<AdSchedule> TwitchAdClient::get_ad_schedule() const
{
    const std::string url =
        "https://api.twitch.tv/helix/channels/ads?broadcaster_id=" + broadcaster_id_;
    return parse_schedule_response(request(url, access_token_, &client_id_, false, stop_flag_), true);
}

ApiResult<AdSchedule> TwitchAdClient::snooze_next_ad() const
{
    const std::string url =
        "https://api.twitch.tv/helix/channels/ads/schedule/snooze?broadcaster_id=" + broadcaster_id_;
    return parse_schedule_response(request(url, access_token_, &client_id_, true, stop_flag_), false);
}

} // namespace adsnooze::twitch
