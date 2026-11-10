#include "exception_flow/analyzer/border_crossing.hpp"

#include <llvm/ADT/STLExtras.h>

#include <variant>

namespace exa::analyzer
{
using summary::FunctionBorder;

namespace
{
struct Side
{
    std::optional<CompilationUnit> compilationUnit;
    FunctionBorder border;
};

template <typename Value>
std::optional<Crossing<Value>> crossing(const std::optional<Value>& from,
                                        const std::optional<Value>& to)
{
  if (!from || !to || *from == *to)
  {
    return std::nullopt;
  }
  return Crossing<Value>{*from, *to};
}

std::optional<std::string> ccTarget(const Side& side)
{
  if (!side.compilationUnit)
  {
    return std::nullopt;
  }
  return side.compilationUnit->ccTarget;
}

std::optional<std::string> translationUnit(const Side& side)
{
  if (!side.compilationUnit)
  {
    return std::nullopt;
  }
  return side.compilationUnit->translationUnit;
}

std::optional<bool> isNounwind(const Side& side)
{
  return side.border.nounwindCause.has_value();
}

BorderCrossing borderCrossing(const Side& from, const Side& to)
{
  return {crossing(translationUnit(from), translationUnit(to)),
          crossing(ccTarget(from), ccTarget(to)),
          crossing(from.border.languageLinkage, to.border.languageLinkage),
          crossing(from.border.exceptionSpecification, to.border.exceptionSpecification),
          crossing(isNounwind(from), isNounwind(to))};
}

std::vector<std::optional<NodeId>> owningFunctions(const ExceptionGraph& graph)
{
  std::vector<std::optional<NodeId>> owners(graph.nodes.size());
  for (NodeId node = 0; node < graph.nodes.size(); ++node)
  {
    const bool isFunction = std::holds_alternative<FunctionNode>(graph.nodes.at(node))
                         || std::holds_alternative<UnknownFunctionNode>(graph.nodes.at(node));
    if (isFunction)
    {
      owners.at(node) = node;
    }
  }
  for (const auto& edge : graph.edges)
  {
    std::visit(llvm::makeVisitor([&](const ThrowEdge&) { owners.at(edge.from) = edge.to; },
                          [&](const CatchEdge&) { owners.at(edge.to) = edge.from; },
                          [&](const UncaughtEdge&) { owners.at(edge.to) = edge.from; },
                          [](const RethrowEdge&) {},
                          [](const CallEdge&) {}),
               edge.kind);
  }
  return owners;
}

class SideResolver
{
  public:
    SideResolver(const ExceptionGraph& graph [[clang::lifetimebound]],
                 const std::map<std::string, CompilationUnit>& compilationUnits
                 [[clang::lifetimebound]])
        : _graph(graph),
          _compilationUnits(compilationUnits),
          _owners(owningFunctions(graph))
    {}

    llvm::Expected<Side> side(NodeId node) const
    {
      const auto owner = _owners.at(node);
      if (!owner)
      {
        return llvm::createStringError("node %zu has no owning function", node);
      }
      return std::visit(
          llvm::makeVisitor([&](const FunctionNode& function) -> llvm::Expected<Side> {
                       return functionSide(function);
                     },
                     [](const UnknownFunctionNode& unknown) -> llvm::Expected<Side> {
                       return Side{std::nullopt, unknown.border};
                     },
                     [&](const auto&) -> llvm::Expected<Side> {
                       return llvm::createStringError(
                           "owner %zu of node %zu is not a function", *owner, node);
                     }),
          _graph.nodes.at(*owner));
    }

  private:
    llvm::Expected<Side> functionSide(const FunctionNode& function) const
    {
      if (!function.compilationUnit)
      {
        return Side{std::nullopt, function.border};
      }
      const auto known = _compilationUnits.find(*function.compilationUnit);
      if (known == _compilationUnits.end())
      {
        return llvm::createStringError("compilation unit '%s' of '%s' is not in the manifest",
                                       function.compilationUnit->c_str(),
                                       function.name.c_str());
      }
      return Side{known->second, function.border};
    }

    const ExceptionGraph& _graph;
    const std::map<std::string, CompilationUnit>& _compilationUnits;
    std::vector<std::optional<NodeId>> _owners;
};
} // namespace

llvm::Expected<std::vector<BorderCrossing>>
borderCrossings(const ExceptionGraph& graph,
                const std::map<std::string, CompilationUnit>& compilationUnits)
{
  const SideResolver resolver(graph, compilationUnits);
  std::vector<BorderCrossing> crossings;
  crossings.reserve(graph.edges.size());
  for (const auto& edge : graph.edges)
  {
    auto from = resolver.side(edge.from);
    if (!from)
    {
      return from.takeError();
    }
    auto to = resolver.side(edge.to);
    if (!to)
    {
      return to.takeError();
    }
    crossings.push_back(borderCrossing(*from, *to));
  }
  return crossings;
}
} // namespace exa::analyzer
