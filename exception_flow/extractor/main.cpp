#include "exception_flow/extractor/extractor.hpp"
#include "exception_flow/summary/json_format.hpp"
#include "exception_flow/summary/summary.hpp"

#include <clang/Frontend/FrontendActions.h>
#include <clang/ScalableStaticAnalysis/Core/Serialization/JSONFormat.h>
#include <clang/ScalableStaticAnalysis/Core/TUSummary/ExtractorRegistry.h>
#include <clang/ScalableStaticAnalysis/Frontend/TUSummaryExtractorFrontendAction.h>
#include <clang/ScalableStaticAnalysis/SSAFBuiltinForceLinker.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Registry.h>
#include <llvm/Support/WithColor.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>

namespace exa::extractor
{
namespace
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
llvm::cl::OptionCategory extractCategory("exa-extract options");
llvm::cl::extrahelp commonHelp(clang::tooling::CommonOptionsParser::HelpMessage);

clang::ssaf::TUSummaryExtractorRegistry::Add<ExceptionFlowExtractor> registerExtractor(
    summary::ExceptionFlowSummary::name,
    "Exception flow: throws, rethrows, calls, catches, class hierarchy");

llvm::Registry<clang::ssaf::JSONFormat::FormatInfo>::Add<summary::ExceptionFlowJSONFormatInfo>
    registerJsonFormat(summary::ExceptionFlowSummary::name,
                       "JSON format of the ExaExceptionFlow summary");
#pragma clang diagnostic pop

class ExtractSummaryActionFactory : public clang::tooling::FrontendActionFactory
{
  public:
    std::unique_ptr<clang::FrontendAction> create() override
    {
      return std::make_unique<clang::ssaf::TUSummaryExtractorFrontendAction>(
          std::make_unique<clang::SyntaxOnlyAction>());
    }
};

int run(int argc, const char** argv)
{
  auto options = clang::tooling::CommonOptionsParser::create(argc, argv, extractCategory);
  if (!options)
  {
    llvm::WithColor::error() << llvm::toString(options.takeError()) << '\n';
    return 1;
  }
  clang::tooling::ClangTool tool(options->getCompilations(), options->getSourcePathList());
  ExtractSummaryActionFactory factory;
  return tool.run(&factory);
}
} // namespace
} // namespace exa::extractor

int main(int argc, const char** argv)
{
  return exa::extractor::run(argc, argv);
}
