#pragma once

#include "exception_flow/extractor/test/translation_unit.hpp"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>

#include <gtest/gtest.h>

namespace exa::extractor::test
{
llvm::json::Value exceptionFlowSummaryOutput(const TranslationUnit& unit);

llvm::json::Value parse(llvm::StringRef text);

testing::AssertionResult hasFields(const llvm::json::Value& actual,
                                   const llvm::json::Value& expected);
} // namespace exa::extractor::test
