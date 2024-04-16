## Quick start

git submodule update --init --recursive # get toolsub submodule
make -C contrib                         # build toolsub submodule
make                                    # build the dbgcov tool
make -C test/hello hello.i              # builds hello.i.dbgcov

## LLVM compatibility

The `dbgcov-tool` binary produced in the `src` directory makes use of Clang
libraries to analyse source ASTs. During development of this tool, LLVM 18 was
used and is the currently recommended version. This version includes an
important preprocessor input fix
(https://github.com/llvm/llvm-project/commit/241cceb9af844ef7d7a87124407a04b0a64991fe).
Older LLVM versions should generally work as well, but you would need to
backport the preprocessor fix and possibly tweak LLVM libraries and defines
slightly.
