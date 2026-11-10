#pragma once

#include "exception_flow/summary/summary.hpp"

#include <clang/ScalableStaticAnalysis/Analyses/PointerFlow/PointerFlowAnalysis.h>
#include <clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/AnalysisName.h>
#include <clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/AnalysisResult.h>
#include <clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/DerivedAnalysis.h>
#include <clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/SummaryAnalysis.h>
#include <llvm/Support/Error.h>

#include <compare>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace exa::analyzer
{
struct ExceptionFlowFacts final : clang::ssaf::AnalysisResult
{
    static clang::ssaf::AnalysisName analysisName()
    {
      return clang::ssaf::AnalysisName("ExaExceptionFlowFacts");
    }

    ~ExceptionFlowFacts() override;

    std::map<summary::EntityId, summary::FunctionDefinition> functions;
    std::map<summary::EntityId, summary::ClassDefinition> classes;
};

using NodeId = size_t;

struct FunctionNode
{
    std::string name;
    bool isDefined = false;
    summary::FunctionBorder border;
    bool isEntryPoint        = false;
    bool isReachableFromMain = false;
    bool inSystemHeader      = false;
    std::optional<summary::SourceLocation> sourceLocation;
    std::optional<std::string> compilationUnit;
};

struct ExceptionNode
{
    std::optional<summary::ExceptionType> thrownType;
};

struct CatchNode
{
    std::optional<summary::ExceptionType> caughtType;
};

enum class TerminateReason
{
  noHandler,
  noexceptFunction,
  noCurrentlyHandledException,
};

struct TerminateNode
{
    TerminateReason reason;
};

enum class UndefinedBehaviorReason
{
  nounwind,
};

struct UndefinedBehaviorNode
{
    UndefinedBehaviorReason reason;
};

struct UnknownFunctionNode
{
    std::string functionType;
    summary::FunctionBorder border;
};

using Node = std::variant<FunctionNode,
                          ExceptionNode,
                          CatchNode,
                          TerminateNode,
                          UndefinedBehaviorNode,
                          UnknownFunctionNode>;

using EdgeId = size_t;

struct ThrowEdge
{
    std::optional<summary::SourceLocation> sourceLocation;
};

struct RethrowEdge
{
    std::optional<summary::SourceLocation> sourceLocation;
};

struct CatchEdge
{
    std::optional<summary::SourceLocation> sourceLocation;
};

struct CallEdge
{
    std::optional<summary::SourceLocation> sourceLocation;
};

struct UncaughtEdge
{
};

using EdgeKind = std::variant<ThrowEdge, RethrowEdge, CatchEdge, CallEdge, UncaughtEdge>;

struct Edge
{
    NodeId from;
    NodeId to;
    EdgeKind kind;
};

struct Step
{
    EdgeId from;
    EdgeId to;

    auto operator<=>(const Step&) const = default;
};

struct Exception
{
    NodeId source;
    EdgeId throwEdge;
    std::vector<Step> unwinds;
};

struct ExceptionGraph final : clang::ssaf::AnalysisResult
{
    static clang::ssaf::AnalysisName analysisName()
    {
      return clang::ssaf::AnalysisName("ExaExceptionFlowGraph");
    }

    ~ExceptionGraph() override;

    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Exception> exceptions;
};

class ExceptionFlowFactsAnalysis final
    : public clang::ssaf::SummaryAnalysis<ExceptionFlowFacts, summary::ExceptionFlowSummary>
{
  public:
    llvm::Error add(summary::EntityId id, const summary::ExceptionFlowSummary& summary) override;
};

class ExceptionGraphAnalysis final
    : public clang::ssaf::DerivedAnalysis<ExceptionGraph,
                                          ExceptionFlowFacts,
                                          clang::ssaf::PointerFlowAnalysisResult>
{
  public:
    llvm::Error initialize(const ExceptionFlowFacts& facts,
                           const clang::ssaf::PointerFlowAnalysisResult& pointerFlow) override;

    llvm::Expected<bool> step() override;
};
} // namespace exa::analyzer
