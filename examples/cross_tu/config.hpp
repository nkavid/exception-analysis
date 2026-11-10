#pragma once

#include <string_view>

namespace demo::config
{
struct Config
{
    int port = 0;
};

Config load(std::string_view port);

using Hook        = void (*)();
using NothrowHook = void (*)() noexcept;

void runHook(Hook hook);

void runNothrowHook(NothrowHook hook) noexcept;
} // namespace demo::config
