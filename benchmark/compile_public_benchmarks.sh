#!/usr/bin/env bash
#
# Build public benchmark programs for SymCC MPI benchmarking.
#
# Usage:
#   ./build_public_benchmarks.sh [--compiler CC] [--all | --cgc | --lava | --google-fts]
#
# Defaults to gcc if SymCC is not available (for testing the MPI framework).
# Set --compiler to specify the C compiler (e.g., symcc or path/to/symcc).
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PUBLIC_DIR="$SCRIPT_DIR/public"
BUILD_DIR="$SCRIPT_DIR/public/bin"
SEEDS_DIR="$SCRIPT_DIR/public/seeds"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

CC="${CC:-gcc}"
CXX="${CXX:-g++}"

############################################################
# CGC cb-multios  (243 challenge binaries)
# Source: https://github.com/trailofbits/cb-multios
# SymCC paper used these for primary evaluation
############################################################
build_cgc() {
    local cgc_dir="$PUBLIC_DIR/cb-multios"
    if [ ! -d "$cgc_dir" ]; then
        error "CGC not found at $cgc_dir"
        error "Run: cd $PUBLIC_DIR && git clone --depth 1 https://github.com/trailofbits/cb-multios.git"
        return 1
    fi

    info "Building CGC challenges with CC=$CC ..."
    mkdir -p "$BUILD_DIR/cgc" "$SEEDS_DIR/cgc"

    cd "$cgc_dir"
    mkdir -p build
    cd build

    # CGC programs are 32-bit. Try 32-bit first, fall back to native.
    if $CC -m32 -x c -o /dev/null /dev/null 2>/dev/null; then
        info "  Building in 32-bit mode"
        CC="$CC" CXX="$CXX" cmake .. -DCMAKE_C_FLAGS="-m32" -DCMAKE_CXX_FLAGS="-m32" \
            -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
            -DCMAKE_BUILD_TYPE=Release 2>/dev/null || true
    else
        warn "  32-bit compilation not available, trying native..."
        CC="$CC" CXX="$CXX" cmake .. \
            -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
            -DCMAKE_BUILD_TYPE=Release 2>/dev/null || true
    fi

    # Build a subset of well-known challenges (fast to compile, good for benchmarking)
    local built=0
    local failed=0
    local targets=(
        # Small, fast programs good for benchmarking
        "NRFIN_00003" "NRFIN_00006" "NRFIN_00009" "NRFIN_00013"
        "CROMU_00001" "CROMU_00003" "CROMU_00007" "CROMU_00014"
        "KPRCA_00001" "KPRCA_00005" "KPRCA_00011"
        "EAGLE_00004" "EAGLE_00005"
        "YAN01_00001" "YAN01_00004" "YAN01_00007" "YAN01_00012"
    )

    for challenge in "${targets[@]}"; do
        if [ -d "$cgc_dir/challenges/$challenge" ]; then
            make "$challenge" -j$(nproc) 2>/dev/null && {
                # Find built binary
                local bin=$(find . -name "$challenge" -type f -executable 2>/dev/null | head -1)
                if [ -n "$bin" ]; then
                    cp "$bin" "$BUILD_DIR/cgc/"
                    built=$((built + 1))
                fi
            } || {
                failed=$((failed + 1))
            }
        fi
    done

    # Generate simple seeds for CGC programs (they read from stdin)
    for bin_file in "$BUILD_DIR/cgc"/*; do
        if [ -f "$bin_file" ]; then
            local name=$(basename "$bin_file")
            mkdir -p "$SEEDS_DIR/cgc/$name"
            # Generate a few small seed inputs
            echo -n "A" > "$SEEDS_DIR/cgc/$name/seed_01"
            printf '\x00\x00\x00\x00' > "$SEEDS_DIR/cgc/$name/seed_02"
            printf '\x41\x42\x43\x44\x45\x46\x47\x48' > "$SEEDS_DIR/cgc/$name/seed_03"
            head -c 64 /dev/urandom > "$SEEDS_DIR/cgc/$name/seed_04" 2>/dev/null || true
        fi
    done

    cd "$SCRIPT_DIR"
    info "CGC: built $built, failed $failed"
}

############################################################
# LAVA targets  (file, jq, grep, pcre2, etc.)
# Source: https://github.com/panda-re/lava
# Real programs with injectable bugs
############################################################
build_lava() {
    local lava_dir="$PUBLIC_DIR/lava-m/lava"
    if [ ! -d "$lava_dir" ]; then
        error "LAVA not found at $lava_dir"
        error "Run: cd $PUBLIC_DIR/lava-m && git clone --depth 1 https://github.com/panda-re/lava.git"
        return 1
    fi

    info "Building LAVA targets with CC=$CC ..."
    mkdir -p "$BUILD_DIR/lava" "$SEEDS_DIR/lava"

    local built=0
    local target_bins_dir="$lava_dir/target_bins"

    # Build from tarballs
    for tarball in "$target_bins_dir"/*.tar.gz; do
        if [ ! -f "$tarball" ]; then continue; fi
        local name=$(basename "$tarball" .tar.gz | sed 's/-[0-9].*//; s/-pre$//')
        info "  Extracting and building $name ..."

        local work="$PUBLIC_DIR/lava-m/build_$name"
        rm -rf "$work"
        mkdir -p "$work"

        # Extract
        tar xzf "$tarball" -C "$work" 2>/dev/null || { warn "  Failed to extract $tarball"; continue; }

        # Find the extracted directory
        local src_dir=$(find "$work" -mindepth 1 -maxdepth 1 -type d | head -1)
        if [ -z "$src_dir" ]; then
            warn "  No source directory found for $name"
            continue
        fi

        cd "$src_dir"

        # Try to build
        local bin_path=""
        case "$name" in
            file)
                if [ -f configure ]; then
                    CC="$CC" ./configure --quiet 2>/dev/null && make -j$(nproc) 2>/dev/null
                    bin_path="src/file"
                fi
                ;;
            jq)
                if [ -f configure ]; then
                    CC="$CC" ./configure --quiet --disable-maintainer-mode 2>/dev/null && make -j$(nproc) 2>/dev/null
                    bin_path="jq"
                elif [ -f Makefile ]; then
                    CC="$CC" make -j$(nproc) 2>/dev/null
                    bin_path="jq"
                fi
                ;;
            grep)
                if [ -f configure ]; then
                    CC="$CC" ./configure --quiet 2>/dev/null && make -j$(nproc) 2>/dev/null
                    bin_path="src/grep"
                fi
                ;;
            pcre2)
                if [ -f configure ]; then
                    CC="$CC" ./configure --quiet 2>/dev/null && make -j$(nproc) 2>/dev/null
                    bin_path="pcre2grep"
                elif [ -f CMakeLists.txt ]; then
                    mkdir -p build && cd build
                    CC="$CC" cmake .. 2>/dev/null && make -j$(nproc) 2>/dev/null
                    bin_path="pcre2grep"
                    cd ..
                fi
                ;;
            duktape)
                if [ -f Makefile ]; then
                    CC="$CC" make -j$(nproc) 2>/dev/null
                    bin_path="duk"
                fi
                ;;
            libyaml)
                if [ -f configure ]; then
                    CC="$CC" ./configure --quiet 2>/dev/null && make -j$(nproc) 2>/dev/null
                    bin_path="tests/run-parser"
                elif [ -f CMakeLists.txt ]; then
                    mkdir -p build && cd build
                    CC="$CC" cmake .. 2>/dev/null && make -j$(nproc) 2>/dev/null
                    bin_path="tests/run-parser"
                    cd ..
                fi
                ;;
            *)
                # Generic: try configure && make
                if [ -f configure ]; then
                    CC="$CC" ./configure --quiet 2>/dev/null && make -j$(nproc) 2>/dev/null
                elif [ -f CMakeLists.txt ]; then
                    mkdir -p build && cd build
                    CC="$CC" cmake .. 2>/dev/null && make -j$(nproc) 2>/dev/null
                    cd ..
                elif [ -f Makefile ]; then
                    CC="$CC" make -j$(nproc) 2>/dev/null
                fi
                ;;
        esac

        # Check if we got a binary
        if [ -n "$bin_path" ] && [ -f "$bin_path" ]; then
            cp "$bin_path" "$BUILD_DIR/lava/${name}"
            built=$((built + 1))
            info "    -> $name built successfully"
        else
            # Try to find any executable
            local found=$(find . -maxdepth 3 -name "$name" -type f -executable 2>/dev/null | head -1)
            if [ -n "$found" ]; then
                cp "$found" "$BUILD_DIR/lava/${name}"
                built=$((built + 1))
                info "    -> $name built successfully"
            else
                warn "    $name: no binary found"
            fi
        fi

        # Create seeds
        mkdir -p "$SEEDS_DIR/lava/$name"
        echo "test input" > "$SEEDS_DIR/lava/$name/seed_01"
        printf 'AAAAAAAAAAAAAAAA' > "$SEEDS_DIR/lava/$name/seed_02"
        head -c 128 /dev/urandom > "$SEEDS_DIR/lava/$name/seed_03" 2>/dev/null || true

        cd "$SCRIPT_DIR"
    done

    info "LAVA: built $built targets"
}

