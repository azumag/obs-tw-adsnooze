#pragma once

#include "core/snooze-policy.hpp"

#include <atomic>
#include <string>

namespace adsnooze::twitch {

template<typename T> struct ApiResult {
    bool ok{false};
    T value{};
    long http_status{0};
    std::string error;
};

struct TokenValidation {
    std::string client_id;
    std::string user_id;
    bool has_read_ads_scope{false};
    bool has_manage_ads_scope{false};
};

class TwitchAdClient {
public:
    TwitchAdClient(std::string client_id, std::string access_token, std::string broadcaster_id);

    // The runtime owns the flag; the client only reads it to abort in-flight requests.
    void set_stop_flag(const std::atomic_bool &flag) noexcept { stop_flag_ = &flag; }

    [[nodiscard]] ApiResult<TokenValidation> validate_token() const;
    [[nodiscard]] ApiResult<AdSchedule> get_ad_schedule() const;
    [[nodiscard]] ApiResult<AdSchedule> snooze_next_ad() const;

private:
    std::string client_id_;
    std::string access_token_;
    std::string broadcaster_id_;
    const std::atomic_bool *stop_flag_{nullptr};
};

} // namespace adsnooze::twitch
