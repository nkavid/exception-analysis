#include "exception_flow/summary/json_format.hpp"
#include "exception_flow/summary/summary.hpp"
#include "exception_flow/summary/test/test_data.hpp"

#include <gtest/gtest.h>
#include <llvm/Support/JSON.h>

namespace exa::summary::test
{
namespace
{
namespace dummy
{
llvm::json::Object entityIdToJSON(EntityId)
{
  return llvm::json::Object{{"@", 0}};
}
} // namespace dummy

TEST(Serialize, ThrowingFunction)
{
  const llvm::json::Value actual = serialize(ExceptionFlowSummary(test_data::throwingFunction()), dummy::entityIdToJSON);

  EXPECT_EQ(actual, test_data::read_file("throwing_function.json"));
}
} // namespace
} // namespace exa::summary::test