############################################################
# Google fuzzer-test-suite
# Source: https://github.com/google/fuzzer-test-suite
# Real-world targets: freetype, libpng, openssl, etc.
# These require downloading source from the internet
############################################################
build_google_fts() {
    local fts_dir="$PUBLIC_DIR/fuzzer-test-suite"
    if [ ! -d "$fts_dir" ]; then
        error "Google fuzzer-test-suite not found at $fts_dir"
        error "Run: cd $PUBLIC_DIR && git clone --depth 1 https://github.com/google/fuzzer-test-suite.git"
        return 1
    fi

    info "Building Google fuzzer-test-suite targets..."
    info "  NOTE: These targets download source from the internet"
    mkdir -p "$BUILD_DIR/google-fts" "$SEEDS_DIR/google-fts"

    local built=0

    # We build a subset of targets that are well-suited for SymCC
    # These are standalone C programs that take file input
    local targets=(
        "libpng-1.2.56"
        "libarchive-2017-01-04"
        "c-ares-CVE-2016-5180"
        "re2-2014-12-09"
        "vorbis-2017-12-11"
        "sqlite-2016-11-14"
        "libxml2-v2.9.2"
    )

    for target in "${targets[@]}"; do
        local target_dir="$fts_dir/$target"
        if [ ! -d "$target_dir" ]; then
            warn "  $target directory not found"
            continue
        fi

        info "  Building $target ..."
        cd "$target_dir"

        # These build scripts expect specific env vars
        export FUZZING_ENGINE="standalone"
        export LIB_FUZZING_ENGINE=""
        export EXECUTABLE_NAME_BASE="$BUILD_DIR/google-fts/$target"
        export SRC="$target_dir/SRC"
        export JOBS=$(nproc)

        # Try to run the build script
        if bash build.sh 2>/dev/null; then
            if [ -f "$BUILD_DIR/google-fts/$target" ]; then
                built=$((built + 1))
                info "    -> $target built successfully"

                # Copy seeds
                if [ -d seeds ]; then
                    mkdir -p "$SEEDS_DIR/google-fts/$target"
                    cp seeds/* "$SEEDS_DIR/google-fts/$target/" 2>/dev/null || true
                fi
            fi
        else
            warn "    $target: build failed"
        fi

        cd "$SCRIPT_DIR"
    done

    info "Google FTS: built $built targets"
}

############################################################
# Main
############################################################
usage() {
    echo "Usage: $0 [--compiler CC] [--all | --cgc | --lava | --google-fts]"
    echo ""
    echo "Build public benchmark programs. Assumes repos are already cloned"
    echo "into benchmark/public/ (use setup_public_benchmarks.sh first)."
    echo ""
    echo "Options:"
    echo "  --compiler CC   C compiler to use (default: gcc, or use 'symcc')"
    echo "  --all           Build all available benchmarks"
    echo "  --cgc           Build CGC cb-multios challenges"
    echo "  --lava          Build LAVA target programs"
    echo "  --google-fts    Build Google fuzzer-test-suite"
    echo ""
    echo "Output:"
    echo "  Binaries: $BUILD_DIR/<suite>/"
    echo "  Seeds:    $SEEDS_DIR/<suite>/"
    echo ""
    echo "Example:"
    echo "  # Build with gcc (for MPI framework testing):"
    echo "  $0 --all"
    echo ""
    echo "  # Build with SymCC (for real symbolic execution):"
    echo "  $0 --compiler symcc --all"
}

# Parse args
BUILD_CGC=false
BUILD_LAVA=false
BUILD_GOOGLE=false

if [ $# -eq 0 ]; then
    usage
    exit 0
fi

while [ $# -gt 0 ]; do
    case "$1" in
        --compiler)
            shift
            CC="$1"
            if [ "$CC" = "symcc" ] || [[ "$CC" == *"/symcc" ]]; then
                # If using symcc, also set CXX to sym++
                CXX="${CC%symcc}sym++"
            fi
            ;;
        --all)        BUILD_CGC=true; BUILD_LAVA=true; BUILD_GOOGLE=true ;;
        --cgc)        BUILD_CGC=true ;;
        --lava)       BUILD_LAVA=true ;;
        --google-fts) BUILD_GOOGLE=true ;;
        --help|-h)    usage; exit 0 ;;
        *)            error "Unknown option: $1"; usage; exit 1 ;;
    esac
    shift
done

mkdir -p "$BUILD_DIR" "$SEEDS_DIR"

echo "================================================================"
echo "  Building Public Benchmarks"
echo "  CC=$CC  CXX=$CXX"
echo "================================================================"
echo ""

$BUILD_CGC    && build_cgc
$BUILD_LAVA   && build_lava
$BUILD_GOOGLE && build_google_fts

echo ""
echo "================================================================"
echo "  Build Complete"
echo "================================================================"
echo "  Binaries:  $BUILD_DIR/"
echo "  Seeds:     $SEEDS_DIR/"
echo ""

# Show what was built
for suite_dir in "$BUILD_DIR"/*/; do
    if [ -d "$suite_dir" ]; then
        local_count=$(find "$suite_dir" -type f -executable 2>/dev/null | wc -l)
        suite_name=$(basename "$suite_dir")
        echo "  $suite_name: $local_count binaries"
    fi
done

echo ""
echo "To run MPI benchmark with these binaries:"
echo "  python3 run_benchmark.py --public \\"
for bin_file in "$BUILD_DIR"/*/*; do
    if [ -f "$bin_file" ] && [ -x "$bin_file" ]; then
        local_name=$(basename "$bin_file")
        local_suite=$(basename "$(dirname "$bin_file")")
        local_seeds="$SEEDS_DIR/$local_suite/$local_name"
        if [ -d "$local_seeds" ]; then
            echo "    '$local_name:$bin_file:$local_seeds' \\"
        fi
    fi
done
echo "    --np-list 2,4,8 --rounds 3 --timeout 120"
