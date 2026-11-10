#include "exception_flow/extractor/test/summary_json.hpp"

#include "exception_flow/extractor/test/translation_unit.hpp"
#include "exception_flow/summary/json_format.hpp"
#include "exception_flow/summary/summary.hpp"

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/JSON.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace exa::extractor::test
{
namespace
{
std::string text(const llvm::json::Value& value)
{
  return llvm::formatv("{0}", value).str();
}

testing::AssertionResult mismatch(const std::string& path,
                                  const llvm::json::Value& actual,
                                  const llvm::json::Value& expected)
{
  return testing::AssertionFailure()
      << path << ": expected " << text(expected) << ", actual " << text(actual);
}

testing::AssertionResult hasFields(const llvm::json::Value& actual,
                                   const llvm::json::Value& expected,
                                   const std::string& path)
{
  if (const auto* expectedObject = expected.getAsObject())
  {
    const auto* actualObject = actual.getAsObject();
    if (actualObject == nullptr)
    {
      return mismatch(path, actual, expected);
    }
    for (const auto& [key, expectedField] : *expectedObject)
    {
      const auto fieldPath = path.empty() ? key.str() : path + "." + key.str();
      const auto* actualField = actualObject->get(key);
      if (actualField == nullptr)
      {
        return testing::AssertionFailure() << fieldPath << ": missing";
      }
      if (auto result = hasFields(*actualField, expectedField, fieldPath); !result)
      {
        return result;
      }
    }
    return testing::AssertionSuccess();
  }
  if (const auto* expectedArray = expected.getAsArray())
  {
    const auto* actualArray = actual.getAsArray();
    if (actualArray == nullptr || actualArray->size() != expectedArray->size())
    {
      return mismatch(path, actual, expected);
    }
    for (std::size_t index = 0; index < expectedArray->size(); ++index)
    {
      const auto elementPath = path + "[" + std::to_string(index) + "]";
      if (auto result = hasFields((*actualArray)[index], (*expectedArray)[index], elementPath);
          !result)
      {
        return result;
      }
    }
    return testing::AssertionSuccess();
  }
  if (actual != expected)
  {
    return mismatch(path, actual, expected);
  }
  return testing::AssertionSuccess();
}
} // namespace

llvm::json::Value exceptionFlowSummaryOutput(const TranslationUnit& unit)
{
  const summary::ExceptionFlowJSONFormatInfo format;
  const auto entityToJson = [&unit](summary::EntityId id) {
    return llvm::json::Object{
        {"@", unit.name(id)}
    };
  };
  llvm::json::Object result;
  for (const auto& [id, facts] : unit.facts())
  {
    result[unit.name(id)] = format.Serialize(summary::ExceptionFlowSummary(facts), entityToJson);
  }
  return result;
}

llvm::json::Value parse(llvm::StringRef text)
{
  auto value = llvm::json::parse(text);
  if (!value)
  {
    llvm::reportFatalUsageError("expected JSON does not parse: "
                                + llvm::Twine(llvm::toString(value.takeError())));
  }
  return std::move(*value);
}

testing::AssertionResult hasFields(const llvm::json::Value& actual,
                                   const llvm::json::Value& expected)
{
  return hasFields(actual, expected, "");
}
} // namespace exa::extractor::test
