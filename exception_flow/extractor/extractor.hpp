#pragma once

#include "exception_flow/summary/summary.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/ScalableStaticAnalysis/Core/TUSummary/TUSummaryExtractor.h>

#include <map>

namespace exa::extractor
{
using EntityFacts = std::map<summary::EntityId, summary::ExceptionFlowSummary::Facts>;

EntityFacts collect(clang::ASTContext& context, clang::ssaf::TUSummaryExtractor& entities);

class ExceptionFlowExtractor final : public clang::ssaf::TUSummaryExtractor
{
  public:
    using clang::ssaf::TUSummaryExtractor::TUSummaryExtractor;

  private:
    void HandleTranslationUnit(clang::ASTContext& context) override;
};
} // namespace exa::extractor
