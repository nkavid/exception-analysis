#include "exception_flow/extractor/test/translation_unit.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/ScalableStaticAnalysis/Core/Model/BuildNamespace.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/TargetParser/Triple.h>

namespace exa::extractor::test
{
namespace
{
clang::ssaf::SSAFOptions options()
{
  clang::ssaf::SSAFOptions result;
  result.CompilationUnitId = mainFile.str();
  return result;
}

std::unique_ptr<clang::ASTUnit> parse(llvm::StringRef code)
{
  auto ast = clang::tooling::buildASTFromCodeWithArgs(
      code,
      {"-std=c++20"},
      mainFile,
      "exa-extractor-test",
      std::make_shared<clang::PCHContainerOperations>(),
      clang::tooling::getClangStripDependencyFileAdjuster(),
      {},
      nullptr,
      llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>());
  if (ast == nullptr || ast->getDiagnostics().hasErrorOccurred())
  {
    llvm::reportFatalUsageError("test code does not compile");
  }
  return ast;
}

class EntityNames : public clang::RecursiveASTVisitor<EntityNames>
{
  public:
    EntityNames(clang::ssaf::TUSummaryExtractor& entities [[clang::lifetimebound]],
                std::map<summary::EntityId, std::string>& names [[clang::lifetimebound]])
        : _entities(entities),
          _names(names)
    {}

    bool VisitNamedDecl(clang::NamedDecl* decl)
    {
      if (decl->isImplicit())
      {
        return true;
      }
      if (const auto id = _entities.addEntity(decl))
      {
        _names.emplace(*id, decl->getQualifiedNameAsString());
      }
      return true;
    }

  private:
    clang::ssaf::TUSummaryExtractor& _entities;
    std::map<summary::EntityId, std::string>& _names;
};
} // namespace

TranslationUnit::TranslationUnit(llvm::StringRef code)
    : _options(options()),
      _summary(llvm::Triple(),
               clang::ssaf::BuildNamespace(clang::ssaf::BuildNamespaceKind::CompilationUnit,
                                           mainFile)),
      _builder(_summary, _options),
      _entities(_builder),
      _ast(parse(code)),
      _facts(collect(_ast->getASTContext(), _entities))
{
  EntityNames(_entities, _names).TraverseDecl(_ast->getASTContext().getTranslationUnitDecl());
}

const EntityFacts& TranslationUnit::facts() const
{
  return _facts;
}

summary::EntityId TranslationUnit::entity(llvm::StringRef name)
{
  auto& context = _ast->getASTContext();
  const auto declarations = context.getTranslationUnitDecl()->lookup(&context.Idents.get(name));
  if (!declarations.isSingleResult())
  {
    llvm::reportFatalUsageError("no single declaration named " + llvm::Twine(name));
  }
  const auto id = _entities.addEntity(declarations.front());
  if (!id)
  {
    llvm::reportFatalUsageError("no SSAF entity for " + llvm::Twine(name));
  }
  return *id;
}

const std::string& TranslationUnit::name(summary::EntityId id) const
{
  const auto found = _names.find(id);
  if (found == _names.end())
  {
    llvm::reportFatalUsageError("entity without a declaration name");
  }
  return found->second;
}
} // namespace exa::extractor::test
