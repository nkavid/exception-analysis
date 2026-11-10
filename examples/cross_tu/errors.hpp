#pragma once

#include <stdexcept>

namespace demo::errors
{
class ParseError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

class ConfigError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};
} // namespace demo::errors
