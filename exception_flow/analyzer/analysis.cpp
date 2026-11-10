#include "exception_flow/analyzer/analysis.hpp"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>

#include <deque>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>

namespace exa::analyzer
{
using summary::Call;
using summary::CatchIndex;
using summary::ClassDefinition;
using summary::DirectCallee;
using summary::EntityId;
using summary::ExceptionFlowSummary;
using summary::ExceptionType;
using summary::FunctionBorder;
using summary::FunctionDefinition;
using summary::IndirectCallee;
using summary::NounwindCause;
using summary::SourceLocation;
using summary::UnmappedCallee;
using summary::VirtualCallee;
using summary::VirtualMethod;

ExceptionFlowFacts::~ExceptionFlowFacts() = default;

ExceptionGraph::~ExceptionGraph() = default;

namespace
{
inline constexpr llvm::StringLiteral voidPointerName = "void *";

class GraphBuilder
{
  public:
    GraphBuilder(const ExceptionFlowFacts& facts [[clang::lifetimebound]],
                 const clang::ssaf::PointerFlowAnalysisResult& pointerFlow,
                 ExceptionGraph& graph [[clang::lifetimebound]])
        : _facts(facts),
          _graph(graph)
    {
      indexClasses();
      for (const auto& [entity, edges] : pointerFlow.Edges)
      {
        for (const auto& [assignee, assigned] : edges)
        {
          _assignedFrom[assignee].insert(assigned.begin(), assigned.end());
        }
      }
    }

    llvm::Error build()
    {
      for (const auto& [id, definition] : _facts.functions)
      {
        _functionNodes.emplace(id,
                               add(FunctionNode{definition.name,
                                                true,
                                                definition.border,
                                                definition.isEntryPoint,
                                                false,
                                                definition.inSystemHeader,
                                                definition.sourceLocation,
                                                definition.compilationUnit}));
      }
      for (const auto& [id, definition] : _facts.functions)
      {
        if (auto error = addBody(_functionNodes.at(id), definition))
        {
          return error;
        }
      }
      markReachableFromMain();
      addRethrowsWithoutHandledException();
      for (const auto& raised : _raised)
      {
        _graph.exceptions.push_back(unwind(raised));
      }
      return llvm::Error::success();
    }

  private:
    struct Raise
    {
        NodeId function;
        std::vector<NodeId> handlers;
    };

    struct HandlerScope
    {
        std::vector<NodeId> handlers;
        std::optional<NodeId> enclosingHandler;
    };

    struct RethrowSite
    {
        NodeId function;
        std::vector<NodeId> handlers;
        std::optional<SourceLocation> sourceLocation;
    };

    struct Raised
    {
        NodeId exception;
        EdgeId throwEdge;
    };

    struct Unwinding
    {
        Exception exception;
        std::set<EdgeId> unwound;
        std::set<Step> steps;
        std::map<NodeId, std::vector<EdgeId>> leaving;
        std::deque<EdgeId> pending;
    };

    NodeId add(Node node)
    {
      _graph.nodes.push_back(std::move(node));
      return _graph.nodes.size() - 1;
    }

    EdgeId addEdge(NodeId from, NodeId to, EdgeKind kind)
    {
      _graph.edges.push_back({from, to, std::move(kind)});
      return _graph.edges.size() - 1;
    }

    void indexClasses()
    {
      for (const auto& [id, definition] : _facts.classes)
      {
        for (const auto& method : definition.virtualMethods)
        {
          _virtualMethods.emplace(method.method, &method);
          for (const auto& overridden : method.overriddenMethods)
          {
            _directOverriders[overridden].insert(method.method);
          }
        }
      }
    }

    void throwException(std::optional<ExceptionType> thrownType,
                        NodeId into,
                        NodeId function,
                        std::vector<NodeId> handlers,
                        std::optional<SourceLocation> sourceLocation)
    {
      const auto exception = add(ExceptionNode{std::move(thrownType)});
      const auto edge      = addEdge(exception, into, ThrowEdge{std::move(sourceLocation)});
      _raises.emplace(edge, Raise{function, std::move(handlers)});
      _raised.push_back({exception, edge});
    }

