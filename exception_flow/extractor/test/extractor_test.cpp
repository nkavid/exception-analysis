#include "exception_flow/extractor/test/translation_unit.hpp"
#include "exception_flow/summary/summary.hpp"
#include "exception_flow/summary/test/stream_operator.hpp"

#include <llvm/ADT/StringRef.h>

#include <gtest/gtest.h>

namespace exa::extractor::test
{
namespace
{
using summary::Call;
using summary::DirectCallee;
using summary::ExceptionSpecification;
using summary::ExceptionType;
using summary::FunctionBorder;
using summary::FunctionDefinition;
using summary::LanguageLinkage;
using summary::SourceLocation;
using summary::Throw;

inline constexpr FunctionBorder potentiallyThrowing{ExceptionSpecification::potentiallyThrowing,
                                                    std::nullopt,
                                                    LanguageLinkage::cxx};

TEST(Extractor, ThrowReachedByDirectCall)
{
  TranslationUnit unit(R"cpp(
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

  const auto fail = unit.entity("fail");

  FunctionDefinition failDefinition{{"fail", SourceLocation{mainFile.str(), 6, 6}, false}};
  failDefinition.compilationUnit = mainFile.str();
  failDefinition.border          = potentiallyThrowing;
  failDefinition.throws          = {
      Throw{ExceptionType{"Error", false, unit.entity("Error")},
            {},
            SourceLocation{mainFile.str(), 8, 3}}
  };

  FunctionDefinition runDefinition{{"run", SourceLocation{mainFile.str(), 11, 6}, false}};
  runDefinition.compilationUnit = mainFile.str();
  runDefinition.border          = potentiallyThrowing;
  runDefinition.calls           = {
      Call{DirectCallee{fail, "fail", potentiallyThrowing},
           {},
           std::nullopt,
           SourceLocation{mainFile.str(), 13, 3}}
  };

  const EntityFacts expected{
      {fail,               failDefinition},
      {unit.entity("run"), runDefinition },
  };
  EXPECT_EQ(unit.facts(), expected);
}
} // namespace
} // namespace exa::extractor::test
