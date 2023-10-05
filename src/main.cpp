#include <cstring>
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
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
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
using namespace llvm;
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

  // Utilities

  PresumedLoc GetPresumedLocation(const SourceLocation &sLoc) {
    const auto &mgr = TheRewriter.getSourceMgr();
    assert(sLoc.isFileID() && "Non-file location");
    PresumedLoc pLoc = mgr.getPresumedLoc(sLoc);
    assert(pLoc.isValid() && "Invalid location");
    return pLoc;
  }

  std::string GetExtendedName(const NamedDecl &decl) {
    const auto &mgr = TheRewriter.getSourceMgr();
    // TODO: Support C++ `BlockDecl`s
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
    // Remove file extension to avoid `.i` vs. `.c` match failures
    llvm::sys::path::replace_extension(relMainFile, "");

    // The precise name format here must match `debuginfo-quality` so we can match
    // data across both tools.
    // <function>, <variable>, decl <file>:<line>, unit <file>
    std::string name;
    raw_string_ostream stream(name);
    stream << functionDecl->getDeclName() << ", " << decl.getDeclName()
           << ", decl " << llvm::sys::path::filename(declLoc.getFilename())
           << ":" << declLoc.getLine() << ", unit " << relMainFile;
    return name;
  }

  void PrintLocation(raw_ostream &stream, const PresumedLoc &loc) {
    SmallString<128> filePath(loc.getFilename());
    std::error_code error = llvm::sys::fs::make_absolute(filePath);
    assert(!error && "Unable to make absolute path");
    stream << filePath << ":" << loc.getLine() << ":"
           << loc.getColumn();
  }

  void PrintRegion(raw_ostream &stream, const SourceLocation &begin,
                   const SourceLocation &end, const Twine &kind,
                   const Twine &detail, bool beginNextLine) {
    const auto &beginLoc = GetPresumedLocation(begin);
    const auto &endLoc = GetPresumedLocation(end);
    if (strcmp(beginLoc.getFilename(), endLoc.getFilename())) {
      llvm::errs() << "Warning: Ignoring multi-file region\n";
      PrintLocation(llvm::errs(), beginLoc);
      llvm::errs() << "\n";
      PrintLocation(llvm::errs(), endLoc);
      llvm::errs() << "\n" << kind << "\n" << detail << "\n";
      return;
    }
    if (beginLoc.getLine() > endLoc.getLine()) {
      llvm::errs() << "Error: Invalid region (begin after end)\n";
      PrintLocation(llvm::errs(), beginLoc);
      llvm::errs() << "\n";
      PrintLocation(llvm::errs(), endLoc);
      llvm::errs() << "\n" << kind << "\n" << detail << "\n";
    }
    assert(beginLoc.getLine() <= endLoc.getLine());
    // Ensure that we don't move the begin line past the end line for
    // single-line regions (e.g. macro invocations)
    if (beginLoc.getLine() == endLoc.getLine())
      beginNextLine = false;
    if (beginNextLine) {
      PrintLocation(stream,
                    PresumedLoc(beginLoc.getFilename(), beginLoc.getFileID(),
                                beginLoc.getLine() + 1, 0,
                                beginLoc.getIncludeLoc()));
    } else {
      PrintLocation(stream, beginLoc);
    }
    stream << "\t";
    PrintLocation(stream, endLoc);
    stream << "\t" << kind << "\t" << detail << "\n";
  }

  template <typename NodeT> const Stmt *GetParentStmt(const NodeT *expr) {
    const Stmt *parentStmt = nullptr;

    // Look for the nearest `Stmt` ancestor
    auto &parentMap = TheContext.getParentMapContext();
    auto parents = parentMap.getParents(*expr);
    while (!parents.empty()) {
      parentStmt = parents[0].template get<Stmt>();
      // C is statement-oriented, so all `Expr`s are also `Stmt`s
      // In this case, we only want `Stmt`s that are _not_ `Expr`s
      if (parentStmt && !isa<Expr>(parentStmt))
        break;
      parents = parentMap.getParents(parents[0]);
    }

    return parentStmt;
  }

  void ReportDeclRefExprAsDefined(const DeclRefExpr *declRefExpr,
                                  const Stmt *stmtForRegionStart,
                                  const Twine &regionKind,
                                  bool beginNextLine) {
    const auto *namedDecl = cast<NamedDecl>(declRefExpr->getDecl());
    // Only examine variables inside functions
    if (!isa<FunctionDecl>(namedDecl->getDeclContext()))
      return;
    // llvm::errs() << "Report for `" << namedDecl->getDeclName() << "`\n";
    // s->dump();

    const auto *parentStmt = GetParentStmt(stmtForRegionStart);
    // Only examine variables within some kind of `Stmt`, such as a continuing
    // `CompoundStmt` or blocks with associated declarations (e.g. `ForStmt`)
    if (!parentStmt)
      return;
    // parentStmt->dump();

    PrintRegion(llvm::outs(), stmtForRegionStart->getEndLoc(),
                parentStmt->getEndLoc(), regionKind,
                GetExtendedName(*namedDecl), beginNextLine);
  }

  void ReportTreeAsDefined(const Expr *tree, const Stmt *stmtForRegionStart,
                           const Twine &regionKind, bool beginNextLine) {
    SmallVector<const Stmt *, 8> workQueue;
    // Find all `DeclRefExpr`s within `tree`
    workQueue.push_back(tree);
    while (!workQueue.empty()) {
      const auto *node = workQueue.back();
      workQueue.pop_back();
      // Add any children to the work queue
      for (const auto *child : node->children())
        workQueue.push_back(child);
      // Report `DeclRefExpr` if found
      if (!isa<DeclRefExpr>(node))
        continue;
      const auto *declRefExpr = cast<DeclRefExpr>(node);
      ReportDeclRefExprAsDefined(declRefExpr, stmtForRegionStart, regionKind,
                                 beginNextLine);
    }
  }

  // Nodes with standardised handling

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

