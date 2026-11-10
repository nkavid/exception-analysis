#pragma once

#include <string_view>

namespace demo::parser
{
int parsePort(std::string_view text);

int parsePortOrDefault(std::string_view text, int fallback) noexcept;
} // namespace demo::parser
