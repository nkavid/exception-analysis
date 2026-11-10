#include "exception_flow/analyzer/graph_json.hpp"
#include "exception_flow/analyzer/analysis.hpp"
#include "exception_flow/analyzer/border_crossing.hpp"
#include "exception_flow/summary/json_format.hpp"
#include "exception_flow/summary/summary.hpp"

#include <clang/ScalableStaticAnalysis/Core/EntityLinker/LUSummary.h>
#include <clang/ScalableStaticAnalysis/Core/Serialization/JSONFormat.h>
#include <clang/ScalableStaticAnalysis/Core/Serialization/SerializationFormat.h>
#include <clang/ScalableStaticAnalysis/Core/Serialization/SerializationFormatRegistry.h>
#include <clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/AnalysisDriver.h>
#include <clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/AnalysisRegistry.h>
#include <clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/WPASuite.h>
#include <clang/ScalableStaticAnalysis/SSAFBuiltinForceLinker.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Registry.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/WithColor.h>
#include <llvm/Support/raw_ostream.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace exa::analyzer
{
namespace
{
namespace cl = llvm::cl;

inline constexpr int exitInvalidInput       = 1;
inline constexpr int exitUncaught = 2;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
llvm::Registry<clang::ssaf::JSONFormat::FormatInfo>::Add<summary::ExceptionFlowJSONFormatInfo>
    registerJsonFormat(summary::ExceptionFlowSummary::name,
                       "JSON format of the ExaExceptionFlow summary");

clang::ssaf::AnalysisRegistry::Add<ExceptionFlowFactsAnalysis>
    registerFactsAnalysis("Collects ExaExceptionFlow summaries of one link unit");

clang::ssaf::AnalysisRegistry::Add<ExceptionGraphAnalysis>
    registerGraphAnalysis("Builds the exception unwinding graph of one link unit");

cl::OptionCategory analyzeCategory("exa-analyze options");

cl::opt<std::string> linkUnitPath(cl::Positional,
                                  cl::desc("<SSAF link unit summary>"),
                                  cl::Required,
                                  cl::cat(analyzeCategory));

cl::opt<std::string> outputPath("output",
                                cl::desc("Exception graph as JSON"),
                                cl::value_desc("path"),
                                cl::init("-"),
                                cl::cat(analyzeCategory));

cl::opt<std::string> manifestPath("manifest",
                                  cl::desc("exa_manifest.json of the cc_binary"),
                                  cl::value_desc("path"),
                                  cl::Required,
                                  cl::cat(analyzeCategory));

cl::list<std::string> underAnalysisFiles(
    "under-analysis",
    cl::desc("Source file of a target tagged exa_under_analysis; repeatable"),
    cl::value_desc("path"),
    cl::cat(analyzeCategory));

cl::opt<bool> failOnUncaught(
    "fail-on-uncaught",
    cl::desc("Exit with status 2 when the graph has an uncaught edge"),
    cl::cat(analyzeCategory));

cl::extrahelp usageHelp(R"(
Builds the exception unwinding graph of an SSAF link unit summary produced by
clang-ssaf-linker:

  exa-analyze app.lu.json --manifest=app.exa_manifest.json --output=app.exa_graph.json
)");
#pragma clang diagnostic pop

void reportError(llvm::Error error)
{
  llvm::WithColor::error() << llvm::toString(std::move(error)) << '\n';
}

llvm::Expected<std::unique_ptr<clang::ssaf::SerializationFormat>>
formatFromExtension(llvm::StringRef path)
{
  auto extension = llvm::sys::path::extension(path);
  if (!extension.consume_front(".") || !clang::ssaf::isFormatRegistered(extension))
  {
    return llvm::createStringError("%s: unknown SSAF serialization format",
                                   path.str().c_str());
  }
  return clang::ssaf::makeFormat(extension);
}

llvm::Expected<clang::ssaf::WPASuite> analyze()
{
  auto format = formatFromExtension(linkUnitPath);
  if (!format)
  {
    return format.takeError();
  }
  auto linkUnit = (*format)->readLUSummary(linkUnitPath);
  if (!linkUnit)
  {
    return linkUnit.takeError();
  }
  clang::ssaf::AnalysisDriver driver(
      std::make_unique<clang::ssaf::LUSummary>(std::move(*linkUnit)));
  return driver.run<ExceptionGraph>();
}

using CompilationUnits = std::map<std::string, CompilationUnit>;

llvm::Error manifestError(llvm::StringRef expected)
{
  return llvm::createStringError("%s: expected %s",
                                 manifestPath.getValue().c_str(),
                                 expected.str().c_str());
}

llvm::Expected<CompilationUnits> compilationUnits(const llvm::json::Value& manifest)
{
  const auto* root    = manifest.getAsObject();
  const auto* targets = root != nullptr ? root->getArray("targets") : nullptr;
  if (targets == nullptr)
  {
    return manifestError("object with array 'targets'");
  }
  CompilationUnits units;
  for (const auto& targetValue : *targets)
  {
    const auto* target  = targetValue.getAsObject();
    const auto* sources = target != nullptr ? target->getArray("sources") : nullptr;
    const auto label    = target != nullptr ? target->getString("label") : std::nullopt;
    if (sources == nullptr || !label)
    {
      return manifestError("target with string 'label' and array 'sources'");
    }
    for (const auto& sourceValue : *sources)
    {
      const auto* source = sourceValue.getAsObject();
      const auto id      = source != nullptr ? source->getString("compilation_unit") : std::nullopt;
      const auto src     = source != nullptr ? source->getString("src") : std::nullopt;
      if (!id || !src)
      {
        return manifestError("source with strings 'compilation_unit' and 'src'");
      }
      if (!units.emplace(id->str(), CompilationUnit{label->str(), src->str()})
               .second)
      {
        return manifestError("unique 'compilation_unit' values");
      }
    }
  }
  return units;
}

llvm::Expected<CompilationUnits> readManifest()
{
  auto buffer = llvm::MemoryBuffer::getFile(manifestPath);
  if (!buffer)
  {
    return llvm::createFileError(manifestPath, buffer.getError());
  }
  auto manifest = llvm::json::parse((*buffer)->getBuffer());
  if (!manifest)
  {
    return llvm::createFileError(manifestPath, manifest.takeError());
  }
  return compilationUnits(*manifest);
}

llvm::Error writeGraph(const ExceptionGraph& graph,
                       const std::vector<BorderCrossing>& crossings)
{
  std::error_code error;
  llvm::raw_fd_ostream out(outputPath, error, llvm::sys::fs::OF_Text);
  if (error)
  {
    return llvm::createFileError(outputPath, error);
  }
  const std::set<std::string> underAnalysis(underAnalysisFiles.begin(),
                                            underAnalysisFiles.end());
  out << llvm::formatv("{0:2}", toJSON(graph, crossings, underAnalysis)) << '\n';
  return llvm::Error::success();
}

int run(int argc, const char** argv)
{
  llvm::InitLLVM initialization(argc, argv);
  cl::HideUnrelatedOptions(analyzeCategory);
  cl::ParseCommandLineOptions(argc, argv, "ExceptionFlow whole-program analysis\n");

  auto suite = analyze();
  if (!suite)
  {
    reportError(suite.takeError());
    return exitInvalidInput;
  }
  auto graph = suite->get<ExceptionGraph>();
  if (!graph)
  {
    reportError(graph.takeError());
    return exitInvalidInput;
  }
  auto units = readManifest();
  if (!units)
  {
    reportError(units.takeError());
    return exitInvalidInput;
  }
  auto crossings = borderCrossings(*graph, *units);
  if (!crossings)
  {
    reportError(crossings.takeError());
    return exitInvalidInput;
  }
  if (auto error = writeGraph(*graph, *crossings))
  {
    reportError(std::move(error));
    return exitInvalidInput;
  }
  return failOnUncaught && hasUncaught(*graph) ? exitUncaught : 0;
}
} // namespace
} // namespace exa::analyzer

int main(int argc, const char** argv)
{
  return exa::analyzer::run(argc, argv);
}
