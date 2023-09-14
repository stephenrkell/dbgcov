#include <sstream>
#include <string>
#ifdef USE_STD_UNIQUE_PTR
#include <memory>
#endif

#include "clang/AST/AST.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#ifdef HAVE_CLANG_AST_PARENTMAPCONTEXT_H
#include "clang/AST/ParentMapContext.h"
#endif
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CommonOptionsParser.h" // TODO: remove
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
#ifdef HAVE_DYNTYPEDNODE_IN_CLANG_NAMESPACE
using clang::DynTypedNode;
#else
using clang::ast_type_traits::DynTypedNode;
#endif
#ifdef USE_STD_UNIQUE_PTR
using std::make_unique;
#else
using llvm::make_unique;
#endif

void PrintNextLine(raw_ostream &stream, const SourceLocation &sLoc,
                   const SourceManager &mgr) {
  assert(sLoc.isFileID() && "Non-file location");
  PresumedLoc pLoc = mgr.getPresumedLoc(sLoc);
  assert(pLoc.isValid() && "Invalid location");
  stream << pLoc.getFilename() << ":" << pLoc.getLine() + 1 << ":" << 0;
}

void PrintExtendedName(raw_ostream &stream, const NamedDecl &decl,
                       const SourceManager &mgr) {
  // The precise name format here must match `debuginfo-quality` so we can match
  // data across both tools.
  // <function>, <variable>, decl <file>:<line>, unit <file>
  const auto *functionDecl = cast<FunctionDecl>(decl.getDeclContext());

  PresumedLoc declLoc = mgr.getPresumedLoc(decl.getLocation());
  assert(declLoc.isValid() && "Invalid decl location");

  // This attempts to match the translation unit file name emitted in DWARF
  // See `CGDebugInfo::createFile` for Clang's path handling approach
  const auto mainFileID = mgr.getMainFileID();
  const auto *mainFileEntry = mgr.getFileEntryForID(mainFileID);
  SmallString<128> currentDirectory;
  llvm::sys::fs::current_path(currentDirectory);
  const auto mainFilePath = mainFileEntry->getName();
  auto mainFileI = llvm::sys::path::begin(mainFilePath);
  const auto mainFileE = llvm::sys::path::end(mainFilePath);
  auto curDirI = llvm::sys::path::begin(currentDirectory);
  const auto curDirE = llvm::sys::path::end(currentDirectory);
  // Skip common path segments
  while (curDirI != curDirE && *curDirI == *mainFileI)
    ++curDirI, ++mainFileI;
  SmallString<128> relMainFile;
  while (mainFileI != mainFileE) {
    llvm::sys::path::append(relMainFile, *mainFileI);
    ++mainFileI;
  }

  stream << functionDecl->getDeclName() << ", " << decl.getDeclName()
         << ", decl " << llvm::sys::path::filename(declLoc.getFilename()) << ":"
         << declLoc.getLine() << ", unit " << relMainFile;
}

class DbgCovASTVisitor : public RecursiveASTVisitor<DbgCovASTVisitor> {
public:
  DbgCovASTVisitor(Rewriter &R, ASTContext &C) : TheRewriter(R), TheContext(C) {}

