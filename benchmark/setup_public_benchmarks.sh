#!/usr/bin/env bash
#
# Download and setup public benchmark suites for SymCC MPI benchmarking.
#
# Usage:
#   ./setup_public_benchmarks.sh [--all | --lava-m | --unibench | --symcc-paper | --fuzzbench]
#
# Each benchmark is downloaded into benchmark/public/<name>/
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PUBLIC_DIR="$SCRIPT_DIR/public"
mkdir -p "$PUBLIC_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

############################################################
# 1a. LAVA  (bug injection tool + target programs)
#     Source: https://github.com/panda-re/lava
#     Targets: file, jq, grep, pcre2, duktape, libyaml, etc.
#     Contains target_bins/ with source tarballs
############################################################
setup_lava() {
    local dest="$PUBLIC_DIR/lava"
    if [ -d "$dest" ] && [ -d "$dest/target_bins" ]; then
        info "LAVA already downloaded at $dest"
        return
    fi

    info "Downloading LAVA (panda-re/lava)..."
    cd "$PUBLIC_DIR"

    git clone --depth 1 https://github.com/panda-re/lava.git 2>/dev/null || {
        warn "Git clone failed, trying archive download..."
        curl -sL https://github.com/panda-re/lava/archive/refs/heads/master.tar.gz | tar xz
        mv lava-master lava
    }

    info "LAVA setup complete at $dest"
    echo "  Target programs (source tarballs in target_bins/):"
    ls "$dest/target_bins/"*.tar.gz 2>/dev/null | while read f; do echo "    $(basename "$f")"; done
    echo "  To build: ./compile_public_benchmarks.sh --lava"
}

############################################################
# 1b. LAVA-M  (coreutils with injected bugs)
#     Source: https://github.com/moyix/lava-m-corpus
#     4 programs: base64, md5sum, uniq, who
#     Each has a seed input + known bug count
############################################################
setup_lava_m() {
    local dest="$PUBLIC_DIR/lava-m"
    if [ -d "$dest/lava_corpus" ]; then
        info "LAVA-M already downloaded at $dest"
        return
    fi

    info "Downloading LAVA-M benchmark..."
    mkdir -p "$dest"
    cd "$dest"

    # Clone the LAVA-M corpus
    git clone --depth 1 https://github.com/moyix/lava-m-corpus.git lava_corpus 2>/dev/null || {
        warn "Git clone failed, trying archive download..."
        curl -sL https://github.com/moyix/lava-m-corpus/archive/refs/heads/main.tar.gz | tar xz
        mv lava-m-corpus-main lava_corpus
    }

    # Create build script
    cat > build_with_symcc.sh << 'BUILDEOF'
#!/bin/bash
# Build LAVA-M targets with SymCC
# Usage: CC=symcc ./build_with_symcc.sh
set -euo pipefail

CC="${CC:-symcc}"
CFLAGS="${CFLAGS:--O2}"

for prog in base64 md5sum uniq who; do
    src_dir="lava_corpus/LAVA-M/$prog/coreutils-8.24-lava-safe"
    if [ ! -d "$src_dir" ]; then
        echo "Source not found: $src_dir"
        continue
    fi

    echo "Building $prog with $CC..."
    cd "$src_dir"
    if [ ! -f configure ]; then
        echo "  Running bootstrap..."
        ./bootstrap 2>/dev/null || true
    fi
    CC="$CC" CFLAGS="$CFLAGS" ./configure --quiet 2>/dev/null || true
    make -j"$(nproc)" -C src "$prog" 2>/dev/null || echo "  WARNING: build may have errors"
    cd -

    if [ -f "$src_dir/src/$prog" ]; then
        cp "$src_dir/src/$prog" "./${prog}_symcc"
        echo "  -> ${prog}_symcc"
    fi
