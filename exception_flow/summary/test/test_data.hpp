#pragma once

#include "exception_flow/summary/summary.hpp"

#include <llvm/Support/JSON.h>

#include <filesystem>

namespace exa::summary::test::test_data
{
FunctionDefinition throwingFunction();

std::filesystem::path path(const std::filesystem::path& fileName);

llvm::json::Value read_file(const std::filesystem::path& fileName);
} // namespace exa::summary::test::test_data