    void throwUnknownException(NodeId into)
    {
      throwException(std::nullopt, into, into, {}, std::nullopt);
    }

    NodeId functionNode(EntityId id,
                        const std::string& name,
                        const FunctionBorder& border,
                        bool hasHiddenBody)
    {
      if (const auto known = _functionNodes.find(id); known != _functionNodes.end())
      {
        return known->second;
      }
      const auto node =
          add(FunctionNode{name, false, border, false, false, false, std::nullopt, std::nullopt});
      _functionNodes.emplace(id, node);
      if (hasHiddenBody && !border.nounwindCause)
      {
        throwUnknownException(node);
      }
      return node;
    }

    NodeId unmappedFunctionNode(const std::string& name, const FunctionBorder& border)
    {
      if (const auto known = _unmappedFunctionNodes.find(name);
          known != _unmappedFunctionNodes.end())
      {
        return known->second;
      }
      const auto node =
          add(FunctionNode{name, false, border, false, false, false, std::nullopt, std::nullopt});
      _unmappedFunctionNodes.emplace(name, node);
      if (!border.nounwindCause)
      {
        throwUnknownException(node);
      }
      return node;
    }

    bool isUnimplemented(EntityId method) const
    {
      const auto known = _virtualMethods.find(method);
      return known != _virtualMethods.end() && known->second->isPureVirtual
          && _facts.functions.count(method) == 0;
    }

    std::vector<EntityId> overridersIncluding(EntityId method) const
    {
      std::vector<EntityId> result;
      std::set<EntityId> seen{method};
      std::deque<EntityId> pending{method};
      while (!pending.empty())
      {
        const auto current = pending.front();
        pending.pop_front();
        result.push_back(current);
        const auto overriders = _directOverriders.find(current);
        if (overriders == _directOverriders.end())
        {
          continue;
        }
        for (const auto& overrider : overriders->second)
        {
          if (seen.insert(overrider).second)
          {
            pending.push_back(overrider);
          }
        }
      }
      return result;
    }

    NodeId overriderNode(EntityId method)
    {
      const auto* known = _virtualMethods.at(method);
      return functionNode(method, known->name, known->border, true);
    }

    void addCallEdge(NodeId callee, NodeId caller, const Call& call, const HandlerScope& scope)
    {
      const auto edge = addEdge(callee, caller, CallEdge{call.sourceLocation});
      _raises.emplace(edge, Raise{caller, scope.handlers});
      _callEdges[callee].push_back(edge);
      _callEdgesByCaller[caller].push_back(edge);
      if (scope.enclosingHandler)
      {
        _callEdgesInHandler[*scope.enclosingHandler].push_back(edge);
        return;
      }
      _callEdgesOutsideHandlers[caller].push_back(edge);
    }

    void addCall(NodeId function, const Call& call, const HandlerScope& scope)
    {
      std::visit(llvm::makeVisitor([&](const DirectCallee& callee) {
                              addCallEdge(functionNode(callee.function,
                                                       callee.name,
                                                       callee.border,
                                                       true),
                                          function,
                                          call,
                                          scope);
                            },
                            [&](const VirtualCallee& callee) {
                              const auto method =
                                  functionNode(callee.method,
                                               callee.name,
                                               callee.border,
                                               !isUnimplemented(callee.method));
                              for (const auto& overrider :
                                   overridersIncluding(callee.method))
                              {
                                if (!isUnimplemented(overrider))
                                {
                                  addCallEdge(overrider == callee.method
                                                  ? method
                                                  : overriderNode(overrider),
                                              function,
                                              call,
                                              scope);
                                }
                              }
                            },
                            [&](const IndirectCallee& callee) {
                              const auto pointed = pointedFunctions(callee.calleePointers);
                              for (const auto pointedFunction : pointed.functions)
                              {
                                addCallEdge(_functionNodes.at(pointedFunction),
                                            function,
                                            call,
                                            scope);
                              }
                              if (pointed.isComplete)
                              {
                                return;
                              }
                              const auto unknown =
                                  add(UnknownFunctionNode{callee.functionType, callee.border});
                              if (!callee.border.nounwindCause)
                              {
                                throwUnknownException(unknown);
                              }
                              addCallEdge(unknown, function, call, scope);
                            },
                            [&](const UnmappedCallee& callee) {
                              addCallEdge(unmappedFunctionNode(callee.name, callee.border),
                                          function,
                                          call,
                                          scope);
                            }),
                 call.callee);
    }