done
BUILDEOF
    chmod +x build_with_symcc.sh

    # Create seeds directory
    mkdir -p seeds
    for prog in base64 md5sum uniq who; do
        seed_dir="lava_corpus/LAVA-M/$prog/fuzzer_input"
        if [ -d "$seed_dir" ]; then
            mkdir -p "seeds/$prog"
            cp "$seed_dir"/* "seeds/$prog/" 2>/dev/null || true
            echo "  Seeds for $prog: $(ls "seeds/$prog" | wc -l) files"
        fi
    done

    info "LAVA-M setup complete at $dest"
    echo "  Programs: base64, md5sum, uniq, who"
    echo "  To build: cd $dest && CC=symcc ./build_with_symcc.sh"
}

############################################################
# 2. UniBench  (20 real-world programs)
#    Source: https://github.com/unifuzz/unibench
#    Covers: image, audio, video, text, binary, network
############################################################
setup_unibench() {
    local dest="$PUBLIC_DIR/unibench"
    if [ -d "$dest/unibench" ]; then
        info "UniBench already downloaded at $dest"
        return
    fi

    info "Downloading UniBench benchmark..."
    mkdir -p "$dest"
    cd "$dest"

    git clone --depth 1 https://github.com/unifuzz/unibench.git 2>/dev/null || {
        warn "Git clone failed, trying archive..."
        curl -sL https://github.com/unifuzz/unibench/archive/refs/heads/master.tar.gz | tar xz
        mv unibench-master unibench
    }

    # Create a guide for SymCC compilation
    cat > README_symcc.txt << 'EOF'
UniBench - 20 Real-World Programs for Fuzzer Benchmarking
=========================================================

Programs included (grouped by type):

Image processing:
  - exiv2 (EXIF/IPTC metadata)
  - imginfo (image info from libtiff)
  - jhead (JPEG header manipulation)
  - tiffsplit (TIFF splitting)

Audio/Video:
  - ffmpeg (multimedia processing)
  - flvmeta (FLV metadata editor)
  - mp3gain (MP3 volume normalization)
  - mp42aac (MP4 AAC extraction)
  - wav2swf (WAV to SWF conversion)

Text/Document:
  - pdftotext (PDF text extraction)
  - mujs (JavaScript interpreter)
  - sqlite3 (database engine)

Binary:
  - nm (ELF symbol table)
  - objdump (object file disassembly)
  - size (object file size)

Network:
  - tcpdump (packet analysis)

Other:
  - cflow (C call graph)
  - infotocap (terminfo conversion)
  - lame (MP3 encoder)

To compile with SymCC:
  export CC=symcc CXX=sym++
  # Then follow each program's build instructions in unibench/

Recommended programs for quick SymCC MPI benchmark:
  - exiv2, jhead, tiffsplit (smaller, faster builds)
  - mujs (good path diversity)
  - tcpdump (used in SymCC paper)
EOF

    info "UniBench setup complete at $dest"
    echo "  See $dest/README_symcc.txt for details"
}

############################################################
# 3. SymCC Paper Programs (OpenJPEG, libarchive, tcpdump)
#    The exact programs used in the SymCC USENIX paper
############################################################
setup_symcc_paper() {
    local dest="$PUBLIC_DIR/symcc-paper"
    if [ -d "$dest" ] && [ -f "$dest/.setup_done" ]; then
        info "SymCC paper benchmarks already set up at $dest"
        return
    fi

    info "Setting up SymCC paper benchmark programs..."
    mkdir -p "$dest"
    cd "$dest"

    # OpenJPEG
    if [ ! -d "openjpeg" ]; then
        info "  Cloning OpenJPEG..."
        git clone --depth 50 https://github.com/uclouvain/openjpeg.git 2>/dev/null || true
        if [ -d "openjpeg" ]; then
            cd openjpeg && git checkout 1f1e9682 2>/dev/null || true && cd ..
        fi
    fi

    # libarchive
    if [ ! -d "libarchive" ]; then
        info "  Cloning libarchive..."
        git clone --depth 50 https://github.com/libarchive/libarchive.git 2>/dev/null || true
        if [ -d "libarchive" ]; then
            cd libarchive && git checkout 9ebb2484 2>/dev/null || true && cd ..
        fi
    fi

    # tcpdump + libpcap
    if [ ! -d "tcpdump" ]; then
        info "  Cloning tcpdump..."
        git clone --depth 50 https://github.com/the-tcpdump-group/tcpdump.git 2>/dev/null || true
        if [ -d "tcpdump" ]; then
            cd tcpdump && git checkout d57927e1 2>/dev/null || true && cd ..
        fi
    fi
    if [ ! -d "libpcap" ]; then
        info "  Cloning libpcap..."
        git clone --depth 50 https://github.com/the-tcpdump-group/libpcap.git 2>/dev/null || true
        if [ -d "libpcap" ]; then
            cd libpcap && git checkout d615abec 2>/dev/null || true && cd ..
        fi
    fi

    # Create build script
    cat > build_all.sh << 'BUILDEOF'
#!/bin/bash
# Build all SymCC paper benchmark programs
# Usage: CC=symcc CXX=sym++ ./build_all.sh
set -euo pipefail

CC="${CC:-symcc}"
CXX="${CXX:-sym++}"
NPROC="$(nproc)"

echo "=== Building with CC=$CC CXX=$CXX ==="

# OpenJPEG
if [ -d openjpeg ]; then
    echo "Building OpenJPEG..."
    cd openjpeg
    mkdir -p build && cd build
    CC="$CC" CXX="$CXX" cmake .. \
        -DBUILD_THIRDPARTY=ON \
        -DCMAKE_BUILD_TYPE=Release 2>/dev/null
    make -j"$NPROC" 2>/dev/null
    cd ../..
    echo "  -> openjpeg/build/bin/opj_decompress"
fi

# libarchive
if [ -d libarchive ]; then
    echo "Building libarchive..."
    cd libarchive
    mkdir -p build && cd build
    CC="$CC" CXX="$CXX" cmake .. \
        -DCMAKE_BUILD_TYPE=Release 2>/dev/null
    make -j"$NPROC" 2>/dev/null
    cd ../..
    echo "  -> libarchive/build/bin/bsdtar"
fi

# libpcap + tcpdump
if [ -d libpcap ] && [ -d tcpdump ]; then
    echo "Building libpcap..."
    cd libpcap
    CC="$CC" ./configure --quiet 2>/dev/null
    make -j"$NPROC" 2>/dev/null
    cd ..

    echo "Building tcpdump..."
    cd tcpdump
    CC="$CC" ./configure --quiet 2>/dev/null
    make -j"$NPROC" 2>/dev/null
    cd ..
    echo "  -> tcpdump/tcpdump"
fi

echo "=== Build complete ==="
BUILDEOF
    chmod +x build_all.sh

    # Create seeds
    mkdir -p seeds/{openjpeg,libarchive,tcpdump}
    echo "A" > seeds/libarchive/dummy.txt
    echo "A" > seeds/tcpdump/dummy.pcap

    # Seed for OpenJPEG: tiny valid JPEG2000 header (minimal)
    printf '\x00\x00\x00\x0cjP  \r\n\x87\n' > seeds/openjpeg/minimal.jp2

    touch .setup_done
    info "SymCC paper benchmarks set up at $dest"
    echo "  Programs: openjpeg (opj_decompress), libarchive (bsdtar), tcpdump"
    echo "  To build: cd $dest && CC=symcc CXX=sym++ ./build_all.sh"
}

############################################################
# 4. Google FuzzBench (selected benchmarks)
#    Source: https://github.com/google/fuzzbench
#    Docker-based - we clone the repo and provide instructions
############################################################
setup_fuzzbench() {
    local dest="$PUBLIC_DIR/fuzzbench"
    if [ -d "$dest/fuzzbench" ]; then
        info "FuzzBench already downloaded at $dest"
        return
    fi

    info "Downloading Google FuzzBench..."
    mkdir -p "$dest"
    cd "$dest"

    git clone --depth 1 https://github.com/google/fuzzbench.git 2>/dev/null || {
        warn "Git clone failed, trying archive..."
        curl -sL https://github.com/google/fuzzbench/archive/refs/heads/master.tar.gz | tar xz
        mv fuzzbench-master fuzzbench
    }

    cat > README_symcc.txt << 'EOF'
Google FuzzBench - Fuzzer Benchmarking Platform
================================================

FuzzBench is Google's open-source fuzzer benchmarking service.
It contains real-world benchmark programs from OSS-Fuzz.

Key benchmarks suitable for SymCC:
  - freetype2_ftfuzzer          (font rendering)
  - harfbuzz_hb-shape-fuzzer    (text shaping)
  - libjpeg-turbo_fuzzer        (JPEG decoding)
  - libpng_libpng_read_fuzzer   (PNG decoding)
  - openssl_x509                (X.509 parsing)
  - vorbis_decode_fuzzer        (audio decoding)
  - zlib_zlib_uncompress_fuzzer (decompression)
  - libpcap_fuzz_both           (packet capture)

FuzzBench uses Docker for reproducible builds.
See https://google.github.io/fuzzbench/ for full documentation.

To use with SymCC MPI benchmark:
  1. Build targets using FuzzBench's Docker infrastructure
  2. Extract the compiled binaries and seeds
  3. Run: python3 run_benchmark.py --targets <target> ...

Note: FuzzBench already has SymCC integration at:
  fuzzbench/fuzzers/symcc_aflplusplus/
EOF

    info "FuzzBench downloaded to $dest"
    echo "  See $dest/README_symcc.txt for SymCC integration details"
}

############################################################
# 5. Magma (ground-truth fuzzing benchmark)
############################################################
setup_magma() {
    local dest="$PUBLIC_DIR/magma"
    if [ -d "$dest/magma" ]; then
        info "Magma already downloaded at $dest"
        return
    fi

    info "Downloading Magma benchmark..."
    mkdir -p "$dest"
    cd "$dest"

    git clone --depth 1 https://github.com/HexHive/magma.git 2>/dev/null || {
        warn "Git clone failed, trying archive..."
        curl -sL https://github.com/HexHive/magma/archive/refs/heads/main.tar.gz | tar xz
        mv magma-main magma
    }

    cat > README_symcc.txt << 'EOF'
Magma - Ground-Truth Fuzzing Benchmark
=======================================

Magma includes real programs with real bugs, instrumented for
ground-truth measurement. It supports SymCC evaluation.

Target libraries:
  - libpng      (PNG image processing)
  - libtiff     (TIFF image processing)
  - libxml2     (XML parsing)
  - openssl     (TLS/crypto)
  - sqlite3     (database engine)
  - php         (PHP interpreter)
  - poppler     (PDF rendering)

Magma provides Docker-based builds. See:
  https://hexhive.epfl.ch/magma/

Magma already evaluated SymCC in their paper over 200,000 CPU-hours.
EOF

    info "Magma downloaded to $dest"
}

############################################################
# Main
############################################################

usage() {
    echo "Usage: $0 [--all | --lava | --lava-m | --unibench | --symcc-paper | --fuzzbench | --magma]"
    echo ""
    echo "Download and set up public benchmark suites for SymCC MPI benchmarking."
    echo ""
    echo "Options:"
    echo "  --all          Download all benchmarks"
    echo "  --lava         LAVA: target programs (file, jq, grep, pcre2, duktape, etc.)"
    echo "  --lava-m       LAVA-M: 4 coreutils with injected bugs (base64, md5sum, uniq, who)"
    echo "  --unibench     UniBench: 20 real-world programs"
    echo "  --symcc-paper  Programs from the SymCC USENIX paper"
    echo "  --fuzzbench    Google FuzzBench framework"
    echo "  --magma        Magma ground-truth benchmark"
    echo ""
    echo "Recommended for quick start: $0 --lava --symcc-paper"
}

if [ $# -eq 0 ]; then
    usage
    exit 0
fi

for arg in "$@"; do
    case "$arg" in
        --all)
            setup_lava
            setup_lava_m
            setup_unibench
            setup_symcc_paper
            setup_fuzzbench
            setup_magma
            ;;
        --lava)         setup_lava ;;
        --lava-m)       setup_lava_m ;;
        --unibench)     setup_unibench ;;
        --symcc-paper)  setup_symcc_paper ;;
        --fuzzbench)    setup_fuzzbench ;;
        --magma)        setup_magma ;;
        --help|-h)      usage; exit 0 ;;
        *)
            error "Unknown option: $arg"
            usage
            exit 1
            ;;
    esac
done

echo ""
info "Setup complete. Benchmarks are in: $PUBLIC_DIR/"
echo ""
echo "Next steps:"
echo "  1. Build targets with SymCC:  CC=symcc CXX=sym++ ..."
echo "  2. Run MPI benchmark:"
echo "     python3 run_benchmark.py --np-list 2,4,8 --rounds 3 --timeout 120"
echo ""
