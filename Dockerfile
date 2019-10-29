#
# The build stage
#
FROM ubuntu:18.04 AS builder

# Install dependencies
RUN apt-get update && apt-get install -y \
    clang-8 \
    cmake \
    g++ \
    git \
    llvm-8-dev \
    llvm-8-tools \
    ninja-build \
    python3-pip \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
RUN pip3 install lit

# Build Z3; the version in the Ubuntu repositories is too old for Qsym.
RUN git clone -b z3-4.8.6 https://github.com/Z3Prover/z3.git \
    && cd z3 \
    && python scripts/mk_make.py \
    && cd build \
    && make -j `nproc` \
    && make install

# Download the LLVM sources for libc++
ARG BUILD_LIBCXX
RUN if [ ! -z ${BUILD_LIBCXX+x} ]; then \
      git clone -b llvmorg-8.0.1 https://github.com/llvm/llvm-project.git /llvm_source; \
    fi

# Build a version of SymCC with the simple backend to compile libc++
COPY . /symcc_source
WORKDIR /symcc_build_simple
RUN if [ ! -z ${BUILD_LIBCXX+x} ]; then \
      cmake -G Ninja -DQSYM_BACKEND=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo /symcc_source \
      && ninja check; \
    fi

# Build libc++ with SymCC
WORKDIR /libcxx_symcc
RUN if [ ! -z ${BUILD_LIBCXX+x} ]; then \
      export SYMCC_REGULAR_LIBCXX=yes SYMCC_NO_SYMBOLIC_INPUT=yes \
      && mkdir /libcxx_symcc_build \
      && cd /libcxx_symcc_build \
      && cmake -G Ninja /llvm_source/llvm \
        -DLLVM_ENABLE_PROJECTS="libcxx;libcxxabi" \
        -DLLVM_TARGETS_TO_BUILD="X86" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=/symcc_build_simple/symcc \
        -DCMAKE_CXX_COMPILER=/symcc_build_simple/sym++ \
      && ninja \
      && cp lib/libc++*.so* /libcxx_symcc \
      && mkdir -p /libcxx_symcc/include/c++ \
      && cp -r include/c++/v1 /libcxx_symcc/include/c++ ; \
    fi

# Build SymCC
WORKDIR /symcc_build
RUN cmake -G Ninja -DQSYM_BACKEND=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo /symcc_source \
    && ninja check

#
# The final image
#
FROM ubuntu:18.04

RUN apt-get update && apt-get install -y \
    clang-8 \
    libllvm8 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /symcc_build /symcc_build
COPY --from=builder /libcxx_symcc /libcxx_symcc
COPY --from=builder /usr/lib/libz3.so /usr/lib

ENV PATH /symcc_build:$PATH
ENV SYMCC_LIBCXX_PATH=/libcxx_symcc
WORKDIR /root
