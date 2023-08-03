THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))

-include ../config.mk
TOOLSUB ?= $(dir $(realpath $(THIS_MAKEFILE)))/../contrib/toolsub

.PHONY: default
default: dbgcov dbgcov-tool

CXX_OBJS := $(patsubst %.cpp,%.o,$(wildcard *.cpp))

dbgcov-tool: LDLIBS += \
        -lclangAST \
        -lclangASTMatchers \
        -lclangAnalysis \
        -lclangBasic \
        -lclangDriver \
        -lclangEdit \
        -lclangFrontend \
        -lclangFrontendTool \
        -lclangLex \
        -lclangParse \
        -lclangSema \
        -lclangSupport \
        -lclangEdit \
        -lclangRewrite \
        -lclangRewriteFrontend \
        -lclangStaticAnalyzerFrontend \
        -lclangStaticAnalyzerCheckers \
        -lclangStaticAnalyzerCore \
        -lclangCrossTU \
        -lclangIndex \
        -lclangSerialization \
        -lclangToolingCore \
        -lclangTooling \
        -lclangFormat

LLVM_CONFIG ?= llvm-config-8

#$(CXX_OBJS): CXX := clang
$(CXX_OBJS): CXXFLAGS += -std=c++14 -fPIC -g `$(LLVM_CONFIG) --cxxflags` -UNDEBUG

dbgcov-tool: LDFLAGS += `$(LLVM_CONFIG) --ldflags`
dbgcov-tool: LDLIBS += `$(LLVM_CONFIG) --libs`
dbgcov-tool: main.o
	$(CXX) -o $@ $+ $(LDFLAGS) $(LDLIBS)

OCAMLOPTFLAGS += -fPIC
CFLAGS += -fPIC

OCAMLFIND ?= ocamlfind
OCAMLFLAGS +=  -package findlib -package dynlink
OCAMLFLAGS += -I $(dir $(THIS_MAKEFILE))/lib
OCAMLFLAGS += -I $(TOOLSUB)/wrapper/lib
vpath %.cmx $(TOOLSUB)/wrapper/lib
vpath %.o $(TOOLSUB)/wrapper/lib

OCAMLLIBS := str.cmxa nums.cmxa unix.cmxa

dbgcov: compiler_args.cmx dbgcov.ml mkstemp_stubs.o
	$(OCAMLFIND) ocamlopt -o "$@" $(OCAMLOPTFLAGS) $(OCAMLFLAGS) $(OCAMLLIBS) $+ -linkpkg -linkall

%.cmxs: %.cmx
	$(OCAMLFIND) ocamlopt -shared -o "$@" $(OCAMLOPTFLAGS) $(OCAMLFLAGS) $+
%.cmx %.cmi: %.ml
	$(OCAMLFIND) ocamlopt -o "$@" $(OCAMLOPTFLAGS) $(OCAMLFLAGS) -c "$<"
%.cmo %.cmi: %.ml
	$(OCAMLFIND) ocamlc -o "$@" $(OCAMLFLAGS) -c "$<"
%.cma: %.cmo
	$(OCAMLFIND) ocamlc -o "$@" $(OCAMLFLAGS) -a $+

clean:
	rm -f *.o *.cmxa *.cmx *.cmo *.cmxs *.cmi
	rm -f dbgcov dbgcov-tool
