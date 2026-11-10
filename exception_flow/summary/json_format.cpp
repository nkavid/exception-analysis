#include "exception_flow/summary/json_format.hpp"
#include "exception_flow/summary/enum_string.hpp"
#include "exception_flow/summary/summary.hpp"

#include <clang/ScalableStaticAnalysis/Analyses/EntityPointerLevel/EntityPointerLevelFormat.h>
#include <clang/ScalableStaticAnalysis/Core/Serialization/JSONFormat.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>

#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace exa::summary
{
namespace json = llvm::json;
using clang::ssaf::EntityPointerLevelSet;
using clang::ssaf::JSONFormat;

namespace
{
thread_local const JSONFormat::EntityIdToJSONFn* entityIdToJSON     = nullptr;
thread_local const JSONFormat::EntityIdFromJSONFn* entityIdFromJSON = nullptr;
thread_local std::string* entityIdError                             = nullptr;

json::Value toJSON(const std::optional<EntityId>& id)
{
  return id ? json::Value((*entityIdToJSON)(*id)) : json::Value(nullptr);
}

bool read(const json::Value& value, std::optional<EntityId>& out, json::Path path)
{
  const auto* object = value.getAsObject();
  if (object == nullptr)
  {
    path.report("expected entity id object");
    return false;
  }
  auto id = (*entityIdFromJSON)(*object);
  if (!id)
  {
    *entityIdError = llvm::toString(id.takeError());
    path.report("invalid entity id");
    return false;
  }
  out.emplace(*id);
  return true;
}

json::Value toJSON(const std::set<EntityId>& ids)
{
  return json::Array(llvm::map_range(ids, [](EntityId id) { return json::Value((*entityIdToJSON)(id)); }));
}

json::Value toJSON(const EntityPointerLevelSet& pointers)
{
  return clang::ssaf::entityPointerLevelSetToJSON(llvm::make_range(pointers.begin(), pointers.end()),
                                                  *entityIdToJSON);
}

bool read(const json::Value& value, EntityPointerLevelSet& out, json::Path path)
{
  const auto* array = value.getAsArray();
  if (array == nullptr)
  {
    path.report("expected array");
    return false;
  }
  auto pointers = clang::ssaf::entityPointerLevelSetFromJSON(*array, *entityIdFromJSON);
  if (!pointers)
  {
    *entityIdError = llvm::toString(pointers.takeError());
    path.report("invalid entity pointer levels");
    return false;
  }
  out = std::move(*pointers);
  return true;
}

template <typename T, typename Value>
bool emplace(std::optional<T>& out, Value&& value)
{
  out.emplace(std::forward<Value>(value));
  return true;
}

bool read(const json::Value& value, std::optional<DirectCallee>& out, json::Path path);
bool read(const json::Value& value, std::optional<VirtualCallee>& out, json::Path path);
bool read(const json::Value& value, std::optional<Call>& out, json::Path path);
bool read(const json::Value& value, std::optional<VirtualMethod>& out, json::Path path);

template <typename T>
bool read(const json::Value& value, std::optional<T>& out, json::Path path)
{
  return fromJSON(value, out.emplace(), path);
}

template <typename T>
bool read(const json::Value& value, std::vector<T>& out, json::Path path)
{
  const auto* array = value.getAsArray();
  if (array == nullptr)
  {
    path.report("expected array");
    return false;
  }
  std::vector<T> elements;
  for (std::size_t index = 0; index < array->size(); ++index)
  {
    std::optional<T> element;
    if (!read((*array)[index], element, path.index(static_cast<unsigned>(index))))
    {
      return false;
    }
    elements.push_back(std::move(*element));
  }
  out = std::move(elements);
  return true;
}

bool read(const json::Value& value, std::set<EntityId>& out, json::Path path)
{
  std::vector<EntityId> ids;
  if (!read(value, ids, path))
  {
    return false;
  }
  out.insert(ids.begin(), ids.end());
  return true;
}

constexpr llvm::StringLiteral typeName(std::type_identity<DirectCallee>) { return "DirectCallee"; }
constexpr llvm::StringLiteral typeName(std::type_identity<VirtualCallee>) { return "VirtualCallee"; }
constexpr llvm::StringLiteral typeName(std::type_identity<IndirectCallee>) { return "IndirectCallee"; }
constexpr llvm::StringLiteral typeName(std::type_identity<UnmappedCallee>) { return "UnmappedCallee"; }
constexpr llvm::StringLiteral typeName(std::type_identity<FunctionDefinition>) { return "FunctionDefinition"; }
constexpr llvm::StringLiteral typeName(std::type_identity<ClassDefinition>) { return "ClassDefinition"; }

template <typename... Alternatives>
bool read(const json::Object& object, std::optional<std::variant<Alternatives...>>& out, json::Path path)
{
  if (object.size() != 1)
  {
    path.report("expected one key naming the alternative type");
    return false;
  }
  const auto& [key, alternative] = *object.begin();
  const auto readAs = [&]<typename Alternative>(std::type_identity<Alternative> type) {
    std::optional<Alternative> result;
    return llvm::StringRef(key) == typeName(type) && read(alternative, result, path.field(key))
        && emplace(out, std::move(*result));
  };
  return (readAs(std::type_identity<Alternatives>{}) || ...);
}

template <typename... Alternatives>
bool read(const json::Value& value, std::optional<std::variant<Alternatives...>>& out, json::Path path)
{
  const auto* object = value.getAsObject();
  if (object == nullptr)
  {
    path.report("expected object");
    return false;
  }
  return read(*object, out, path);
}

template <typename T>
bool mapRequired(const json::Value& value, llvm::StringLiteral key, T& out, json::Path path)
{
  const auto* field = value.getAsObject()->get(key);
  if (field == nullptr)
  {
    path.field(key).report("missing value");
    return false;
  }
  return read(*field, out, path.field(key));
}

bool mapOptionalId(const json::Value& value, llvm::StringLiteral key, std::optional<EntityId>& out, json::Path path)
{
  const auto* field = value.getAsObject()->get(key);
  return field == nullptr || field->getAsNull() || read(*field, out, path.field(key));
}

template <typename Enum, std::size_t Count>
bool enumFromJSON(const json::Value& value, Enum& out, json::Path path, const EnumString<Enum> (&strings)[Count])
{
  const auto string = value.getAsString();
  const auto* entry = llvm::find_if(strings, [&](const auto& candidate) { return candidate.string == string; });
  if (!string || entry == std::end(strings))
  {
    path.report("expected enumerator name");
    return false;
  }
  out = entry->value;
  return true;
}

bool read(const json::Value& value, std::optional<DirectCallee>& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  std::optional<EntityId> function;
  std::string name;
  FunctionBorder border;
  return object && mapRequired(value, "function", function, path) && object.map("name", name)
      && object.map("border", border) && emplace(out, DirectCallee{*function, name, border});
}

bool read(const json::Value& value, std::optional<VirtualCallee>& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  std::optional<EntityId> method;
  std::string name;
  FunctionBorder border;
  return object && mapRequired(value, "method", method, path) && object.map("name", name)
      && object.map("border", border) && emplace(out, VirtualCallee{*method, name, border});
}

bool read(const json::Value& value, std::optional<Call>& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  std::optional<Callee> callee;
  std::vector<CatchIndex> handlers;
  std::optional<CatchIndex> enclosingHandler;
  std::optional<SourceLocation> sourceLocation;
  return object && mapRequired(value, "callee", callee, path) && object.map("handlers", handlers)
      && object.map("enclosingHandler", enclosingHandler) && object.map("sourceLocation", sourceLocation)
      && emplace(out, Call{std::move(*callee), handlers, enclosingHandler, sourceLocation});
}

bool read(const json::Value& value, std::optional<VirtualMethod>& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  std::optional<EntityId> id;
  std::string name;
  FunctionBorder border;
  bool isPureVirtual = false;
  std::set<EntityId> overriddenMethods;
  return object && mapRequired(value, "method", id, path) && object.map("name", name)
      && object.map("border", border) && object.map("isPureVirtual", isPureVirtual)
      && mapRequired(value, "overriddenMethods", overriddenMethods, path)
      && emplace(out, VirtualMethod{*id, name, border, isPureVirtual, overriddenMethods});
}
} // namespace

template <typename... Alternatives>
static json::Value toJSON(const std::variant<Alternatives...>& variant)
{
  return std::visit([]<typename Alternative>(const Alternative& alternative) {
    return json::Value(json::Object{{typeName(std::type_identity<Alternative>{}), alternative}});
  }, variant);
}

static json::Value toJSON(ExceptionSpecification value) { return toString(exceptionSpecificationStrings, value); }
static json::Value toJSON(NounwindCause value) { return toString(nounwindCauseStrings, value); }
static json::Value toJSON(LanguageLinkage value) { return toString(languageLinkageStrings, value); }

static bool fromJSON(const json::Value& value, ExceptionSpecification& out, json::Path path)
{
  return enumFromJSON(value, out, path, exceptionSpecificationStrings);
}

static bool fromJSON(const json::Value& value, NounwindCause& out, json::Path path)
{
  return enumFromJSON(value, out, path, nounwindCauseStrings);
}

static bool fromJSON(const json::Value& value, LanguageLinkage& out, json::Path path)
{
  return enumFromJSON(value, out, path, languageLinkageStrings);
}

static json::Value toJSON(const SourceLocation& location)
{
  return json::Object{{"file", location.file}, {"line", location.line}, {"column", location.column}};
}

static bool fromJSON(const json::Value& value, SourceLocation& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("file", out.file) && object.map("line", out.line) && object.map("column", out.column);
}

static json::Value toJSON(const ExceptionType& type)
{
  return json::Object{{"name", type.name}, {"isPointer", type.isPointer}, {"record", toJSON(type.record)}};
}

static bool fromJSON(const json::Value& value, ExceptionType& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("name", out.name) && object.map("isPointer", out.isPointer)
      && mapOptionalId(value, "record", out.record, path);
}

static json::Value toJSON(const FunctionBorder& border)
{
  return json::Object{{"exceptionSpecification", border.exceptionSpecification},
                      {"nounwindCause", border.nounwindCause},
                      {"languageLinkage", border.languageLinkage}};
}

static bool fromJSON(const json::Value& value, FunctionBorder& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("exceptionSpecification", out.exceptionSpecification)
      && object.map("nounwindCause", out.nounwindCause) && object.map("languageLinkage", out.languageLinkage);
}

static json::Value toJSON(const Catch& catchSite)
{
  return json::Object{{"caughtType", catchSite.caughtType}, {"sourceLocation", catchSite.sourceLocation}};
}

static bool fromJSON(const json::Value& value, Catch& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("caughtType", out.caughtType) && object.map("sourceLocation", out.sourceLocation);
}

static json::Value toJSON(const Throw& throwSite)
{
  return json::Object{{"thrownType", throwSite.thrownType},
                      {"handlers", throwSite.handlers},
                      {"sourceLocation", throwSite.sourceLocation}};
}

static bool fromJSON(const json::Value& value, Throw& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("thrownType", out.thrownType) && object.map("handlers", out.handlers)
      && object.map("sourceLocation", out.sourceLocation);
}

static json::Value toJSON(const Rethrow& rethrow)
{
  return json::Object{{"handlers", rethrow.handlers},
                      {"enclosingHandler", rethrow.enclosingHandler},
                      {"sourceLocation", rethrow.sourceLocation}};
}

static bool fromJSON(const json::Value& value, Rethrow& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("handlers", out.handlers) && object.map("enclosingHandler", out.enclosingHandler)
      && object.map("sourceLocation", out.sourceLocation);
}

static json::Value toJSON(const DirectCallee& callee)
{
  return json::Object{{"function", toJSON(std::optional(callee.function))}, {"name", callee.name}, {"border", callee.border}};
}

static json::Value toJSON(const VirtualCallee& callee)
{
  return json::Object{{"method", toJSON(std::optional(callee.method))}, {"name", callee.name}, {"border", callee.border}};
}

static json::Value toJSON(const IndirectCallee& callee)
{
  return json::Object{{"functionType", callee.functionType},
                      {"border", callee.border},
                      {"calleePointers", toJSON(callee.calleePointers)}};
}

static bool fromJSON(const json::Value& value, IndirectCallee& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("functionType", out.functionType) && object.map("border", out.border)
      && mapRequired(value, "calleePointers", out.calleePointers, path);
}

static json::Value toJSON(const UnmappedCallee& callee)
{
  return json::Object{{"name", callee.name}, {"border", callee.border}};
}

static bool fromJSON(const json::Value& value, UnmappedCallee& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("name", out.name) && object.map("border", out.border);
}

static json::Value toJSON(const Call& call)
{
  return json::Object{{"callee", call.callee},
                      {"handlers", call.handlers},
                      {"enclosingHandler", call.enclosingHandler},
                      {"sourceLocation", call.sourceLocation}};
}

static json::Value toJSON(const EntityDefinition& definition)
{
  return json::Object{{"name", definition.name},
                      {"sourceLocation", definition.sourceLocation},
                      {"inSystemHeader", definition.inSystemHeader}};
}

static bool fromJSON(const json::Value& value, EntityDefinition& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("name", out.name) && object.map("sourceLocation", out.sourceLocation)
      && object.map("inSystemHeader", out.inSystemHeader);
}

static json::Value toJSON(const FunctionDefinition& definition)
{
  return json::Object{{"EntityDefinition", static_cast<const EntityDefinition&>(definition)},
                      {"compilationUnit", definition.compilationUnit},
                      {"border", definition.border},
                      {"isEntryPoint", definition.isEntryPoint},
                      {"catches", definition.catches},
                      {"throws", definition.throws},
                      {"rethrows", definition.rethrows},
                      {"calls", definition.calls}};
}

static bool fromJSON(const json::Value& value, FunctionDefinition& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("EntityDefinition", static_cast<EntityDefinition&>(out))
      && object.map("compilationUnit", out.compilationUnit) && object.map("border", out.border)
      && object.map("isEntryPoint", out.isEntryPoint) && object.map("catches", out.catches)
      && object.map("throws", out.throws) && object.map("rethrows", out.rethrows)
      && mapRequired(value, "calls", out.calls, path);
}

static json::Value toJSON(const VirtualMethod& method)
{
  return json::Object{{"method", toJSON(std::optional(method.method))},
                      {"name", method.name},
                      {"border", method.border},
                      {"isPureVirtual", method.isPureVirtual},
                      {"overriddenMethods", toJSON(method.overriddenMethods)}};
}

static json::Value toJSON(const ClassDefinition& definition)
{
  return json::Object{{"EntityDefinition", static_cast<const EntityDefinition&>(definition)},
                      {"bases", toJSON(definition.bases)},
                      {"virtualMethods", definition.virtualMethods}};
}

static bool fromJSON(const json::Value& value, ClassDefinition& out, json::Path path)
{
  json::ObjectMapper object(value, path);
  return object && object.map("EntityDefinition", static_cast<EntityDefinition&>(out))
      && mapRequired(value, "bases", out.bases, path) && mapRequired(value, "virtualMethods", out.virtualMethods, path);
}

json::Object serialize(const clang::ssaf::EntitySummary& summary, JSONFormat::EntityIdToJSONFn toJson)
{
  entityIdToJSON = &toJson;
  auto value     = toJSON(static_cast<const ExceptionFlowSummary&>(summary).facts());
  entityIdToJSON = nullptr;
  return std::move(*value.getAsObject());
}

llvm::Expected<std::unique_ptr<clang::ssaf::EntitySummary>>
deserialize(const json::Object& object, clang::ssaf::EntityIdTable&, JSONFormat::EntityIdFromJSONFn fromJson)
{
  std::string error;
  entityIdFromJSON = &fromJson;
  entityIdError    = &error;
  json::Path::Root root(ExceptionFlowSummary::name);
  std::optional<ExceptionFlowSummary::Facts> facts;
  const bool succeeded = read(object, facts, root);
  entityIdFromJSON     = nullptr;
  entityIdError        = nullptr;
  if (!succeeded)
  {
    return llvm::createStringError("%s%s%s", llvm::toString(root.getError()).c_str(), error.empty() ? "" : ": ", error.c_str());
  }
  return std::make_unique<ExceptionFlowSummary>(std::move(*facts));
}

ExceptionFlowJSONFormatInfo::ExceptionFlowJSONFormatInfo()
    : JSONFormat::FormatInfo(ExceptionFlowSummary::summaryName(), serialize, deserialize)
{}

ExceptionFlowJSONFormatInfo::~ExceptionFlowJSONFormatInfo() = default;
} // namespace exa::summary
