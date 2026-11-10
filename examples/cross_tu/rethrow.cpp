#include "examples/cross_tu/rethrow.hpp"

namespace demo::rethrow
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexceptions"
void g() noexcept
{
  throw;
}
#pragma clang diagnostic pop

void h()
{
  g();
}

void f()
{
  try
  {
    throw A{};
  }
  catch (A&)
  {
    h();
  }
}
} // namespace demo::rethrow
