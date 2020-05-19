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
    && mkdir z3/build \
    && cd z3/build \
    && cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. \
    && ninja \
    && ninja install

# Download the LLVM sources already so that we don't need to get them again when
# SymCC changes
RUN git clone -b llvmorg-8.0.1 --depth 1 https://github.com/llvm/llvm-project.git /llvm_source

# Build a version of SymCC with the simple backend to compile libc++
COPY . /symcc_source
WORKDIR /symcc_build_simple
RUN cmake -G Ninja -DQSYM_BACKEND=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo /symcc_source \
    && ninja check

# Build SymCC with the Qsym backend
WORKDIR /symcc_build
RUN cmake -G Ninja -DQSYM_BACKEND=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo /symcc_source \
    && ninja check

# Build libc++ with SymCC using the simple backend
WORKDIR /libcxx_symcc
RUN export SYMCC_REGULAR_LIBCXX=yes SYMCC_NO_SYMBOLIC_INPUT=yes \
    && mkdir /libcxx_symcc_build \
    && cd /libcxx_symcc_build \
    && cmake -G Ninja /llvm_source/llvm \
         -DLLVM_ENABLE_PROJECTS="libcxx;libcxxabi" \
         -DLLVM_TARGETS_TO_BUILD="X86" \
         -DLLVM_DISTRIBUTION_COMPONENTS="cxx;cxxabi;cxx-headers" \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_PREFIX=/libcxx_symcc_install \
         -DCMAKE_C_COMPILER=/symcc_build_simple/symcc \
         -DCMAKE_CXX_COMPILER=/symcc_build_simple/sym++ \
    && ninja distribution \
    && ninja install-distribution

#
# The final image
#
FROM ubuntu:18.04

RUN apt-get update && apt-get install -y \
      clang-8 \
      libllvm8 \
      zlib1g \
      sudo \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash ubuntu \
    && echo 'ubuntu ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/ubuntu

COPY --from=builder /symcc_build /symcc_build
COPY --from=builder /libcxx_symcc_install /libcxx_symcc_install
COPY --from=builder /usr/local/lib/libz3.so* /usr/local/lib/

ENV PATH /symcc_build:$PATH
ENV SYMCC_LIBCXX_PATH=/libcxx_symcc_install

USER ubuntu
WORKDIR /home/ubuntu
COPY sample.cpp /home/ubuntu/
RUN mkdir /tmp/output