    template <typename Value>
    static llvm::ArrayRef<Value>
    lookup(const std::map<NodeId, std::vector<Value>>& index [[clang::lifetimebound]], NodeId key)
    {
      const auto known = index.find(key);
      if (known == index.end())
      {
        return {};
      }
      return known->second;
    }

    const std::vector<std::size_t>& rethrowSitesOf(NodeId handler)
    {
      const auto [known, inserted] = _rethrowSitesOfHandler.try_emplace(handler);
      if (!inserted)
      {
        return known->second;
      }
      auto& sites = known->second;
      std::set<NodeId> handling;
      std::deque<NodeId> pending;
      const auto enter = [&](llvm::ArrayRef<EdgeId> calls) {
        for (const auto edge : calls)
        {
          const auto callee = _graph.edges.at(edge).from;
          if (handling.insert(callee).second)
          {
            pending.push_back(callee);
          }
        }
      };
      const auto inHandler = lookup(_rethrowSitesInHandler, handler);
      sites.insert(sites.end(), inHandler.begin(), inHandler.end());
      enter(lookup(_callEdgesInHandler, handler));
      while (!pending.empty())
      {
        const auto function = pending.front();
        pending.pop_front();
        const auto outsideHandlers = lookup(_rethrowSitesOutsideHandlers, function);
        sites.insert(sites.end(), outsideHandlers.begin(), outsideHandlers.end());
        enter(lookup(_callEdgesOutsideHandlers, function));
      }
      return sites;
    }

    EdgeId rethrowEdge(NodeId exception, std::size_t site)
    {
      const auto [known, inserted] = _rethrowEdges.try_emplace({exception, site}, 0);
      if (inserted)
      {
        const auto& rethrow = _rethrowSites.at(site);
        known->second =
            addEdge(exception, rethrow.function, RethrowEdge{rethrow.sourceLocation});
        _raises.emplace(known->second, Raise{rethrow.function, rethrow.handlers});
      }
      return known->second;
    }

    llvm::Error addBody(NodeId function, const FunctionDefinition& definition)
    {
      std::vector<NodeId> catches;
      for (const auto& catchSite : definition.catches)
      {
        const auto node = add(CatchNode{catchSite.caughtType});
        _catchEdges.emplace(node,
                            addEdge(function, node, CatchEdge{catchSite.sourceLocation}));
        catches.push_back(node);
      }
      const auto handlerNodes = [&](const std::vector<CatchIndex>& handlers) {
        std::vector<NodeId> nodes;
        for (const auto index : handlers)
        {
          nodes.push_back(catches.at(index));
        }
        return nodes;
      };
      const auto handlerScope = [&](const std::vector<CatchIndex>& handlers,
                                    const std::optional<CatchIndex>& enclosingHandler) {
        HandlerScope scope{handlerNodes(handlers), std::nullopt};
        if (enclosingHandler)
        {
          scope.enclosingHandler = catches.at(*enclosingHandler);
        }
        return scope;
      };
      for (const auto& throwSite : definition.throws)
      {
        throwException(throwSite.thrownType,
                       function,
                       function,
                       handlerNodes(throwSite.handlers),
                       throwSite.sourceLocation);
      }
      for (const auto& rethrow : definition.rethrows)
      {
        const auto site  = _rethrowSites.size();
        const auto scope = handlerScope(rethrow.handlers, rethrow.enclosingHandler);
        _rethrowSites.push_back({function, scope.handlers, rethrow.sourceLocation});
        if (scope.enclosingHandler)
        {
          _rethrowSitesInHandler[*scope.enclosingHandler].push_back(site);
          continue;
        }
        _rethrowSitesOutsideHandlers[function].push_back(site);
      }
      for (const auto& call : definition.calls)
      {
        addCall(function, call, handlerScope(call.handlers, call.enclosingHandler));
      }
      return llvm::Error::success();
    }