  /* What can we "visit" using RecursiveASTVisitor?
     - attributes or specific classes thereof
     - statements or specific classes thereof -- expressions are a kind of statement!
     - specific unary and binary operators
     - compound assignments
     - types
     - decls

     Of these, I *think* it is only expressions that contain code that executes at run time.
     So let's focus on those... what kinds of expression are there? Expr is the base.
     Problem: the taxonomy of statements/expressions is liable to change
     across clang versions.

     Let's use a macroised list to generate our visitors
   */
#define STMTS_TO_PRINT(v) \
  v(ArraySubscriptExpr) \
  v(CXXConstructExpr) \
  v(CXXDefaultArgExpr) \
  v(CXXFoldExpr) \
  v(CXXInheritedCtorInitExpr) \
  v(CXXNewExpr) \
  v(CXXPseudoDestructorExpr) \
  v(CXXScalarValueInitExpr) \
  v(CXXTypeidExpr) \
  /* v(CastExpr) */ /* too many artificial instances of this */ \
  v(ChooseExpr) \
  v(DeclRefExpr) /* may be too general, requires excluding constants */ \
  v(GenericSelectionExpr) \
  v(LambdaExpr) \
  v(MaterializeTemporaryExpr) \
  v(MemberExpr) \
  v(OpaqueValueExpr) \
  v(PseudoObjectExpr) \
  v(UnaryOperator) \
  v(VAArgExpr) \
  v(BreakStmt) \
  v(ContinueStmt) \
  v(GotoStmt) \
  v(ReturnStmt)

#define VISITOR_METHOD_PRINT(type, var) \
    var->getBeginLoc().print(llvm::outs(), TheRewriter.getSourceMgr()); \
    llvm::outs() << "\t"; \
    var->getEndLoc().print(llvm::outs(), TheRewriter.getSourceMgr()); \
    llvm::outs() << "\t"; \
    llvm::outs() << "Computation"; \
    llvm::outs() << "\t"; \
    llvm::outs() << #type; \
    llvm::outs() << "\n";

#define VISITOR_METHOD(type) \
  bool Visit ## type (type *s) { \
    VISITOR_METHOD_PRINT(type, s) \
    return true; \
  } /* end VisitExpr */

  STMTS_TO_PRINT(VISITOR_METHOD)

  // Some nodes have non-body subexpressions

  bool VisitDoStmt(DoStmt *s) {
    if (const auto *cond = s->getCond()) {
      VISITOR_METHOD_PRINT(DoStmt.Cond, cond)
    }
    return true;
  }

  bool VisitForStmt(ForStmt *s) {
    if (const auto *init = s->getInit()) {
      VISITOR_METHOD_PRINT(ForStmt.Init, init)
    }
    if (const auto *cond = s->getCond()) {
      VISITOR_METHOD_PRINT(ForStmt.Cond, cond)
    }
    if (const auto *inc = s->getInc()) {
      VISITOR_METHOD_PRINT(ForStmt.Inc, inc)
    }
    return true;
  }

  bool VisitIfStmt(IfStmt *s) {
    if (const auto *cond = s->getCond()) {
      VISITOR_METHOD_PRINT(IfStmt.Cond, cond)
    }
    return true;
  }

  bool VisitSwitchStmt(SwitchStmt *s) {
    if (const auto *cond = s->getCond()) {
      VISITOR_METHOD_PRINT(SwitchStmt.Cond, cond)
    }
    return true;
  }

  bool VisitWhileStmt(WhileStmt *s) {
    if (const auto *cond = s->getCond()) {
      VISITOR_METHOD_PRINT(WhileStmt.Cond, cond)
    }
    return true;
  }

  // Some nodes require customised handling depending on the data they contain

  bool VisitBinaryOperator(BinaryOperator *s) {
    VISITOR_METHOD_PRINT(BinaryOperator, s)

    if (!s->isAssignmentOp())
      return true;

    // Record variable definition region for assignment operations
    const auto *declRefExpr = cast<DeclRefExpr>(s->getLHS());
    const auto *namedDecl = cast<NamedDecl>(declRefExpr->getDecl());
    // llvm::errs() << "Assignment for `" << namedDecl->getDeclName() << "`\n";
    // s->dump();
    auto &parentMap = TheContext.getParentMapContext();
    auto *parentCompoundStmt = parentMap.getParents(*s)[0].get<CompoundStmt>();
    // Only record definitions within a continuing `CompoundStmt`
    if (!parentCompoundStmt)
      return true;
    // parentCompoundStmt->dump();

    // Debug info typically reflects variables as defined on the line _after_
    // assignment, so we print the next line here.
    PrintNextLine(llvm::outs(), s->getEndLoc(), TheRewriter.getSourceMgr());
    llvm::outs() << "\t";
    parentCompoundStmt->getEndLoc().print(llvm::outs(), TheRewriter.getSourceMgr());
    llvm::outs() << "\t"
                 << "MustBeDefined"
                 << "\t";
    PrintExtendedName(llvm::outs(), *namedDecl, TheRewriter.getSourceMgr());
    llvm::outs() << "\n";

    return true;
  }

  bool VisitCallExpr(CallExpr *s) {
    if (const auto *callee = s->getCallee()) {
      VISITOR_METHOD_PRINT(CallExpr.Callee, callee)
    }
    // Arguments shouldn't be added at this level, as they may have a whole tree
    // of multi-line computation, so we instead inspect them further by
    // recursion
    return true;
  }

  bool TraverseConstantExpr(ConstantExpr *s) {
    // Skip constant expressions (e.g. case statements)
    return true;
  }

  bool VisitFunctionDecl(FunctionDecl *s) {
    // We want to mark the opening and closing braces as having computation
    // Debug info associates the function prologue / epilogue with these lines
    if (const auto *body = dyn_cast_if_present<CompoundStmt>(s->getBody())) {
      // Prologue
      body->getBeginLoc().print(llvm::outs(), TheRewriter.getSourceMgr());
      llvm::outs() << "\t";
      body->getBeginLoc().print(llvm::outs(), TheRewriter.getSourceMgr());
      llvm::outs() << "\t";
      llvm::outs() << "Computation";
      llvm::outs() << "\t";
      llvm::outs() << "FunctionDecl.Prologue";
      llvm::outs() << "\n";

      // Epilogue
      body->getEndLoc().print(llvm::outs(), TheRewriter.getSourceMgr());
      llvm::outs() << "\t";
      body->getEndLoc().print(llvm::outs(), TheRewriter.getSourceMgr());
      llvm::outs() << "\t";
      llvm::outs() << "Computation";
      llvm::outs() << "\t";
      llvm::outs() << "FunctionDecl.Epilogue";
      llvm::outs() << "\n";

      // Record parameter definition region
      for (const auto *param : s->parameters()) {
        // Debug info typically reflects parameters as defined starting on the
        // line with the opening brace of the function body.
        body->getBeginLoc().print(llvm::outs(), TheRewriter.getSourceMgr());
        llvm::outs() << "\t";
        body->getEndLoc().print(llvm::outs(), TheRewriter.getSourceMgr());
        llvm::outs() << "\t"
                     << "MustBeDefined"
                     << "\t";
        PrintExtendedName(llvm::outs(), *param, TheRewriter.getSourceMgr());
        llvm::outs() << "\n";
      }
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *s) {
    // `VarDecl` has computation only for locals with an initialiser
    // TODO: Check C++ default initialisation cases
    if (!s->isLocalVarDecl() || !s->hasInit())
      return true;
    VISITOR_METHOD_PRINT(VarDecl, s)

    // Record variable definition region
    // llvm::errs() << "VD for `" << s->getName() << "`\n"
    //              << "  parentStmt:\n";
    auto &parentMap = TheContext.getParentMapContext();
    auto *parentDeclStmt = parentMap.getParents(*s)[0].get<DeclStmt>();
    // `VarDecl` with initialiser should be child of `DeclStmt`
    assert(parentDeclStmt && "VarDecl not child of DeclStmt");
    auto *parentStmt = parentMap.getParents(*parentDeclStmt)[0].get<Stmt>();
    // Record definitions within any kind of `Stmt`, such as a continuing
    // `CompoundStmt` or blocks with associated declarations (e.g. `ForStmt`)
    if (!parentStmt)
      return true;
    // parentStmt->dump();

    // Debug info typically reflects variables as defined on the line _after_
    // assignment, so we print the next line here.
    PrintNextLine(llvm::outs(), s->getEndLoc(), TheRewriter.getSourceMgr());
    llvm::outs() << "\t";
    parentStmt->getEndLoc().print(llvm::outs(), TheRewriter.getSourceMgr());
    llvm::outs() << "\t"
                 << "MustBeDefined"
                 << "\t";
    PrintExtendedName(llvm::outs(), *s, TheRewriter.getSourceMgr());
    llvm::outs() << "\n";

    return true;
  }

private:
  Rewriter &TheRewriter;
  ASTContext &TheContext;
};

