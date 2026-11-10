#include "exception_flow/extractor/extractor.hpp"
#include "exception_flow/summary/summary.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/StmtCXX.h>
#include <clang/Basic/ExceptionSpecificationType.h>
#include <clang/Basic/Linkage.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/SSAFOptions.h>
#include <clang/ScalableStaticAnalysis/Analyses/EntityPointerLevel/EntityPointerLevel.h>
#include <clang/ScalableStaticAnalysis/Core/TUSummary/TUSummaryBuilder.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/FileSystem.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace exa::extractor
{
using summary::Callee;
using summary::Catch;
using summary::CatchIndex;
using summary::ClassDefinition;
using summary::DirectCallee;
using summary::EntityDefinition;
using summary::EntityId;
using summary::ExceptionFlowSummary;
using summary::ExceptionSpecification;
using summary::ExceptionType;
using summary::FunctionBorder;
using summary::FunctionDefinition;
using summary::IndirectCallee;
using summary::LanguageLinkage;
using summary::NounwindCause;
using summary::SourceLocation;
using summary::UnmappedCallee;
using summary::VirtualCallee;
using summary::VirtualMethod;

namespace
{
inline constexpr bool dependentSpecIsNothrow          = false;
inline constexpr bool destructorsAreNoexceptByDefault = true;

enum class Dispatch
{
  direct,
  virtualDispatch,
};

std::string workingDirectory()
{
  llvm::SmallString<256> path;
  if (const auto error = llvm::sys::fs::current_path(path))
  {
    llvm::reportFatalUsageError("cannot get the current working directory: "
                                + llvm::Twine(error.message()));
  }
  return path.str().str();
}

class EntityDescriber
{
  public:
    EntityDescriber(clang::ASTContext& ast [[clang::lifetimebound]],
                    clang::ssaf::TUSummaryExtractor& extractor [[clang::lifetimebound]])
        : _ast(ast),
          _extractor(extractor),
          _policy(ast.getPrintingPolicy()),
          _workingDirectory(workingDirectory())
    {
      _policy.SuppressTagKeyword = true;
    }

    clang::ASTContext& ast() const
    {
      return _ast;
    }

    clang::ssaf::EntityPointerLevelSet pointerLevels(const clang::Expr* expression) const
    {
      auto pointers = clang::ssaf::translateEntityPointerLevel(expression, _ast, _extractor);
      if (!pointers)
      {
        llvm::consumeError(pointers.takeError());
        return {};
      }
      return std::move(*pointers);
    }

    std::optional<EntityId> entity(const clang::NamedDecl* decl) const
    {
      return _extractor.addEntity(decl);
    }

    std::optional<EntityId> recordEntity(const clang::CXXRecordDecl* record) const
    {
      const auto* definition = record->getDefinition();
      return entity(definition != nullptr ? definition : record);
    }

    std::string qualifiedName(const clang::NamedDecl* decl) const
    {
      std::string result;
      llvm::raw_string_ostream stream(result);
      decl->printQualifiedName(stream, _policy);
      return result;
    }

    std::optional<SourceLocation> sourceLocation(clang::SourceLocation location) const
    {
      const auto& sources = _ast.getSourceManager();
      const auto presumed = sources.getPresumedLoc(sources.getExpansionLoc(location));
      if (presumed.isInvalid())
      {
        return std::nullopt;
      }
      return SourceLocation{relativeToWorkingDirectory(presumed.getFilename()),
                            presumed.getLine(),
                            presumed.getColumn()};
    }

    EntityDefinition entityDefinition(const clang::NamedDecl* decl) const
    {
      return {qualifiedName(decl),
              sourceLocation(decl->getLocation()),
              _ast.getSourceManager().isInSystemHeader(decl->getLocation())};
    }

    ExceptionType exceptionType(clang::QualType type) const
    {
      const clang::QualType canonical = type.getCanonicalType().getUnqualifiedType();
      const auto* pointer             = canonical->getAs<clang::PointerType>();
      const clang::QualType object    = pointer != nullptr ? pointer->getPointeeType()
                                                           : canonical;
      ExceptionType result;
      result.name      = canonical.getAsString(_policy);
      result.isPointer = pointer != nullptr;
      if (const auto* record = object->getAsCXXRecordDecl())
      {
        result.record = recordEntity(record);
      }
      return result;
    }

    FunctionBorder functionTypeBorder(clang::QualType type) const
    {
      const auto specification = exceptionSpecification(type);
      if (specification == ExceptionSpecification::nonThrowing)
      {
        return {specification, NounwindCause::exceptionSpecification, std::nullopt};
      }
      return {specification, std::nullopt, std::nullopt};
    }

    FunctionBorder declaredBorder(const clang::FunctionDecl* function) const
    {
      auto border = isUnevaluatedDestructorAssumedNothrow(function)
                      ? FunctionBorder{ExceptionSpecification::nonThrowing,
                                       NounwindCause::exceptionSpecification,
                                       std::nullopt}
                      : functionTypeBorder(function->getType());
      if (!border.nounwindCause && function->hasAttr<clang::NoThrowAttr>())
      {
        border.nounwindCause = NounwindCause::nothrowAttribute;
      }
      border.languageLinkage = languageLinkage(function->getLanguageLinkage());
      return border;
    }

    FunctionBorder definitionBorder(const clang::FunctionDecl* function) const
    {
      auto border        = declaredBorder(function);
      const auto& language = _ast.getLangOpts();
      if (!language.Exceptions)
      {
        border.nounwindCause = NounwindCause::exceptionsDisabled;
        return border;
      }
      if (border.nounwindCause == NounwindCause::exceptionSpecification
          && !language.CXXExceptions)
      {
        border.nounwindCause = NounwindCause::cxxExceptionsDisabled;
      }
      return border;
    }

  private:
    static std::optional<LanguageLinkage> languageLinkage(clang::LanguageLinkage linkage)
    {
      switch (linkage)
      {
      case clang::CLanguageLinkage:
        return LanguageLinkage::c;
      case clang::CXXLanguageLinkage:
        return LanguageLinkage::cxx;
      case clang::NoLanguageLinkage:
        return std::nullopt;
      }
      llvm_unreachable("unknown clang::LanguageLinkage");
    }

    std::optional<ExceptionSpecification> exceptionSpecification(clang::QualType type) const
    {
      if (!_ast.getLangOpts().CPlusPlus)
      {
        return std::nullopt;
      }
      const auto* prototype = type->getAs<clang::FunctionProtoType>();
      if (prototype == nullptr || hasUnevaluatedSpec(prototype)
          || !prototype->isNothrow(dependentSpecIsNothrow))
      {
        return ExceptionSpecification::potentiallyThrowing;
      }
      return ExceptionSpecification::nonThrowing;
    }

    static bool hasUnevaluatedSpec(const clang::FunctionProtoType* prototype)
    {
      return clang::isUnresolvedExceptionSpec(prototype->getExceptionSpecType());
    }

    static bool
    isUnevaluatedDestructorAssumedNothrow(const clang::FunctionDecl* function)
    {
      const auto* prototype = function->getType()->getAs<clang::FunctionProtoType>();
      return llvm::isa<clang::CXXDestructorDecl>(function) && prototype != nullptr
          && hasUnevaluatedSpec(prototype) && destructorsAreNoexceptByDefault;
    }

    std::string relativeToWorkingDirectory(llvm::StringRef file) const
    {
      llvm::StringRef relative = file;
      if (relative.consume_front(_workingDirectory) && relative.consume_front("/"))
      {
        return relative.str();
      }
      return file.str();
    }

    clang::ASTContext& _ast;
    clang::ssaf::TUSummaryExtractor& _extractor;
    clang::PrintingPolicy _policy;
    std::string _workingDirectory;
};

class FunctionBodyWalker : public clang::RecursiveASTVisitor<FunctionBodyWalker>
{
  public:
    FunctionBodyWalker(const EntityDescriber& describer [[clang::lifetimebound]],
                       FunctionDefinition& function [[clang::lifetimebound]])
        : _describer(describer),
          _function(function)
    {}

    bool shouldVisitTemplateInstantiations() const
    {
      return true;
    }

    bool shouldVisitImplicitCode() const
    {
      return true;
    }

    bool shouldVisitLambdaBody() const
    {
      return false;
    }

    bool TraverseCXXRecordDecl(clang::CXXRecordDecl*)
    {
      return true;
    }

    void walkConstructorInitializers(const clang::FunctionDecl* function)
    {
      if (const auto* constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(function))
      {
        for (const auto* initializer : constructor->inits())
        {
          TraverseStmt(initializer->getInit());
        }
      }
    }

    bool TraverseCXXTryStmt(clang::CXXTryStmt* statement)
    {
      const auto catches = recordCatches(statement);
      _tryScopes.push_back(catches);
      TraverseStmt(statement->getTryBlock());
      _tryScopes.pop_back();
      for (unsigned index = 0; index < statement->getNumHandlers(); ++index)
      {
        _handlerScopes.push_back(catches.at(index));
        TraverseStmt(statement->getHandler(index)->getHandlerBlock());
        _handlerScopes.pop_back();
      }
      return true;
    }

    bool VisitCXXThrowExpr(clang::CXXThrowExpr* expression)
    {
      const auto sourceLocation = _describer.sourceLocation(expression->getThrowLoc());
      if (const auto* operand = expression->getSubExpr())
      {
        _function.throws.push_back(
            {_describer.exceptionType(
                 _describer.ast().getExceptionObjectType(operand->getType())),
             handlersInScope(),
             sourceLocation});
        return true;
      }
      _function.rethrows.push_back({handlersInScope(), enclosingHandler(), sourceLocation});
      return true;
    }

    bool VisitCallExpr(clang::CallExpr* expression)
    {
      if (expression->isTypeDependent() || expression->isValueDependent())
      {
        return true;
      }
      if (const auto* callee = expression->getDirectCallee())
      {
        recordCall(callee, expression->getBeginLoc(), dispatchOf(expression, callee));
        return true;
      }
      if (const auto* calleeExpression = expression->getCallee();
          calleeExpression != nullptr && !isPseudoDestructor(calleeExpression))
      {
        recordIndirectCall(calleeExpression, expression->getBeginLoc());
      }
      return true;
    }

    bool VisitCXXConstructExpr(clang::CXXConstructExpr* expression)
    {
      const auto* constructor = expression->getConstructor();
      if (constructor != nullptr && !constructor->isTrivial())
      {
        recordCall(constructor,
                   expression->getBeginLoc(),
                   Dispatch::direct);
      }
      return true;
    }

    bool VisitCXXNewExpr(clang::CXXNewExpr* expression)
    {
      if (const auto* allocation = expression->getOperatorNew())
      {
        recordCall(allocation,
                   expression->getBeginLoc(),
                   Dispatch::direct);
      }
      return true;
    }

    bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* expression)
    {
      recordDestructionOf(expression->getDestroyedType(), expression->getBeginLoc());
      return true;
    }

    bool VisitCXXBindTemporaryExpr(clang::CXXBindTemporaryExpr* expression)
    {
      if (const auto* destructor = expression->getTemporary()->getDestructor())
      {
        recordDestructorCall(destructor, expression->getBeginLoc());
      }
      return true;
    }

    bool VisitVarDecl(clang::VarDecl* variable)
    {
      const bool destroyedAtScopeExit = variable->hasLocalStorage()
                                     && !llvm::isa<clang::ParmVarDecl>(variable);
      if (destroyedAtScopeExit)
      {
        recordDestructionOf(variable->getType(), variable->getLocation());
      }
      return true;
    }

  private:
    std::vector<CatchIndex> recordCatches(const clang::CXXTryStmt* statement)
    {
      std::vector<CatchIndex> catches;
      for (unsigned index = 0; index < statement->getNumHandlers(); ++index)
      {
        const auto* handler = statement->getHandler(index);
        Catch catchFact;
        catchFact.sourceLocation = _describer.sourceLocation(handler->getCatchLoc());
        if (handler->getExceptionDecl() != nullptr)
        {
          catchFact.caughtType = _describer.exceptionType(
              handler->getCaughtType().getNonReferenceType().getUnqualifiedType());
        }
        catches.push_back(_function.catches.size());
        _function.catches.push_back(std::move(catchFact));
      }
      return catches;
    }

    std::vector<CatchIndex> handlersInScope() const
    {
      std::vector<CatchIndex> handlers;
      for (auto scope = _tryScopes.rbegin(); scope != _tryScopes.rend(); ++scope)
      {
        handlers.insert(handlers.end(), scope->begin(), scope->end());
      }
      return handlers;
    }

    std::optional<CatchIndex> enclosingHandler() const
    {
      if (_handlerScopes.empty())
      {
        return std::nullopt;
      }
      return _handlerScopes.back();
    }

    static bool isPseudoDestructor(const clang::Expr* callee)
    {
      return llvm::isa<clang::CXXPseudoDestructorExpr>(callee->IgnoreParenImpCasts());
    }

    static bool isQualifiedMemberCall(const clang::CallExpr* expression)
    {
      const auto* member = llvm::dyn_cast<clang::MemberExpr>(
          expression->getCallee()->IgnoreParenImpCasts());
      return member != nullptr && member->hasQualifier();
    }

    static bool cannotBeOverridden(const clang::CXXMethodDecl* method)
    {
      return method->hasAttr<clang::FinalAttr>()
          || method->getParent()->isEffectivelyFinal();
    }

    static Dispatch dispatchOf(const clang::CallExpr* expression,
                               const clang::FunctionDecl* callee)
    {
      const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
      if (method == nullptr || !method->isVirtual() || isQualifiedMemberCall(expression)
          || cannotBeOverridden(method))
      {
        return Dispatch::direct;
      }
      return Dispatch::virtualDispatch;
    }

    static clang::QualType calledFunctionType(clang::QualType calleeType)
    {
      if (const auto* pointer = calleeType->getAs<clang::PointerType>())
      {
        return pointer->getPointeeType();
      }
      if (const auto* member = calleeType->getAs<clang::MemberPointerType>())
      {
        return member->getPointeeType();
      }
      if (const auto* reference = calleeType->getAs<clang::ReferenceType>())
      {
        return reference->getPointeeType();
      }
      return calleeType;
    }

    std::optional<SourceLocation> callSourceLocation(clang::SourceLocation location) const
    {
      return _describer.sourceLocation(_implicitCallSite.value_or(location));
    }

    void addCall(Callee callee, clang::SourceLocation location)
    {
      _function.calls.push_back({std::move(callee),
                                 handlersInScope(),
                                 enclosingHandler(),
                                 callSourceLocation(location)});
    }

    void recordIndirectCall(const clang::Expr* callee, clang::SourceLocation location)
    {
      const auto functionType = calledFunctionType(callee->getType());
      addCall(IndirectCallee{functionType.getAsString(),
                             _describer.functionTypeBorder(functionType),
                             _describer.pointerLevels(callee)},
              location);
    }

    void recordDestructionOf(clang::QualType type, clang::SourceLocation location)
    {
      const auto* element = type->getBaseElementTypeUnsafe();
      const auto* record = element != nullptr ? element->getAsCXXRecordDecl() : nullptr;
      if (record == nullptr || !record->hasDefinition())
      {
        return;
      }
      if (const auto* destructor = record->getDestructor())
      {
        recordDestructorCall(destructor, location);
      }
    }

    void recordDestructorCall(const clang::CXXDestructorDecl* destructor,
                              clang::SourceLocation location)
    {
      if (destructor->isTrivial())
      {
        return;
      }
      recordCall(destructor,
                 location,
                 destructor->isVirtual() ? Dispatch::virtualDispatch : Dispatch::direct);
    }

    void flattenImplicitFunction(const clang::FunctionDecl* callee,
                                 clang::SourceLocation location)
    {
      const clang::FunctionDecl* definition = nullptr;
      const bool hasGeneratedBody           = callee->hasBody(definition);
      if (!hasGeneratedBody || !_flattening.insert(definition).second)
      {
        recordUnmappedCall(callee, location);
        return;
      }
      const auto enclosingImplicitCallSite = _implicitCallSite;
      _implicitCallSite                    = _implicitCallSite.value_or(location);
      walkConstructorInitializers(definition);
      TraverseStmt(definition->getBody());
      _implicitCallSite = enclosingImplicitCallSite;
      _flattening.erase(definition);
    }

    void recordUnmappedCall(const clang::FunctionDecl* callee,
                            clang::SourceLocation location)
    {
      auto border = _describer.declaredBorder(callee);
      if (border.nounwindCause)
      {
        return;
      }
      addCall(UnmappedCallee{_describer.qualifiedName(callee), border}, location);
    }

    static const clang::FunctionDecl* explicitRedeclaration(const clang::FunctionDecl* function)
    {
      for (const auto* redeclaration : function->redecls())
      {
        if (!redeclaration->isImplicit())
        {
          return redeclaration;
        }
      }
      return nullptr;
    }

    void recordCall(const clang::FunctionDecl* callee,
                    clang::SourceLocation location,
                    Dispatch dispatch)
    {
      const bool isCompilerBuiltin = callee->getBuiltinID() != 0;
      if (isCompilerBuiltin)
      {
        return;
      }
      if (callee->isImplicit())
      {
        const auto* explicitDeclaration = explicitRedeclaration(callee);
        if (explicitDeclaration == nullptr)
        {
          flattenImplicitFunction(callee, location);
          return;
        }
        callee = explicitDeclaration;
      }
      auto name             = _describer.qualifiedName(callee);
      auto border         = _describer.declaredBorder(callee);
      const auto id         = _describer.entity(callee);
      if (!id)
      {
        addCall(UnmappedCallee{std::move(name), border}, location);
        return;
      }
      switch (dispatch)
      {
      case Dispatch::direct:
        addCall(DirectCallee{*id, std::move(name), border}, location);
        return;
      case Dispatch::virtualDispatch:
        addCall(VirtualCallee{*id, std::move(name), border}, location);
        return;
      }
    }

    const EntityDescriber& _describer;
    FunctionDefinition& _function;
    std::vector<std::vector<CatchIndex>> _tryScopes;
    std::vector<CatchIndex> _handlerScopes;
    std::optional<clang::SourceLocation> _implicitCallSite;
    llvm::DenseSet<const clang::FunctionDecl*> _flattening;
};

class DefinitionCollector : public clang::RecursiveASTVisitor<DefinitionCollector>
{
  public:
    DefinitionCollector(const EntityDescriber& describer [[clang::lifetimebound]],
                        std::string compilationUnit,
                        EntityFacts& facts [[clang::lifetimebound]])
        : _describer(describer),
          _compilationUnit(std::move(compilationUnit)),
          _facts(facts)
    {}

    bool shouldVisitTemplateInstantiations() const
    {
      return true;
    }

    bool shouldVisitImplicitCode() const
    {
      return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl* function)
    {
      if (isUninstantiatedTemplate(function) || function->isImplicit())
      {
        return true;
      }
      if (function->doesThisDeclarationHaveABody())
      {
        recordFunctionDefinition(function);
      }
      return true;
    }

    bool VisitLambdaExpr(clang::LambdaExpr* lambda)
    {
      auto* callOperator = lambda->getCallOperator();
      if (callOperator != nullptr && !isUninstantiatedTemplate(callOperator)
          && callOperator->doesThisDeclarationHaveABody())
      {
        recordFunctionDefinition(callOperator);
      }
      return true;
    }

    bool VisitCXXRecordDecl(clang::CXXRecordDecl* record)
    {
      if (!record->isThisDeclarationADefinition() || record->isDependentContext()
          || record->isLambda() || !mattersForExceptionFlow(record))
      {
        return true;
      }
      const auto id = _describer.entity(record);
      if (!id || !_recordedClasses.insert(record).second)
      {
        return true;
      }
      ClassDefinition definition{_describer.entityDefinition(record)};
      for (const auto& base : record->bases())
      {
        if (const auto* baseRecord = base.getType()->getAsCXXRecordDecl())
        {
          if (auto baseId = _describer.recordEntity(baseRecord))
          {
            definition.bases.insert(*baseId);
          }
        }
      }
      for (const auto* method : record->methods())
      {
        if (method->isVirtual() && !method->isImplicit())
        {
          if (auto virtualMethod = describeVirtualMethod(method))
          {
            definition.virtualMethods.push_back(std::move(*virtualMethod));
          }
        }
      }
      _facts.emplace(*id, std::move(definition));
      return true;
    }

  private:
    static bool isUninstantiatedTemplate(const clang::FunctionDecl* function)
    {
      return function->isDependentContext();
    }

    static bool mattersForExceptionFlow(const clang::CXXRecordDecl* record)
    {
      const bool canBeCaughtAsBase       = record->getNumBases() > 0;
      const bool canDispatchVirtualCalls = record->isPolymorphic();
      return canBeCaughtAsBase || canDispatchVirtualCalls;
    }

    std::optional<VirtualMethod>
    describeVirtualMethod(const clang::CXXMethodDecl* method) const
    {
      auto id = _describer.entity(method);
      if (!id)
      {
        return std::nullopt;
      }
      VirtualMethod virtualMethod{*id,
                                  _describer.qualifiedName(method),
                                  _describer.declaredBorder(method),
                                  method->isPureVirtual(),
                                  {}};
      for (const auto* overridden : method->overridden_methods())
      {
        if (auto overriddenId = _describer.entity(overridden))
        {
          virtualMethod.overriddenMethods.insert(*overriddenId);
        }
      }
      return virtualMethod;
    }

    void recordFunctionDefinition(clang::FunctionDecl* function)
    {
      if (!_recordedFunctions.insert(function).second)
      {
        return;
      }
      const auto id = _describer.entity(function);
      if (!id)
      {
        return;
      }
      FunctionDefinition definition{_describer.entityDefinition(function)};
      definition.compilationUnit = _compilationUnit;
      definition.border          = _describer.definitionBorder(function);
      definition.isEntryPoint = function->isMain();

      FunctionBodyWalker walker(_describer, definition);
      walker.walkConstructorInitializers(function);
      walker.TraverseStmt(function->getBody());
      _facts.emplace(*id, std::move(definition));
    }

    const EntityDescriber& _describer;
    std::string _compilationUnit;
    EntityFacts& _facts;
    llvm::DenseSet<const clang::FunctionDecl*> _recordedFunctions;
    llvm::DenseSet<const clang::CXXRecordDecl*> _recordedClasses;
};
} // namespace

EntityFacts collect(clang::ASTContext& context, clang::ssaf::TUSummaryExtractor& entities)
{
  EntityFacts facts;
  EntityDescriber describer(context, entities);
  DefinitionCollector collector(describer, entities.getOptions().CompilationUnitId, facts);
  collector.TraverseDecl(context.getTranslationUnitDecl());
  return facts;
}

void ExceptionFlowExtractor::HandleTranslationUnit(clang::ASTContext& context)
{
  for (auto& [id, facts] : collect(context, *this))
  {
    SummaryBuilder.addSummary(id, std::make_unique<ExceptionFlowSummary>(std::move(facts)));
  }
}
} // namespace exa::extractor