    bool isDerivedFrom(EntityId derived, EntityId base) const
    {
      std::set<EntityId> seen{derived};
      std::deque<EntityId> pending{derived};
      while (!pending.empty())
      {
        const auto current = pending.front();
        pending.pop_front();
        if (current == base)
        {
          return true;
        }
        const auto known = _facts.classes.find(current);
        if (known == _facts.classes.end())
        {
          continue;
        }
        for (const auto& next : known->second.bases)
        {
          if (seen.insert(next).second)
          {
            pending.push_back(next);
          }
        }
      }
      return false;
    }

    bool catches(const std::optional<ExceptionType>& caught,
                 const std::optional<ExceptionType>& thrown) const
    {
      if (!caught)
      {
        return true;
      }
      if (!thrown)
      {
        return false;
      }
      if (caught->isPointer && thrown->isPointer
          && caught->name == voidPointerName)
      {
        return true;
      }
      if (caught->isPointer != thrown->isPointer)
      {
        return false;
      }
      if (caught->record && thrown->record)
      {
        return isDerivedFrom(*thrown->record, *caught->record);
      }
      return caught->name == thrown->name;
    }

    void follow(Unwinding& state, EdgeId edge) const
    {
      if (!state.unwound.insert(edge).second)
      {
        return;
      }
      if (_raises.count(edge) != 0)
      {
        state.pending.push_back(edge);
      }
    }

    void step(Unwinding& state, EdgeId previous, EdgeId edge) const
    {
      state.steps.insert({previous, edge});
      follow(state, edge);
    }

    EdgeId uncaughtEdge(NodeId function, TerminateReason reason)
    {
      const auto [known, inserted] = _terminateEdges.try_emplace({function, reason}, 0);
      if (inserted)
      {
        known->second = addEdge(function, add(TerminateNode{reason}), UncaughtEdge{});
      }
      return known->second;
    }

    EdgeId uncaughtEdge(NodeId function, UndefinedBehaviorReason reason)
    {
      const auto [known, inserted] =
          _undefinedBehaviorEdges.try_emplace({function, reason}, 0);
      if (inserted)
      {
        known->second =
            addEdge(function, add(UndefinedBehaviorNode{reason}), UncaughtEdge{});
      }
      return known->second;
    }

    EdgeId leavingNounwindEdge(NodeId function, NounwindCause cause)
    {
      switch (cause)
      {
      case NounwindCause::exceptionSpecification:
        return uncaughtEdge(function, TerminateReason::noexceptFunction);
      case NounwindCause::nothrowAttribute:
      case NounwindCause::exceptionsDisabled:
      case NounwindCause::cxxExceptionsDisabled:
        return uncaughtEdge(function, UndefinedBehaviorReason::nounwind);
      }
      llvm_unreachable("unknown NounwindCause");
    }

    void markReachableFromMain()
    {
      std::deque<NodeId> pending;
      for (const auto& [id, definition] : _facts.functions)
      {
        if (definition.isEntryPoint)
        {
          pending.push_back(_functionNodes.at(id));
        }
      }
      while (!pending.empty())
      {
        const auto function = pending.front();
        pending.pop_front();
        auto* node = std::get_if<FunctionNode>(&_graph.nodes.at(function));
        if (node == nullptr || node->isReachableFromMain)
        {
          continue;
        }
        node->isReachableFromMain = true;
        for (const auto edge : lookup(_callEdgesByCaller, function))
        {
          pending.push_back(_graph.edges.at(edge).from);
        }
      }
    }