// Implementation of the ASTConsumer interface for reading an AST produced
// by the Clang parser.
class DbgCovConsumer : public ASTConsumer {
public:
  DbgCovConsumer(Rewriter &R, ASTContext &C) : Visitor(R, C), R(R), C(C)
  {}
  // Override the method that gets called for each parsed top-level
  // declaration.
  bool HandleTopLevelDecl(DeclGroupRef DR) override {
    //llvm::errs() << "== Saw top-level decl\n";
    for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
      // HACK: to get parent info, I have to do this, but I have no idea why.
      C.setTraversalScope({*b});
      //(*b)->dump();

      // Traverse the declaration using our AST visitor
      Visitor.TraverseDecl(*b);
    }
    return true;
  }

private:
  DbgCovASTVisitor Visitor;
  Rewriter &R;
  ASTContext &C;
};

// For each source file provided to the tool, a new FrontendAction is created.
class MyFrontendAction : public ASTFrontendAction {
public:
  MyFrontendAction() {}
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef file) override {
    //llvm::errs() << "== Creating AST consumer for: " << file << "\n";
    TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return make_unique<DbgCovConsumer>(TheRewriter, CI.getASTContext());
  }

private:
  Rewriter TheRewriter;
};

/* Clang wants a "compilations database" and a "source path list".
 * We want to mimic the gcc command-line interface; since so (mostly)
 * does clang, we should be able to get what we want from libclang
 * code. How does it parse its argc and argv? Where is the clang
 * main(), even? It's in clang/tools/driver.
 * HACK: for now, don't mimic gcc's command line. Just use the
 * LLVM common options format, and let our wrapper script adapt.
 * (But see Attic/options.cpp for a partial attempt at the original.)
 */
