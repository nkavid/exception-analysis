#pragma once

#include "exception_flow/analyzer/analysis.hpp"

#include <llvm/Support/Error.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace exa::analyzer
{
struct CompilationUnit
{
    std::string ccTarget;
    std::string translationUnit;
};

template <typename Value>
struct Crossing
{
    Value from;
    Value to;
};

struct BorderCrossing
{
    std::optional<Crossing<std::string>> translationUnit;
    std::optional<Crossing<std::string>> ccTarget;
    std::optional<Crossing<summary::LanguageLinkage>> languageLinkage;
    std::optional<Crossing<summary::ExceptionSpecification>> exceptionSpecification;
    std::optional<Crossing<bool>> nounwind;
};

llvm::Expected<std::vector<BorderCrossing>>
borderCrossings(const ExceptionGraph& graph,
                const std::map<std::string, CompilationUnit>& compilationUnits);
} // namespace exa::analyzer
