#include "exception_flow/summary/json_format.hpp"
#include "exception_flow/summary/summary.hpp"
#include "exception_flow/summary/test/test_data.hpp"

#include <clang/ScalableStaticAnalysis/Core/Serialization/JSONFormat.h>
#include <gtest/gtest.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Registry.h>

namespace exa::summary::test
{
namespace
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
llvm::Registry<clang::ssaf::JSONFormat::FormatInfo>::Add<ExceptionFlowJSONFormatInfo>
    registerJsonFormat(ExceptionFlowSummary::name, "JSON format of the ExaExceptionFlow summary");
#pragma clang diagnostic pop

TEST(ReadTUSummary, ValidFile)
{
  auto summary = clang::ssaf::JSONFormat().readTUSummary(test_data::path("tu_summary.json").string());

  EXPECT_TRUE(static_cast<bool>(summary)) << (summary ? "" : llvm::toString(summary.takeError()));
}
} // namespace
} // namespace exa::summary::test
