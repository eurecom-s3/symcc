# Contributing to SymCC

We encourage everyone to contribute improvements and bug fixes to SymCC. Our
preferred way of accepting contributions is via GitHub pull requests. Please be
sure to run clang-format on any C/C++ code you change; an easy way to do so is
with `git clang-format --style LLVM` just before committing. (On Ubuntu, you can
get `git-clang-format` via `apt install clang-format`.) Ideally, also add a test
to your patch (see the
[docs](https://github.com/eurecom-s3/symcc/blob/master/docs/Testing.txt) for
details). Unfortunately, since the project is a bit short on developers at the
moment, we have to ask for your patience while we review your PR.

Please note that any contributions you make are licensed under the same terms as
the code you're contributing to, as per the GitHub Terms of Service, [section
D.6](https://docs.github.com/en/site-policy/github-terms/github-terms-of-service#6-contributions-under-repository-license).
At the time of writing, this means LGPL (version 3 or later) for the SymCC
runtime, and GPL (version 3 or later) for the rest of SymCC.
