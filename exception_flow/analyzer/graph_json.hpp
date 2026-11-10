#pragma once

#include "exception_flow/analyzer/analysis.hpp"
#include "exception_flow/analyzer/border_crossing.hpp"

#include <llvm/Support/JSON.h>

#include <set>
#include <string>
#include <vector>

namespace exa::analyzer
{
llvm::json::Value toJSON(const ExceptionGraph& graph,
                         const std::vector<BorderCrossing>& crossings,
                         const std::set<std::string>& underAnalysisFiles);

bool hasUncaught(const ExceptionGraph& graph);
} // namespace exa::analyzer
