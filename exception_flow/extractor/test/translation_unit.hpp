#pragma once

#include "exception_flow/extractor/extractor.hpp"
#include "exception_flow/summary/summary.hpp"

#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/SSAFOptions.h>
#include <clang/ScalableStaticAnalysis/Core/TUSummary/TUSummary.h>
#include <clang/ScalableStaticAnalysis/Core/TUSummary/TUSummaryBuilder.h>
#include <clang/ScalableStaticAnalysis/Core/TUSummary/TUSummaryExtractor.h>
#include <llvm/ADT/StringRef.h>

#include <map>
#include <memory>
#include <string>

namespace exa::extractor::test
{
inline constexpr llvm::StringLiteral mainFile = "main.cpp";

class TranslationUnit
{
  public:
    explicit TranslationUnit(llvm::StringRef code);

    const EntityFacts& facts() const [[clang::lifetimebound]];

    summary::EntityId entity(llvm::StringRef name);

    const std::string& name(summary::EntityId id) const [[clang::lifetimebound]];

  private:
    clang::ssaf::SSAFOptions _options;
    clang::ssaf::TUSummary _summary;
    clang::ssaf::TUSummaryBuilder _builder;
    clang::ssaf::TUSummaryExtractor _entities;
    std::unique_ptr<clang::ASTUnit> _ast;
    EntityFacts _facts;
    std::map<summary::EntityId, std::string> _names;
};
} // namespace exa::extractor::test
