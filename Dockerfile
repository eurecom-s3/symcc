FROM ubuntu:18.04

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
    && make install \
    && cd ../.. \
    && rm -rf z3

# Build SymCC
COPY . /symcc_source
WORKDIR /symcc_build
RUN cmake -G Ninja -DQSYM_BACKEND=ON /symcc_source \
    && ninja check

ENV PATH /symcc_build:$PATH
WORKDIR /root