static llvm::cl::OptionCategory DbgCovCategory("DbgCov");

int main(int argc, const char **argv) {
  /* How do we use an ordinary (gcc-like) compiler command line
   * to drive a clang tool?
   *
   * The CommonOptionsParser seems to want our command-line to
   * be structured as
   * <arguments in the cl syntax> -- <compiler options>.
   * Indeed if we want the tool to do anything, we need to pass '--' on
   * the compiler command line.
   *
   * For now we just rip off cilpp to create a new driver program that
   * execs the tool (i.e. us) with a clang-tool-style command line.  */
  // build a CompilationsDatabase from argv
  /* Some time between LLVM 8 and LLVM 13 the constructor we were using
   * has become protected and it wants us to call ::create() instead. */
#ifdef HAVE_COMMONOPTIONSPARSER_CREATE
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, DbgCovCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();
#else
  CommonOptionsParser OptionsParser(argc, argv, DbgCovCategory);
#endif
  //llvm::errs() << "source paths size is " << OptionsParser.getSourcePathList().size() << "\n";
  auto &Compilations = OptionsParser.getCompilations();
  auto &SourcePaths = OptionsParser.getSourcePathList();
  ClangTool Tool(Compilations, SourcePaths);

  // ClangTool::run accepts a FrontendActionFactory, which is then used to
  // create new objects implementing the FrontendAction interface. Here we use
  // the helper newFrontendActionFactory to create a default factory that will
  // return a new MyFrontendAction object every time.
  // To further customize this, we could create our own factory class.
  std::unique_ptr<FrontendActionFactory> ActionFactory
   = newFrontendActionFactory<MyFrontendAction>();
  return Tool.run(ActionFactory.get());
}