    void addRethrowsWithoutHandledException()
    {
      std::set<NodeId> reached;
      std::deque<NodeId> pending;
      for (const auto& [id, definition] : _facts.functions)
      {
        const auto function = _functionNodes.at(id);
        if (definition.isEntryPoint && reached.insert(function).second)
        {
          pending.push_back(function);
        }
      }
      while (!pending.empty())
      {
        const auto function = pending.front();
        pending.pop_front();
        if (!lookup(_rethrowSitesOutsideHandlers, function).empty())
        {
          uncaughtEdge(function, TerminateReason::noCurrentlyHandledException);
        }
        for (const auto edge : lookup(_callEdgesOutsideHandlers, function))
        {
          const auto callee = _graph.edges.at(edge).from;
          if (reached.insert(callee).second)
          {
            pending.push_back(callee);
          }
        }
      }
    }

    std::vector<EdgeId> leavingEdges(NodeId function)
    {
      if (const auto* node = std::get_if<FunctionNode>(&_graph.nodes.at(function)))
      {
        if (const auto cause = node->border.nounwindCause)
        {
          return {leavingNounwindEdge(function, *cause)};
        }
        if (node->isEntryPoint)
        {
          return {uncaughtEdge(function, TerminateReason::noHandler)};
        }
      }
      const auto callers = lookup(_callEdges, function);
      return {callers.begin(), callers.end()};
    }

    void leave(Unwinding& state, EdgeId arriving, NodeId function)
    {
      auto [leaving, isFirst] = state.leaving.try_emplace(function);
      if (isFirst)
      {
        leaving->second = leavingEdges(function);
      }
      for (const auto edge : leaving->second)
      {
        step(state, arriving, edge);
      }
    }

    void arrive(Unwinding& state,
                EdgeId arriving,
                const Raise& raise,
                const std::optional<ExceptionType>& thrownType)
    {
      for (const auto handler : raise.handlers)
      {
        const bool isCaught =
            catches(std::get<CatchNode>(_graph.nodes.at(handler)).caughtType, thrownType);
        if (!isCaught)
        {
          continue;
        }
        const auto catchEdge = _catchEdges.at(handler);
        step(state, arriving, catchEdge);
        for (const auto site : rethrowSitesOf(handler))
        {
          step(state, catchEdge, rethrowEdge(state.exception.source, site));
        }
        return;
      }
      leave(state, arriving, raise.function);
    }

    Exception unwind(const Raised& raised)
    {
      const auto thrownType =
          std::get<ExceptionNode>(_graph.nodes.at(raised.exception)).thrownType;
      Unwinding state{{raised.exception, raised.throwEdge, {}}, {}, {}, {}, {}};
      follow(state, raised.throwEdge);
      while (!state.pending.empty())
      {
        const auto edge = state.pending.front();
        state.pending.pop_front();
        arrive(state, edge, _raises.at(edge), thrownType);
      }
      state.exception.unwinds.assign(state.steps.begin(), state.steps.end());
      return std::move(state.exception);
    }

    struct PointedFunctions
    {
        std::vector<EntityId> functions;
        bool isComplete = false;
    };

    PointedFunctions pointedFunctions(const clang::ssaf::EntityPointerLevelSet& pointers) const
    {
      PointedFunctions result{{}, !pointers.empty()};
      std::set<clang::ssaf::EntityPointerLevel> seen(pointers.begin(), pointers.end());
      std::deque<clang::ssaf::EntityPointerLevel> pending(pointers.begin(), pointers.end());
      while (!pending.empty())
      {
        const auto current = pending.front();
        pending.pop_front();
        const bool isFunctionAddress = current.getPointerLevel() == 0
                                       && _facts.functions.count(current.getEntity()) != 0;
        if (isFunctionAddress)
        {
          result.functions.push_back(current.getEntity());
          continue;
        }
        const auto assigned = _assignedFrom.find(current);
        if (assigned == _assignedFrom.end())
        {
          result.isComplete = false;
          continue;
        }
        for (const auto& source : assigned->second)
        {
          if (seen.insert(source).second)
          {
            pending.push_back(source);
          }
        }
      }
      return result;
    }

