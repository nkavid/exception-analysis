#pragma once

#include <clang/ScalableStaticAnalysis/Analyses/EntityPointerLevel/EntityPointerLevel.h>
#include <clang/ScalableStaticAnalysis/Core/Model/EntityId.h>
#include <clang/ScalableStaticAnalysis/Core/Model/SummaryName.h>
#include <clang/ScalableStaticAnalysis/Core/TUSummary/EntitySummary.h>
#include <llvm/ADT/StringRef.h>

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace exa::summary
{
using clang::ssaf::EntityId;

struct SourceLocation
{
    std::string file;
    unsigned line = 0;
    unsigned column = 0;

    bool operator==(const SourceLocation&) const = default;
};

struct ExceptionType
{
    std::string name;
    bool isPointer = false;
    std::optional<EntityId> record;

    bool operator==(const ExceptionType&) const = default;
};

enum class ExceptionSpecification
{
  nonThrowing,
  potentiallyThrowing,
};

enum class NounwindCause
{
  exceptionSpecification,
  nothrowAttribute,
  exceptionsDisabled,
  cxxExceptionsDisabled,
};

enum class LanguageLinkage
{
  c,
  cxx,
};

struct FunctionBorder
{
    std::optional<ExceptionSpecification> exceptionSpecification;
    std::optional<NounwindCause> nounwindCause;
    std::optional<LanguageLinkage> languageLinkage;

    bool operator==(const FunctionBorder&) const = default;
};

using CatchIndex = size_t;

struct Catch
{
    std::optional<ExceptionType> caughtType;
    std::optional<SourceLocation> sourceLocation;

    bool operator==(const Catch&) const = default;
};

struct Throw
{
    ExceptionType thrownType;
    std::vector<CatchIndex> handlers;
    std::optional<SourceLocation> sourceLocation;

    bool operator==(const Throw&) const = default;
};

struct Rethrow
{
    std::vector<CatchIndex> handlers;
    std::optional<CatchIndex> enclosingHandler;
    std::optional<SourceLocation> sourceLocation;

    bool operator==(const Rethrow&) const = default;
};

struct DirectCallee
{
    EntityId function;
    std::string name;
    FunctionBorder border;

    bool operator==(const DirectCallee&) const = default;
};

struct VirtualCallee
{
    EntityId method;
    std::string name;
    FunctionBorder border;

    bool operator==(const VirtualCallee&) const = default;
};

struct IndirectCallee
{
    std::string functionType;
    FunctionBorder border;
    clang::ssaf::EntityPointerLevelSet calleePointers;

    bool operator==(const IndirectCallee&) const = default;
};

struct UnmappedCallee
{
    std::string name;
    FunctionBorder border;

    bool operator==(const UnmappedCallee&) const = default;
};

using Callee =
    std::variant<DirectCallee, VirtualCallee, IndirectCallee, UnmappedCallee>;

struct Call
{
    Callee callee;
    std::vector<CatchIndex> handlers;
    std::optional<CatchIndex> enclosingHandler;
    std::optional<SourceLocation> sourceLocation;

    bool operator==(const Call&) const = default;
};

struct EntityDefinition
{
    std::string name;
    std::optional<SourceLocation> sourceLocation;
    bool inSystemHeader = false;

    bool operator==(const EntityDefinition&) const = default;
};

struct FunctionDefinition : EntityDefinition
{
    std::string compilationUnit{};
    FunctionBorder border{};
    bool isEntryPoint = false;
    std::vector<Catch> catches{};
    std::vector<Throw> throws{};
    std::vector<Rethrow> rethrows{};
    std::vector<Call> calls{};

    bool operator==(const FunctionDefinition&) const = default;
};

struct VirtualMethod
{
    EntityId method;
    std::string name;
    FunctionBorder border;
    bool isPureVirtual = false;
    std::set<EntityId> overriddenMethods;

    bool operator==(const VirtualMethod&) const = default;
};

struct ClassDefinition : EntityDefinition
{
    std::set<EntityId> bases{};
    std::vector<VirtualMethod> virtualMethods{};

    bool operator==(const ClassDefinition&) const = default;
};

class ExceptionFlowSummary final : public clang::ssaf::EntitySummary
{
  public:
    using Facts = std::variant<FunctionDefinition, ClassDefinition>;

    static constexpr llvm::StringLiteral name = "ExaExceptionFlow";

    explicit ExceptionFlowSummary(Facts facts)
        : _facts(std::move(facts))
    {}

    static clang::ssaf::SummaryName summaryName()
    {
      return clang::ssaf::SummaryName(name.str());
    }

    clang::ssaf::SummaryName getSummaryName() const override;

    const Facts& facts() const [[clang::lifetimebound]]
    {
      return _facts;
    }

  private:
    Facts _facts;
};
} // namespace exa::summary
