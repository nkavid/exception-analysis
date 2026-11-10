#include "exception_flow/analyzer/graph_json.hpp"

#include "exception_flow/summary/enum_string.hpp"

#include <llvm/Support/ErrorHandling.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace exa::analyzer
{
namespace
{
namespace json = llvm::json;

using summary::FunctionBorder;
using summary::LanguageLinkage;
using summary::languageLinkageStrings;
using summary::ExceptionSpecification;
using summary::exceptionSpecificationStrings;
using summary::NounwindCause;
using summary::nounwindCauseStrings;
using summary::toString;
using summary::SourceLocation;

int64_t id(size_t index)
{
  return static_cast<int64_t>(index);
}

llvm::StringRef terminateReasonName(TerminateReason reason)
{
  switch (reason)
  {
  case TerminateReason::noHandler:
    return "no_handler";
  case TerminateReason::noexceptFunction:
    return "noexcept";
  case TerminateReason::noCurrentlyHandledException:
    return "no_currently_handled_exception";
  }
  llvm_unreachable("unknown TerminateReason");
}

llvm::StringRef undefinedBehaviorReasonName(UndefinedBehaviorReason reason)
{
  switch (reason)
  {
  case UndefinedBehaviorReason::nounwind:
    return "nounwind";
  }
  llvm_unreachable("unknown UndefinedBehaviorReason");
}

void putSourceLocation(json::Object& object, const std::optional<SourceLocation>& sourceLocation)
{
  if (sourceLocation)
  {
    object["source_location"] = json::Object{
        {"file", sourceLocation->file},
        {"line", sourceLocation->line},
        { "column",  sourceLocation->column}
    };
  }
}

void putBorder(json::Object& object, const FunctionBorder& border)
{
  if (const auto specification = border.exceptionSpecification)
  {
    object["exception_specification"] = toString(exceptionSpecificationStrings, *specification);
  }
  if (const auto cause = border.nounwindCause)
  {
    object["nounwind_cause"] = toString(nounwindCauseStrings, *cause);
  }
  if (const auto linkage = border.languageLinkage)
  {
    object["language_linkage"] = toString(languageLinkageStrings, *linkage);
  }
}

void describe(json::Object& object, const FunctionNode& function)
{
  object["kind"]                    = "function";
  object["name"]                    = function.name;
  object["is_defined"]              = function.isDefined;
  object["is_entry_point"]          = function.isEntryPoint;
  object["is_reachable_from_main"]  = function.isReachableFromMain;
  object["in_system_header"]        = function.inSystemHeader;
  putBorder(object, function.border);
  putSourceLocation(object, function.sourceLocation);
}

void describe(json::Object& object, const ExceptionNode& exception)
{
  object["kind"] = "exception";
  if (exception.thrownType)
  {
    object["thrown_type"] = exception.thrownType->name;
  }
}

void describe(json::Object& object, const CatchNode& catchNode)
{
  object["kind"] = "catch";
  if (catchNode.caughtType)
  {
    object["caught_type"] = catchNode.caughtType->name;
  }
}

void describe(json::Object& object, const TerminateNode& terminate)
{
  object["kind"]   = "terminate";
  object["reason"] = terminateReasonName(terminate.reason);
}

void describe(json::Object& object, const UndefinedBehaviorNode& undefinedBehavior)
{
  object["kind"]   = "undefined_behavior";
  object["reason"] = undefinedBehaviorReasonName(undefinedBehavior.reason);
}

void describe(json::Object& object, const UnknownFunctionNode& unknown)
{
  object["kind"]          = "unknown_function";
  object["function_type"] = unknown.functionType;
  putBorder(object, unknown.border);
}

void describe(json::Object& object, const ThrowEdge& edge)
{
  object["kind"] = "throw";
  putSourceLocation(object, edge.sourceLocation);
}

void describe(json::Object& object, const RethrowEdge& edge)
{
  object["kind"] = "rethrow";
  putSourceLocation(object, edge.sourceLocation);
}

void describe(json::Object& object, const CatchEdge& edge)
{
  object["kind"] = "catch";
  putSourceLocation(object, edge.sourceLocation);
}

void describe(json::Object& object, const CallEdge& edge)
{
  object["kind"] = "call";
  putSourceLocation(object, edge.sourceLocation);
}

void describe(json::Object& object, const UncaughtEdge&)
{
  object["kind"] = "uncaught";
}

template <typename Value, typename Name>
void putCrossing(json::Object& object,
                 llvm::StringRef key,
                 const std::optional<Crossing<Value>>& crossing,
                 Name name)
{
  if (crossing)
  {
    object[key] = json::Object{
        {"from", name(crossing->from)},
        {  "to",   name(crossing->to)}
    };
  }
}

json::Object crossingObject(const BorderCrossing& crossing)
{
  const auto same = [](const auto& value) { return json::Value(value); };
  json::Object object;
  putCrossing(object, "translation_unit", crossing.translationUnit, same);
  putCrossing(object, "cc_target", crossing.ccTarget, same);
  putCrossing(object, "language_linkage", crossing.languageLinkage, [](LanguageLinkage linkage) {
    return toString(languageLinkageStrings, linkage);
  });
  putCrossing(object,
              "exception_specification",
              crossing.exceptionSpecification,
              [](ExceptionSpecification specification) {
                return toString(exceptionSpecificationStrings, specification);
              });
  putCrossing(object, "nounwind", crossing.nounwind, same);
  return object;
}
} // namespace

namespace
{
bool isUnderAnalysis(const FunctionNode& function,
                     const std::set<std::string>& underAnalysisFiles)
{
  return function.sourceLocation
      && underAnalysisFiles.count(function.sourceLocation->file) != 0;
}
} // namespace

json::Value toJSON(const ExceptionGraph& graph,
                   const std::vector<BorderCrossing>& crossings,
                   const std::set<std::string>& underAnalysisFiles)
{
  json::Array nodes;
  for (size_t index = 0; index < graph.nodes.size(); ++index)
  {
    json::Object object{
        {"id", id(index)}
    };
    std::visit([&](const auto& node) { describe(object, node); },
               graph.nodes.at(index));
    if (const auto* function = std::get_if<FunctionNode>(&graph.nodes.at(index)))
    {
      object["is_under_analysis"] = isUnderAnalysis(*function, underAnalysisFiles);
    }
    nodes.push_back(std::move(object));
  }

  json::Array edges;
  for (size_t index = 0; index < graph.edges.size(); ++index)
  {
    const Edge& edge = graph.edges.at(index);
    json::Object object{
        {  "id",     id(index)},
        {"from", id(edge.from)},
        {  "to",   id(edge.to)}
    };
    std::visit([&](const auto& kind) { describe(object, kind); }, edge.kind);
    object["crossing"] = crossingObject(crossings.at(index));
    edges.push_back(std::move(object));
  }

  json::Array exceptions;
  for (size_t index = 0; index < graph.exceptions.size(); ++index)
  {
    const Exception& exception = graph.exceptions.at(index);
    json::Array unwinds;
    for (const Step& step : exception.unwinds)
    {
      unwinds.push_back(json::Object{
          {"from", id(step.from)},
          {  "to",   id(step.to)}
      });
    }
    exceptions.push_back(json::Object{
        {     "id",               id(index)},
        { "source",    id(exception.source)},
        {  "throw", id(exception.throwEdge)},
        {"unwinds",      std::move(unwinds)}
    });
  }

  return json::Object{
      {     "nodes",      std::move(nodes)},
      {     "edges",      std::move(edges)},
      {"exceptions", std::move(exceptions)}
  };
}

bool hasUncaught(const ExceptionGraph& graph)
{
  for (const auto& edge : graph.edges)
  {
    if (std::holds_alternative<UncaughtEdge>(edge.kind))
    {
      return true;
    }
  }
  return false;
}
} // namespace exa::analyzer
