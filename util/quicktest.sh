#!/bin/sh

# This script is to make a quick test of SymCC with Qsym backend, it
# is supposed to work on ubuntu groovy, e.g., after running:

# vagrant init ubuntu/groovy64
# vagrant up
# vagrant ssh

# exit when any command fails
set -e

if ! uname -a  | grep ubuntu-groovy>> /dev/null ; then
    echo "** Warning: Script known to work for ubuntu groovy only **"
fi

# update system
sudo apt-get update
sudo apt-get upgrade -y

# install requirements
sudo apt-get install -y git cargo clang-10 cmake g++ git libz3-dev llvm-10-dev llvm-10-tools ninja-build python3-pip zlib1g-dev
sudo pip3 install lit

# Clone project 
git clone https://github.com/eurecom-s3/symcc.git
cd symcc

# init/update submodules 
git submodule update --init --recursive

#  build
mkdir ../symcc-build
cd ../symcc-build
cmake -G Ninja -DSYMCC_RT_BACKEND=qsym  -DZ3_TRUST_SYSTEM_VERSION=on ../symcc
ninja check

# create a test case 
cat  > test.c  << 'EOF'
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

EOF

# test it 
./symcc test.c -o test.out
mkdir -p results
export SYMCC_OUTPUT_DIR=`pwd`/results
echo 'aaaa' | ./test.out
cat ${SYMCC_OUTPUT_DIR}/000000 | ./test.out

# TODO: this is not a very precise regression test, generated testcase
# may be incorrect, but binding to a specific test case may be too
# narrow (fail if test isn't exactly the expected result, but a
# different valid one), this should be improved.
if [ -f ${SYMCC_OUTPUT_DIR}/000001 ]; then 
    echo "SUCCESS: looks like this build of  SymCC is working, type vagrant ssh to interact with it !"
fi
