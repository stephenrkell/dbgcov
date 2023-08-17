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
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CommonOptionsParser.h" // TODO: remove
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
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
     The following comes from version 8 (StmtNodes.inc) and is my filter on "might do computation".
ArraySubscriptExpr
BinaryOperator      (no 'Expr')
CXXConstructExpr    (CXXTemporaryObjectExpr is a subclass)
CXXDefaultArgExpr
CXXFoldExpr         what is this?
CXXInheritedCtorInitExpr
CXXNewExpr
CXXPseudoDestructorExpr
CXXScalarValueInitExpr
CXXTypeidExpr
CallExpr
CXXMemberCallExpr
CastExpr            many subclasses, but even C-style cast in C++ might do pointer adjustment
ChooseExpr
FullExpr            what is this?
GenericSelectionExpr  what is this?
LambdaExpr
MaterializeTemporaryExpr  what is this?
OpaqueValueExpr           what is this?
PseudoObjectExpr          what is this?
UnaryOperator
VAArgExpr

     Let's use a macroised list to generate our visitors
   */
#define STMTS_TO_PRINT(v) \
  v(ArraySubscriptExpr) \
  v(BinaryOperator) \
  v(CXXConstructExpr) \
  v(CXXDefaultArgExpr) \
  v(CXXFoldExpr) \
  v(CXXInheritedCtorInitExpr) \
  v(CXXNewExpr) \
  v(CXXPseudoDestructorExpr) \
  v(CXXScalarValueInitExpr) \
  v(CXXTypeidExpr) \
  v(CallExpr) \
  v(CXXMemberCallExpr) \
  /* v(CastExpr) */ /* too many artificial instances of this */ \
  v(ChooseExpr) \
  v(FullExpr) \
  v(GenericSelectionExpr) \
  v(LambdaExpr) \
  v(MaterializeTemporaryExpr) \
  v(OpaqueValueExpr) \
  v(PseudoObjectExpr) \
  v(UnaryOperator) \
  v(VAArgExpr) \
  v(ReturnStmt)

#define VISITOR_METHOD_INNER(tok) \
    s->getSourceRange().getBegin().print(llvm::outs(), TheRewriter.getSourceMgr()); \
    llvm::outs() << "\t"; \
    s->getSourceRange().getEnd().print(llvm::outs(), TheRewriter.getSourceMgr()); \
    llvm::outs() << "\t"; \
    llvm::outs() << #tok; \
    llvm::outs() << "\n"; \
    return true; /* recurse */

#define VISITOR_METHOD(tok) \
  bool Visit ## tok (tok *s) { \
    VISITOR_METHOD_INNER(tok) \
  } /* end VisitExpr */

  STMTS_TO_PRINT(VISITOR_METHOD)

  // Some nodes require customised handling depending on the data they contain

  bool VisitVarDecl(VarDecl *s) {
    // VarDecl has computation only when it has an initialiser
    // TODO: Check C++ default initialisation cases
    if (!s->hasInit())
      return true;
    VISITOR_METHOD_INNER(VarDecl)
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
    unsigned count = 0;
    SourceLocation lastSourceLoc;
    //llvm::errs() << "== Saw top-level decl\n";
    for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
      // HACK: to get parent info, I have to do this, but I have no idea why.
      C.setTraversalScope({*b});
      // Traverse the declaration using our AST visitor.
      Visitor.TraverseDecl(*b);
      //(*b)->dump();
      ++count;
      lastSourceLoc = (*b)->getSourceRange().getEnd();
    }

    /* At some point, we reach the end of the file proper, and then start traversing
     * the template instantiations that are implied by various other stuff and which
     * clang has to elaborate in the AST for its own reasons. We have already
     * rewritten the source code, so if we go on processing them, we will double-
     * rewrite stuff which will be bad. How can we identify these instances? */
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
