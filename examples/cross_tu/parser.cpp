#include "examples/cross_tu/parser.hpp"

#include "examples/cross_tu/errors.hpp"

#include <string>

namespace demo::parser
{
int parsePort(std::string_view text)
{
  if (text.empty())
  {
    throw errors::ParseError("empty port");
  }
  int port = 0;
  for (const char digit : text)
  {
    if (digit < '0' || digit > '9')
    {
      throw errors::ParseError("not a number: " + std::string(text));
    }
    port = port * 10 + (digit - '0');
  }
  return port;
}

int parsePortOrDefault(std::string_view text, int fallback) noexcept
{
  return text.empty() ? fallback : parsePort(text);
}
} // namespace demo::parser
