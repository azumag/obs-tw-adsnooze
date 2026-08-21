#pragma once

#include "core/snooze-policy.hpp"

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

    [[nodiscard]] ApiResult<TokenValidation> validate_token() const;
    [[nodiscard]] ApiResult<AdSchedule> get_ad_schedule() const;
    [[nodiscard]] ApiResult<AdSchedule> snooze_next_ad() const;

private:
    std::string client_id_;
    std::string access_token_;
    std::string broadcaster_id_;
};

} // namespace adsnooze::twitch
