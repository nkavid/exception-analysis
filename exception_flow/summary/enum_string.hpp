#pragma once

#include "exception_flow/summary/summary.hpp"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>

#include <cstddef>

namespace exa::summary
{
template <typename Enum>
struct EnumString
{
    Enum value;
    llvm::StringLiteral string;
};

inline constexpr EnumString<ExceptionSpecification> exceptionSpecificationStrings[] = {
    {        ExceptionSpecification::nonThrowing,          "non_throwing"},
    {ExceptionSpecification::potentiallyThrowing, "potentially_throwing"},
};

inline constexpr EnumString<NounwindCause> nounwindCauseStrings[] = {
    {NounwindCause::exceptionSpecification, "exception_specification"},
    {      NounwindCause::nothrowAttribute,       "nothrow_attribute"},
    {    NounwindCause::exceptionsDisabled,     "exceptions_disabled"},
    { NounwindCause::cxxExceptionsDisabled, "cxx_exceptions_disabled"},
};

inline constexpr EnumString<LanguageLinkage> languageLinkageStrings[] = {
    {  LanguageLinkage::c,   "c"},
    {LanguageLinkage::cxx, "cxx"},
};

template <typename Enum, std::size_t Count>
llvm::StringRef toString(const EnumString<Enum> (&strings)[Count], Enum value)
{
  for (const auto& entry : strings)
  {
    if (entry.value == value)
    {
      return entry.string;
    }
  }
  llvm_unreachable("enum value without string");
}
} // namespace exa::summary