#define VISITOR_METHOD_PRINT(type, var)                                        \
  PrintRegion(llvm::outs(), var->getBeginLoc(), var->getEndLoc(),              \
              "Computation", #type, /* beginNextLine = */ false);

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

    // Consider right-hand side variables as likely to be defined
    // as of the current line
    ReportTreeAsDefined(s->getRHS(), s, "MayBeDefined",
                        /* beginNextLine = */ false);

    // Record variable definition region for assignment operations
    // on the next line _after_ assignment
    ReportTreeAsDefined(s->getLHS(), s, "MustBeDefined",
                        /* beginNextLine = */ true);

    return true;
  }

  bool VisitCallExpr(CallExpr *s) {
    if (const auto *callee = s->getCallee()) {
      VISITOR_METHOD_PRINT(CallExpr.Callee, callee)
    }

    // Arguments shouldn't be examined for computation at this level, as they
    // may have a whole tree of multi-line computation, so we instead inspect
    // them further by recursion
    // llvm::errs() << "Call:\n";
    // s->dump();

    // Mark variables used in call arguments as "may be defined" after it
    // llvm::errs() << "Arguments: " << s->getNumArgs() << "\n";
    for (const Expr *argument : s->arguments()) {
      // llvm::errs() << "Argument:\n";
      // argument->dump();
      // Descend to `DeclRefExpr` within the argument
      while (argument && !isa<DeclRefExpr>(argument)) {
        // Check whether there are any children
        if (argument->child_begin() == argument->child_end())
          break;
        // Only check the first child for simplicity (seems fine for most cases)
        const Stmt *child = *argument->child_begin();
        // llvm::errs() << "Argument child:\n";
        // child->dump();
        // Ignore non-`Expr` children
        if (!isa<Expr>(child))
          break;
        argument = cast<Expr>(child);
      }
      if (!argument || !isa<DeclRefExpr>(argument))
        continue;
      const auto *declRefExpr = cast<DeclRefExpr>(argument);
      // When referencing existing variables like this, assume storage may
      // already exist on the current line.
      ReportDeclRefExprAsDefined(declRefExpr, s, "MayBeDefined",
                                 /* beginNextLine = */ false);
    }

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
      PrintRegion(llvm::outs(), body->getBeginLoc(), body->getBeginLoc(),
                  "Computation", "FunctionDecl.Prologue",
                  /* beginNextLine = */ false);

      // Epilogue
      PrintRegion(llvm::outs(), body->getEndLoc(), body->getEndLoc(),
                  "Computation", "FunctionDecl.Epilogue",
                  /* beginNextLine = */ false);

      for (const auto *param : s->parameters()) {
        // Record parameter declaration scope
        // Matches the definition region below, which uses the function body.
        PrintRegion(llvm::outs(), body->getBeginLoc(), body->getEndLoc(),
                    "DeclScope", GetExtendedName(*param),
                    /* beginNextLine = */ false);

        // Record parameter definition region
        // Debug info typically reflects parameters as defined starting on the
        // line with the opening brace of the function body.
        PrintRegion(llvm::outs(), body->getBeginLoc(), body->getEndLoc(),
                    "MustBeDefined", GetExtendedName(*param),
                    /* beginNextLine = */ false);
      }
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *s) {
    // Only examine local variables inside functions
    if (!isa<FunctionDecl>(s->getDeclContext()) || !s->isLocalVarDecl())
      return true;

    auto &parentMap = TheContext.getParentMapContext();
    auto *parentDeclStmt = parentMap.getParents(*s)[0].get<DeclStmt>();
    // `VarDecl` should be child of `DeclStmt`
    assert(parentDeclStmt && "VarDecl not child of DeclStmt");
    const auto *parentStmt = GetParentStmt(parentDeclStmt);
    // Only examine variables within some kind of `Stmt`, such as a continuing
    // `CompoundStmt` or blocks with associated declarations (e.g. `ForStmt`)
    if (!parentStmt)
      return true;

    // Record variable declaration scope
    // Treats the entire enclosing block as potential scope
    // This allows for e.g. storage on the stack to match the whole block
    // Note: We currently filter away stack coverage via the definition filter
    PrintRegion(llvm::outs(), parentStmt->getBeginLoc(),
                parentStmt->getEndLoc(), "DeclScope", GetExtendedName(*s),
                /* beginNextLine = */ false);

    // `VarDecl` has computation only for
    //   - automatic locals with an initialiser
    //   - static locals
    // TODO: Check C++ default initialisation cases
    if (!s->isStaticLocal() && !s->hasInit())
      return true;
    VISITOR_METHOD_PRINT(VarDecl, s)

    // Record variable definition region
    // Debug info typically reflects variables as defined on the line _after_
    // assignment, so we print the next line here.
    PrintRegion(llvm::outs(), s->getEndLoc(), parentStmt->getEndLoc(),
                "MustBeDefined", GetExtendedName(*s),
                /* beginNextLine = */ true);

    // Consider initialiser variables as likely to be defined
    // as of the current line
    if (s->hasInit())
      ReportTreeAsDefined(s->getInit(), parentDeclStmt, "MayBeDefined",
                          /* beginNextLine = */ false);

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
