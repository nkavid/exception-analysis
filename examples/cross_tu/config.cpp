#include "examples/cross_tu/config.hpp"

#include "examples/cross_tu/errors.hpp"
#include "examples/cross_tu/parser.hpp"

namespace demo::config
{
Config load(std::string_view port)
{
  const int parsed = parser::parsePort(port);
  if (parsed > 65535)
  {
    throw errors::ConfigError("port out of range");
  }
  return Config{parsed};
}

void runHook(Hook hook)
{
  hook();
}

void runNothrowHook(NothrowHook hook) noexcept
{
  hook();
}
} // namespace demo::config
