## Quick start

```
git submodule update --init --recursive # get toolsub submodule
make -C contrib                         # build toolsub submodule
make                                    # build the dbgcov tool
make -C test/hello hello.i              # builds hello.i.dbgcov
```

## Usage

Although `dbgcov` internals depend on analyzing Clang ASTs, you will actually
use GCC at the top level to analyse a source file together with `dbgcov`. This
is because `dbgcov` depends on GCC's `-wrapper` option to interpose into the
compilation process.

An example analysis of a C source file might look like:

```
gcc -std=c99 `$(DBGCOV_PATH)/bin/dbgcov-cflags` -save-temps -E -o hello.i hello.c
```

`dbgcov-cflags` inserts additional flags including `-wrapper` and others to
allow `dbgcov-tool` to analyse the source file. An additional
`input-path.dbgcov` file will be produced for every analysed source file.
See the `test` directory for a bit more usage info from the test files.

To integrate this analysis phase with the build process of an existing program,
it should be sufficient to add
```
`$(DBGCOV_PATH)/bin/dbgcov-cflags` -save-temps
```
to the compiler flags used.

For some build system and compiler combinations, the `-save-temps` flag is also
needed to ensure a preprocessing step occurs as separate external program
execution that can then be successfully redirected through `dbgcov-tool`.

## LLVM compatibility

The `dbgcov-tool` binary produced in the `src` directory makes use of Clang
libraries to analyse source ASTs. During development of this tool, LLVM 18 was
used and is the currently recommended version. This version includes an
[important preprocessor input
fix](https://github.com/llvm/llvm-project/commit/241cceb9af844ef7d7a87124407a04b0a64991fe).
Older LLVM versions should generally work as well, but you would need to
backport the preprocessor fix and possibly tweak LLVM libraries and defines
slightly.
