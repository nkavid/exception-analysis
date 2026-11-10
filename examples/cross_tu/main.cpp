#include "examples/cross_tu/config.hpp"
#include "examples/cross_tu/errors.hpp"
#include "examples/cross_tu/parser.hpp"
#include "examples/cross_tu/rethrow.hpp"

#include <cstdio>

namespace
{
void quietHook() noexcept {}

void loudHook() {}

int serve(std::string_view port)
{
  try
  {
    return demo::config::load(port).port;
  }
  catch (const demo::errors::ParseError& error)
  {
    std::puts(error.what());
  }
  return -1;
}

void runPlugins()
{
  try
  {
    demo::config::runHook(&loudHook);
  }
  catch (...)
  {
    std::puts("plugin failed");
  }
  demo::config::runNothrowHook(&quietHook);
}
} // namespace

int main(int argc, char** argv)
{
  runPlugins();
  demo::rethrow::f();
  if (argc > 2)
  {
    demo::rethrow::h();
  }
  const int port = serve(argc > 1 ? argv[1] : "8080");
  return demo::parser::parsePortOrDefault("", port) > 0 ? 0 : 1;
}
