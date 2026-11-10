#pragma once

#include <clang/ScalableStaticAnalysis/Core/Model/EntityIdTable.h>
#include <clang/ScalableStaticAnalysis/Core/Serialization/JSONFormat.h>
#include <clang/ScalableStaticAnalysis/Core/TUSummary/EntitySummary.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>

#include <memory>

namespace exa::summary
{
llvm::json::Object serialize(const clang::ssaf::EntitySummary& summary,
                             clang::ssaf::JSONFormat::EntityIdToJSONFn toJson);

llvm::Expected<std::unique_ptr<clang::ssaf::EntitySummary>>
deserialize(const llvm::json::Object& object,
            clang::ssaf::EntityIdTable& table,
            clang::ssaf::JSONFormat::EntityIdFromJSONFn fromJson);

struct ExceptionFlowJSONFormatInfo final : clang::ssaf::JSONFormat::FormatInfo
{
    ExceptionFlowJSONFormatInfo();
    ~ExceptionFlowJSONFormatInfo() override;
};
} // namespace exa::summary
