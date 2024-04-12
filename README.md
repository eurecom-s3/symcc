[![Compile and test SymCC](https://github.com/eurecom-s3/symcc/actions/workflows/run_tests.yml/badge.svg)](https://github.com/eurecom-s3/symcc/actions/workflows/run_tests.yml)

Note: The SymCC project is currently understaffed and therefore maintained in a 
best effort mode. In fact, we are hiring, in case you are interested to join 
the [S3 group at Eurecom](https://www.s3.eurecom.fr/) to work on this (and other
projects in the group) please [contact us](mailto:aurelien.francillon@eurecom.fr). 
We nevertheless appreciate PRs and apologize in advance for the slow processing 
of PRs, we will try to merge them when possible.

# SymCC: efficient compiler-based symbolic execution

SymCC is a compiler pass which embeds symbolic execution into the program
during compilation, and an associated run-time support library. In essence, the
compiler inserts code that computes symbolic expressions for each value in the
program. The actual computation happens through calls to the support library at
run time.

To build the pass and the support library, install LLVM (any version between 8
and 18) and Z3 (version 4.5 or later), as well as a C++ compiler with support
for C++17. LLVM lit is only needed to run the tests; if it's not packaged with
your LLVM, you can get it with `pip install lit`.

Under Ubuntu Groovy the following one liner should install all required
packages:

```
sudo apt install -y git cargo clang-14 cmake g++ git libz3-dev llvm-14-dev llvm-14-tools ninja-build python3-pip zlib1g-dev && sudo pip3 install lit
```

Alternatively, see below for using the provided Dockerfile, or the file
`util/quicktest.sh` for exact steps to perform under Ubuntu (or use with the
provided Vagrant file).

Make sure to pull the QSYM code:

```
$ git submodule init
$ git submodule update
```

Note that it is not necessary or recommended to build the QSYM submodule - our
build system will automatically extract the right source files and include them
in the build.

Create a build directory somewhere, and execute the following commands inside
it:

```
$ cmake -G Ninja -DQSYM_BACKEND=ON /path/to/compiler/sources
$ ninja check
```

If LLVM is installed in a non-standard location, add the CMake parameter
`-DLLVM_DIR=/path/to/llvm/cmake/module`. Similarly, you can point to a
non-standard Z3 installation with `-DZ3_DIR=/path/to/z3/cmake/module` (which
requires Z3 to be built with CMake).

The main build artifact from the user's point of view is `symcc`, a wrapper
script around clang that sets the right options to load our pass and link
against the run-time library. (See below for additional C++ support.)

To try the compiler, take some simple C code like the following:

``` c
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

int foo(int a, int b) {
    if (2 * a < b)
        return a;
    else if (a % b)
        return b;
    else
        return a + b;
}

int main(int argc, char* argv[]) {
    int x;
    if (read(STDIN_FILENO, &x, sizeof(x)) != sizeof(x)) {
        printf("Failed to read x\n");
        return -1;
    }
    printf("%d\n", foo(x, 7));
    return 0;
}
```

Save the code as `test.c`. To compile it with symbolic execution built in, we
call symcc as we would normally call clang:

```
$ ./symcc test.c -o test
```

Before starting the analysis, create a directory for the results and tell SymCC
about it:

```
$ mkdir results
$ export SYMCC_OUTPUT_DIR=`pwd`/results
```

Then run the program like any other binary, providing arbitrary input:

```
$ echo 'aaaa' | ./test
```

The program will execute the same computations as an uninstrumented version
would, but additionally the injected code will track computations symbolically
and attempt to compute diverging inputs at each branch point. All data that the
program reads from standard input is treated as symbolic; alternatively, you can
set the environment variable SYMCC_INPUT_FILE to the name of a file whose
contents will be treated as symbolic when read.

Note that due to how the QSYM backend is implemented, all input has to be available
from the start. In particular, when providing symbolic data on standard input
interactively, you need to terminate your input by pressing Ctrl+D before the
program starts to execute.

When execution is finished, the result directory will contain the new test cases
generated during program execution. Try running the program again on one of
those (or use [util/pure_concolic_execution.sh](util/pure_concolic_execution.sh)
to automate the process). For better results, combine SymCC with a fuzzer (see
[docs/Fuzzing.txt](docs/Fuzzing.txt)).


## Documentation

The directory [docs](docs) contains documentation on several internal aspects of
SymCC, as well as [building C++ code](docs/C++.txt), [compiling 32-bit binaries
on a 64-bit host](docs/32-bit.txt), and [running SymCC with a
fuzzer](docs/Fuzzing.txt). There is also a [list of all configuration
options](docs/Configuration.txt).

If you're interested in the research paper that we wrote about SymCC, have a
look at our group's
[website](http://www.s3.eurecom.fr/tools/symbolic_execution/symcc.html). It also
contains detailed instructions to replicate our experiments, as well as the raw
results that we obtained.

### Video demonstration
On YouTube you can find [a practical introduction to SymCC](https://www.youtube.com/watch?v=htDrNBiL7Y8) as well as a video on [how to combine AFL and SymCC](https://www.youtube.com/watch?v=zmC-ptp3W3k)

## Building a Docker image

If you prefer a Docker container over building SymCC natively, just tell Docker
to build the image after pulling the QSYM code as above. (Be warned though: the
Docker image enables optional C++ support from source, so creating
the image can take quite some time!)

```
$ git submodule init
$ git submodule update
$ docker build -t symcc .
$ docker run -it --rm symcc
```

This will build a Docker image and run an ephemeral container to try out SymCC.
Inside the container, `symcc` is available as a drop-in replacement for `clang`,
using the QSYM backend; similarly, `sym++` can be used instead of `clang++`. Now
try something like the following inside the container:

```
container$ cat sample.cpp
(Note that "root" is the input we're looking for.)
container$ sym++ -o sample sample.cpp
container$ echo test | ./sample
...
container$ cat /tmp/output/000008-optimistic
root
```

The Docker image also has AFL and `symcc_fuzzing_helper` preinstalled, so you
can use it to run SymCC with a fuzzer as described in [the
docs](docs/Fuzzing.txt). (The AFL binaries are located in `/afl`.)

While the Docker image is very convenient for _using_ SymCC, I recommend a local
build outside Docker for _development_. Docker will rebuild most of the image on
every change to SymCC (which is, in principle the right thing to do), whereas in
many cases it is sufficient to let the build system figure out what to rebuild
(and recompile, e.g., libc++ only when necessary).

## FAQ / BUGS / TODOs

### Why is SymCC only exploring one path and not all paths?

SymCC is currently a concolic executor it follows the concrete
path. In theory, it would be possible to make it a forking executor
see [issue #14](https://github.com/eurecom-s3/symcc/issues/14)

### Why does SymCC not generate some test cases?

There are multiple possible reasons:

#### QSym backend performs pruning

When built with the QSym backend exploration (e.g., loops) symcc is
subject to path pruning, this is part of the optimizations that makes
SymCC/QSym fast, it isn't sound. This is not a problem for using in
hybrid fuzzing, but this may be a problem for other uses. See for
example [issue #88](https://github.com/eurecom-s3/symcc/issues/88).

When building with the simple backend the paths should be found. If
the paths are not found with the simple backend this may be a bug (or
possibly a limitation of the simple backend).

#### Incomplete symbolic handing of functions, systems interactions.

The current symbolic understanding of libc is incomplete. So when an
unsupported libc function is called SymCC can't trace the computations
that happen in the function.

1. Adding the function to the [collection of wrapped libc
   functions](https://github.com/eurecom-s3/symcc/blob/master/runtime/LibcWrappers.cpp)
   and [register the
   wrapper](https://github.com/eurecom-s3/symcc/blob/b29dc4db2803830ebf50798e72b336473a567655/compiler/Runtime.cpp#L159)
   in the compiler.
2. Build a fully instrumented libc.
3. Cherry-pick individual libc functions from a libc implementation (e.g., musl) 

See [issue #23](https://github.com/eurecom-s3/symcc/issues/23) for more details.


### Rust support ?

This would be possible to support RUST, see [issue
#1](https://github.com/eurecom-s3/symcc/issues/1) for tracking this.

### Bug reporting

We appreciate bugs with test cases and steps to reproduce, PR with
corresponding test cases. SymCC is currently understaffed, we hope to
catch up and get back to active development at some point.

## Contact

Feel free to use GitHub issues and pull requests for improvements, bug reports,
etc. Alternatively, you can send an email to Sebastian Poeplau
(sebastian.poeplau@eurecom.fr) and Aurélien Francillon
(aurelien.francillon@eurecom.fr).


## Reference

To cite SymCC in scientific work, please use the following BibTeX:

``` bibtex
@inproceedings {poeplau2020symcc,
  author =       {Sebastian Poeplau and Aurélien Francillon},
  title =        {Symbolic execution with {SymCC}: Don't interpret, compile!},
  booktitle =    {29th {USENIX} Security Symposium ({USENIX} Security 20)},
  isbn =         {978-1-939133-17-5},
  pages =        {181--198},
  year =         2020,
  url =          {https://www.usenix.org/conference/usenixsecurity20/presentation/poeplau},
  publisher =    {{USENIX} Association},
  month =        aug,
}
```

More information on the paper is available
[here](http://www.s3.eurecom.fr/tools/symbolic_execution/symcc.html).


## Other projects using SymCC

[SymQEMU](https://github.com/eurecom-s3/symqemu) relies on SymCC.

LibAFL supports concolic execution with [SymCC](https://aflplus.plus/libafl-book/advanced_features/concolic/concolic.html), 
requires external patches (for now).

[AdaCore](https://www.adacore.com/) published [a paper describing](https://dl.acm.org/doi/10.1145/3631483.3631500) 
SymCC integration in GNATfuzz for test case generation and [plans to release this
as part of GNATfuzz beta release](https://docs.adacore.com/live/wave/roadmap/html/roadmap/roadmap_25_GNAT%20Pro.html#symbolic-execution-to-retrieve-input-values).

## License

SymCC is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

As an exception from the above, you can redistribute and/or modify the SymCC
runtime under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your
option) any later version. See #114 for the rationale.

SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License and the GNU
Lesser General Public License along with SymCC. If not, see
<https://www.gnu.org/licenses/>.

The following pieces of software have additional or alternate copyrights,
licenses, and/or restrictions:

| Program       | Directory                   |
|---------------|-----------------------------|
| QSYM          | `runtime/qsym_backend/qsym` |
| SymCC runtime | `runtime`                   |

