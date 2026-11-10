#include "exception_flow/summary/test/test_data.hpp"

#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/MemoryBuffer.h>

#include <filesystem>
#include <optional>

namespace exa::summary::test::test_data
{
FunctionDefinition throwingFunction()
{
  return FunctionDefinition{
      EntityDefinition{"throwing", SourceLocation{"throwing.cpp", 3, 1}, false},
      "throwing.cpp",
      FunctionBorder{ExceptionSpecification::potentiallyThrowing, std::nullopt, LanguageLinkage::cxx},
      false,
      {},
      {Throw{ExceptionType{"Error", false, std::nullopt}, {}, SourceLocation{"throwing.cpp", 5, 3}}},
      {},
      {Call{UnmappedCallee{"log",
                          FunctionBorder{ExceptionSpecification::nonThrowing,
                                         NounwindCause::exceptionSpecification,
                                         LanguageLinkage::cxx}},
            {},
            std::nullopt,
            SourceLocation{"throwing.cpp", 4, 3}}},
  };
}

std::filesystem::path path(const std::filesystem::path& fileName)
{
  return std::filesystem::path("exception_flow/summary/test/test_data") / fileName;
}

llvm::json::Value read_file(const std::filesystem::path& fileName)
{
  const auto path = test_data::path(fileName).string();
  auto buffer     = llvm::MemoryBuffer::getFile(path);
  if (!buffer)
  {
    llvm::reportFatalUsageError(llvm::Twine(path) + ": " + buffer.getError().message());
  }
  auto value = llvm::json::parse((*buffer)->getBuffer());
  if (!value)
  {
    llvm::reportFatalUsageError(llvm::Twine(path) + ": " + llvm::toString(value.takeError()));
  }
  return std::move(*value);
}
} // namespace exa::summary::test::test_data
