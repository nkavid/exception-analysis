#include "exception_flow/summary/json_format.hpp"
#include "exception_flow/summary/summary.hpp"
#include "exception_flow/summary/test/stream_operator.hpp"
#include "exception_flow/summary/test/test_data.hpp"

#include <clang/ScalableStaticAnalysis/Core/Model/BuildNamespace.h>
#include <clang/ScalableStaticAnalysis/Core/Model/EntityIdTable.h>
#include <clang/ScalableStaticAnalysis/Core/Model/EntityName.h>
#include <gtest/gtest.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>

#include <variant>

namespace exa::summary::test
{
namespace
{
class Deserialize : public testing::Test
{
  protected:
    llvm::Expected<EntityId> dummyEntityIdFromJSON(const llvm::json::Object&)
    {
      return dummyEntityIdTable.getId(clang::ssaf::EntityName("dummy", "", clang::ssaf::NestedBuildNamespace()));
    }

    clang::ssaf::EntityIdTable dummyEntityIdTable;
};

TEST_F(Deserialize, ThrowingFunction)
{
  const auto json = test_data::read_file("throwing_function.json");

  auto summary = deserialize(*json.getAsObject(),
                             dummyEntityIdTable,
                             [this](const llvm::json::Object& object) { return dummyEntityIdFromJSON(object); });
  if (!summary)
  {
    FAIL() << llvm::toString(summary.takeError());
  }

  const auto& facts = static_cast<const ExceptionFlowSummary&>(**summary).facts();
  ASSERT_TRUE(std::holds_alternative<FunctionDefinition>(facts));
  EXPECT_EQ(std::get<FunctionDefinition>(facts), test_data::throwingFunction());
}
} // namespace
} // namespace exa::summary::test
