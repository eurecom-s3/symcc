#!/bin/bash

set -u

function usage() {
    echo "Usage: $0 -i INPUT_DIR [-o OUTPUT_DIR] TARGET..."
    echo
    echo "Run SymCC-instrumented TARGET in a loop, feeding newly generated inputs back "
    echo "into it. Initial inputs are expected in INPUT_DIR, and new inputs are "
    echo "continuously read from there. If OUTPUT_DIR is specified, a copy of the corpus "
    echo "and of each generated input is preserved there. TARGET may contain the special "
    echo "string \"@@\", which is replaced with the name of the current input file."
    echo
    echo "Note that SymCC never changes the length of the input, so be sure that the "
    echo "initial inputs cover all required input lengths."
}

while getopts "i:o:" opt; do
    case "$opt" in
        i)
            in=$OPTARG
            ;;
        o)
            out=$OPTARG
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done
shift $((OPTIND-1))
target=$@
timeout="timeout -k 5 90"

if [[ ! -v in ]]; then
    echo "Please specify the input directory!"
    usage
    exit 1
fi

# Create temporary directories for the current and next generations of inputs
work_dir=$(mktemp -d)
mkdir $work_dir/{cur,next}
if [[ -v out ]]; then
    mkdir -p $out
fi

function cleanup() {
    rm -rf $work_dir
}

trap cleanup EXIT

# Move files from the source directory into our current generation and possibly
# make a copy in $out
function import() {
    source_dir=$1
    if [ "$(ls -A $source_dir)" ]; then
        for new_case in $source_dir/*; do
            dest=$work_dir/cur/$(sha256sum $new_case | cut -d' ' -f1)
            mv $new_case $dest

            if [[ -v out ]]; then
                cp $dest $out
            fi
        done
    fi
}

# Set up the environment
export SYMCC_OUTPUT_DIR=$work_dir/next
export SYMCC_ENABLE_LINEARIZATION=1
export SYMCC_AFL_COVERAGE_MAP=$work_dir/map

# Run generation after generation until we don't generate new inputs anymore
gen_count=0
while true; do
    # Initialize the generation
    import $work_dir/next      # created in the previous generation
    import $in                 # new files from the input directory (if any)

    # Run it (or wait if there's nothing to run on)
    if [ "$(ls -A $work_dir/cur)" ]; then
        echo "Generation $gen_count..."
        for f in $work_dir/cur/*; do
            if [[ "$target " =~ " @@ " ]]; then
                SYMCC_INPUT_FILE=$f $timeout ${target[@]/@@/$f} >/dev/null 2>&1
            else
                cat $f | $timeout $target >/dev/null 2>&1
            fi

            rm $f
        done

        gen_count=$((gen_count+1))
    else
        echo "Waiting for more input..."
        sleep 5
    fi
done
