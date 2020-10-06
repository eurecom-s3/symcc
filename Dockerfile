# This file is part of SymCC.
#
# SymCC is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# SymCC. If not, see <https://www.gnu.org/licenses/>.

#
# The build stage
#
FROM ubuntu:20.04 AS builder

# Install dependencies
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y \
        cargo \
        clang-10 \
        cmake \
        g++ \
        git \
        libz3-dev \
        llvm-10-dev \
        llvm-10-tools \
        ninja-build \
        python2 \
        python3-pip \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
RUN pip3 install lit

# Build AFL.
RUN git clone -b v2.56b https://github.com/google/AFL.git afl \
    && cd afl \
    && make

# Download the LLVM sources already so that we don't need to get them again when
# SymCC changes
RUN git clone -b llvmorg-10.0.1 --depth 1 https://github.com/llvm/llvm-project.git /llvm_source

# Build a version of SymCC with the simple backend to compile libc++
COPY . /symcc_source
WORKDIR /symcc_build_simple
RUN cmake -G Ninja \
        -DQSYM_BACKEND=OFF \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DZ3_TRUST_SYSTEM_VERSION=on \
        /symcc_source \
    && ninja check

# Build SymCC with the Qsym backend
WORKDIR /symcc_build
RUN cmake -G Ninja \
        -DQSYM_BACKEND=ON \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DZ3_TRUST_SYSTEM_VERSION=on \
        /symcc_source \
    && ninja check \
    && cargo install --path /symcc_source/util/symcc_fuzzing_helper

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
FROM ubuntu:20.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        clang-10 \
        g++ \
        libllvm10 \
        zlib1g \
        sudo \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash ubuntu \
    && echo 'ubuntu ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/ubuntu

COPY --from=builder /symcc_build /symcc_build
COPY --from=builder /root/.cargo/bin/symcc_fuzzing_helper /symcc_build/
COPY util/pure_concolic_execution.sh /symcc_build/
COPY --from=builder /libcxx_symcc_install /libcxx_symcc_install
COPY --from=builder /afl /afl

ENV PATH /symcc_build:$PATH
ENV AFL_PATH /afl
ENV AFL_CC clang-10
ENV AFL_CXX clang++-10
ENV SYMCC_LIBCXX_PATH=/libcxx_symcc_install

USER ubuntu
WORKDIR /home/ubuntu
COPY sample.cpp /home/ubuntu/
RUN mkdir /tmp/output
