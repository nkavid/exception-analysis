#pragma once

#include "exception_flow/summary/enum_string.hpp"
#include "exception_flow/summary/summary.hpp"

#include <llvm/Support/raw_os_ostream.h>

#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace exa::summary
{
namespace test
{
inline void write(std::ostream& out, const EntityId& id)
{
  llvm::raw_os_ostream stream(out);
  stream << id;
}

inline void write(std::ostream& out, const clang::ssaf::EntityPointerLevel& pointer)
{
  write(out, pointer.getEntity());
  out << "*" << pointer.getPointerLevel();
}

inline void write(std::ostream& out, bool value)
{
  out << (value ? "true" : "false");
}

inline void write(std::ostream& out, const std::string& value)
{
  out << '"' << value << '"';
}

template <typename Value>
void write(std::ostream& out, const Value& value)
{
  out << value;
}

template <typename Value>
void write(std::ostream& out, const std::optional<Value>& value)
{
  if (!value)
  {
    out << "nullopt";
    return;
  }
  write(out, *value);
}

template <typename... Alternatives>
void write(std::ostream& out, const std::variant<Alternatives...>& value)
{
  std::visit([&out](const auto& alternative) { write(out, alternative); }, value);
}

template <typename Range>
void writeRange(std::ostream& out, const Range& range)
{
  out << "[";
  const char* separator = "";
  for (const auto& element : range)
  {
    out << separator;
    write(out, element);
    separator = ", ";
  }
  out << "]";
}

template <typename Value>
void write(std::ostream& out, const std::vector<Value>& values)
{
  writeRange(out, values);
}

template <typename Value, typename Compare>
void write(std::ostream& out, const std::set<Value, Compare>& values)
{
  writeRange(out, values);
}

template <typename... Fields>
std::ostream& writeFields(std::ostream& out [[clang::lifetimebound]], const Fields&... fields)
{
  out << "{";
  const char* separator = "";
  ((out << separator, write(out, fields), separator = ", "), ...);
  out << "}";
  return out;
}
} // namespace test

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                ExceptionSpecification value)
{
  out << toString(exceptionSpecificationStrings, value).str();
  return out;
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]], NounwindCause value)
{
  out << toString(nounwindCauseStrings, value).str();
  return out;
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]], LanguageLinkage value)
{
  out << toString(languageLinkageStrings, value).str();
  return out;
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const SourceLocation& location)
{
  out << location.file << ":" << location.line << ":" << location.column;
  return out;
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const ExceptionType& type)
{
  return test::writeFields(out, type.name, type.isPointer, type.record);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const FunctionBorder& border)
{
  return test::writeFields(
      out, border.exceptionSpecification, border.nounwindCause, border.languageLinkage);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]], const Catch& handler)
{
  return test::writeFields(out, handler.caughtType, handler.sourceLocation);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]], const Throw& thrown)
{
  return test::writeFields(out, thrown.thrownType, thrown.handlers, thrown.sourceLocation);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const Rethrow& rethrown)
{
  return test::writeFields(
      out, rethrown.handlers, rethrown.enclosingHandler, rethrown.sourceLocation);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const DirectCallee& callee)
{
  return test::writeFields(out, callee.function, callee.name, callee.border);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const VirtualCallee& callee)
{
  return test::writeFields(out, callee.method, callee.name, callee.border);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const IndirectCallee& callee)
{
  return test::writeFields(out, callee.functionType, callee.border, callee.calleePointers);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const UnmappedCallee& callee)
{
  return test::writeFields(out, callee.name, callee.border);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]], const Call& call)
{
  return test::writeFields(
      out, call.callee, call.handlers, call.enclosingHandler, call.sourceLocation);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const FunctionDefinition& definition)
{
  return test::writeFields(out,
                           definition.name,
                           definition.sourceLocation,
                           definition.inSystemHeader,
                           definition.compilationUnit,
                           definition.border,
                           definition.isEntryPoint,
                           definition.catches,
                           definition.throws,
                           definition.rethrows,
                           definition.calls);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const VirtualMethod& method)
{
  return test::writeFields(out,
                           method.method,
                           method.name,
                           method.border,
                           method.isPureVirtual,
                           method.overriddenMethods);
}

inline std::ostream& operator<<(std::ostream& out [[clang::lifetimebound]],
                                const ClassDefinition& definition)
{
  return test::writeFields(out,
                           definition.name,
                           definition.sourceLocation,
                           definition.inSystemHeader,
                           definition.bases,
                           definition.virtualMethods);
}
} // namespace exa::summary
