#include "exception_flow/extractor/test/summary_json.hpp"
#include "exception_flow/extractor/test/translation_unit.hpp"

#include <gtest/gtest.h>

namespace exa::extractor::test
{
namespace
{
TEST(ExtractorJson, ThrowReachedByDirectCall)
{
  const TranslationUnit unit(R"cpp(
struct Error
{
};

void fail()
{
  throw Error{};
}

void run()
{
  fail();
}
)cpp");

  const auto actual = exceptionFlowSummaryOutput(unit);

  const auto expected = parse(R"json({
  "fail": {"FunctionDefinition": {
    "throws": [{"thrownType": {"name": "Error", "record": {"@": "Error"}}}],
    "calls": []
  }},
  "run": {"FunctionDefinition": {
    "throws": [],
    "calls": [{"callee": {"DirectCallee": {"function": {"@": "fail"}}},
               "sourceLocation": {"line": 13}}]
  }}
})json");

  EXPECT_TRUE(hasFields(actual, expected));
}
} // namespace
} // namespace exa::extractor::test