    const ExceptionFlowFacts& _facts;
    std::map<clang::ssaf::EntityPointerLevel, std::set<clang::ssaf::EntityPointerLevel>>
        _assignedFrom;
    ExceptionGraph& _graph;
    std::map<EntityId, const VirtualMethod*> _virtualMethods;
    std::map<EntityId, std::set<EntityId>> _directOverriders;
    std::map<EntityId, NodeId> _functionNodes;
    std::map<std::string, NodeId> _unmappedFunctionNodes;
    std::map<EdgeId, Raise> _raises;
    std::map<NodeId, EdgeId> _catchEdges;
    std::map<NodeId, std::vector<EdgeId>> _callEdges;
    std::map<NodeId, std::vector<EdgeId>> _callEdgesByCaller;
    std::map<NodeId, std::vector<EdgeId>> _callEdgesInHandler;
    std::map<NodeId, std::vector<EdgeId>> _callEdgesOutsideHandlers;
    std::vector<RethrowSite> _rethrowSites;
    std::map<NodeId, std::vector<std::size_t>> _rethrowSitesInHandler;
    std::map<NodeId, std::vector<std::size_t>> _rethrowSitesOutsideHandlers;
    std::map<NodeId, std::vector<std::size_t>> _rethrowSitesOfHandler;
    std::map<std::pair<NodeId, std::size_t>, EdgeId> _rethrowEdges;
    std::map<std::pair<NodeId, TerminateReason>, EdgeId> _terminateEdges;
    std::map<std::pair<NodeId, UndefinedBehaviorReason>, EdgeId> _undefinedBehaviorEdges;
    std::vector<Raised> _raised;
};

llvm::Error checkCatchIndices(const FunctionDefinition& function)
{
  const auto inCatches = [&](CatchIndex index) { return index < function.catches.size(); };
  const auto sitesInCatches = [&](const auto& sites) {
    return llvm::all_of(sites, [&](const auto& site) {
      if constexpr (requires { site.enclosingHandler; })
      {
        if (site.enclosingHandler && !inCatches(*site.enclosingHandler))
        {
          return false;
        }
      }
      return llvm::all_of(site.handlers, inCatches);
    });
  };
  if (sitesInCatches(function.throws) && sitesInCatches(function.rethrows) && sitesInCatches(function.calls))
  {
    return llvm::Error::success();
  }
  return llvm::createStringError("%s: catch index out of range of its %zu catches",
                                 function.name.c_str(),
                                 function.catches.size());
}
} // namespace

llvm::Error ExceptionFlowFactsAnalysis::add(EntityId id, const ExceptionFlowSummary& summary)
{
  if (const auto* function = std::get_if<FunctionDefinition>(&summary.facts()))
  {
    if (auto error = checkCatchIndices(*function))
    {
      return error;
    }
  }
  std::visit(llvm::makeVisitor([&](const FunctionDefinition& function) {
                          getResult().functions.emplace(id, function);
                        },
                        [&](const ClassDefinition& definition) {
                          getResult().classes.emplace(id, definition);
                        }),
             summary.facts());
  return llvm::Error::success();
}

llvm::Error
ExceptionGraphAnalysis::initialize(const ExceptionFlowFacts& facts,
                                   const clang::ssaf::PointerFlowAnalysisResult& pointerFlow)
{
  return GraphBuilder(facts, pointerFlow, getResult()).build();
}

llvm::Expected<bool> ExceptionGraphAnalysis::step()
{
  return false;
}
} // namespace exa::analyzer
