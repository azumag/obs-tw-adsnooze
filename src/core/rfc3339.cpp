#include "core/rfc3339.hpp"

#include <chrono>
#include <cstdint>

namespace adsnooze {
namespace {

bool parse_digits(std::string_view value, std::size_t offset, std::size_t count, int &result) noexcept
{
    if (offset + count > value.size()) {
        return false;
    }

    int parsed = 0;
    for (std::size_t index = offset; index < offset + count; ++index) {
        const char character = value[index];
        if (character < '0' || character > '9') {
            return false;
        }
        parsed = parsed * 10 + (character - '0');
    }
    result = parsed;
    return true;
}

bool is_leap_year(int year) noexcept
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int days_in_month(int year, int month) noexcept
{
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

// Howard Hinnant's civil-date conversion, returning days since 1970-01-01.
std::int64_t days_from_civil(int year, unsigned month, unsigned day) noexcept
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year = (153 * (month + (month > 2 ? -3U : 9U)) + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

} // namespace

std::optional<WallTimePoint> parse_rfc3339(std::string_view value) noexcept
{
    // Minimum form: 2026-08-21T12:34:56Z
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' || (value[10] != 'T' && value[10] != 't') ||
        value[13] != ':' || value[16] != ':') {
        return std::nullopt;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!parse_digits(value, 0, 4, year) || !parse_digits(value, 5, 2, month) ||
        !parse_digits(value, 8, 2, day) || !parse_digits(value, 11, 2, hour) ||
        !parse_digits(value, 14, 2, minute) || !parse_digits(value, 17, 2, second)) {
        return std::nullopt;
    }

    if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month) || hour > 23 || minute > 59 ||
        second > 60) {
        return std::nullopt;
    }

    std::size_t timezone_offset = 19;
    std::chrono::nanoseconds fractional{0};
    if (timezone_offset < value.size() && value[timezone_offset] == '.') {
        ++timezone_offset;
        std::int64_t nanoseconds = 0;
        int digits = 0;
        const std::size_t fractional_start = timezone_offset;
        while (timezone_offset < value.size() && value[timezone_offset] >= '0' && value[timezone_offset] <= '9') {
            if (digits < 9) {
                nanoseconds = nanoseconds * 10 + (value[timezone_offset] - '0');
                ++digits;
            }
            ++timezone_offset;
        }
        if (timezone_offset == fractional_start) {
            return std::nullopt;
        }
        while (digits < 9) {
            nanoseconds *= 10;
            ++digits;
        }
        fractional = std::chrono::nanoseconds{nanoseconds};
    }

    int timezone_seconds = 0;
    if (timezone_offset >= value.size()) {
        return std::nullopt;
    }

    const char timezone_marker = value[timezone_offset];
    if (timezone_marker == 'Z' || timezone_marker == 'z') {
        if (timezone_offset + 1 != value.size()) {
            return std::nullopt;
        }
    } else if (timezone_marker == '+' || timezone_marker == '-') {
        int offset_hour = 0;
        int offset_minute = 0;
        if (timezone_offset + 6 != value.size() || value[timezone_offset + 3] != ':' ||
            !parse_digits(value, timezone_offset + 1, 2, offset_hour) ||
            !parse_digits(value, timezone_offset + 4, 2, offset_minute) || offset_hour > 23 || offset_minute > 59) {
            return std::nullopt;
        }
        timezone_seconds = offset_hour * 3600 + offset_minute * 60;
        if (timezone_marker == '-') {
            timezone_seconds = -timezone_seconds;
        }
    } else {
        return std::nullopt;
    }

    const std::int64_t days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const std::int64_t seconds_since_epoch = days * 86400 + hour * 3600 + minute * 60 + second - timezone_seconds;
    return WallTimePoint{std::chrono::seconds{seconds_since_epoch} + fractional};
}

} // namespace adsnooze
