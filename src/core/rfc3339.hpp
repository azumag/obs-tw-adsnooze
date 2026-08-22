#pragma once

#include "core/time.hpp"

#include <optional>
#include <string_view>

namespace adsnooze {

[[nodiscard]] std::optional<WallTimePoint> parse_rfc3339(std::string_view value) noexcept;

} // namespace adsnooze
