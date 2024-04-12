#!/bin/bash

set -u

function usage() {
    echo "Usage: $0 -i INPUT_DIR [-o OUTPUT_DIR] [-f FAILED_DIR] TARGET..."
    echo
    echo "Run SymCC-instrumented TARGET in a loop, feeding newly generated inputs back "
    echo "into it. Initial inputs are expected in INPUT_DIR, and new inputs are "
    echo "continuously read from there. If OUTPUT_DIR is specified, a copy of the corpus "
    echo "and of each generated input is preserved there. TARGET may contain the special "
    echo "string \"@@\", which is replaced with the name of the current input file."
    echo "If FAILED_DIR is specified, a copy of the failing test cases is preserved there."
    echo
    echo "Note that SymCC never changes the length of the input, so be sure that the "
    echo "initial inputs cover all required input lengths."
}

while getopts "i:o:f:" opt; do
    case "$opt" in
        i)
            in=$OPTARG
            ;;
        o)
            out=$OPTARG
            ;;
        f)
            failed_dir=$OPTARG
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done
shift $((OPTIND-1))
target=("$@")
target[0]=$(realpath "${target[0]}")
target="${target[@]}"
timeout="timeout -k 5 90"

if [[ ! -v in ]]; then
    echo "Please specify the input directory!"
    usage
    exit 1
fi

# Create the work environment
work_dir=$(mktemp -d)
mkdir $work_dir/{next,symcc_out}
touch $work_dir/analyzed_inputs
if [[ -v out ]]; then
    mkdir -p $out
fi
if [[ -v failed_dir ]]; then
    mkdir -p "$failed_dir"
fi

function cleanup() {
    rm -rf --preserve-root -- $work_dir
}

trap cleanup EXIT

# Copy one file to the destination directory, renaming it according to its hash.
function copy_file_with_unique_name() {
  local file_name="$1"
  local dest_dir="$2"

  local dest="$dest_dir/$(sha256sum "$file_name" | cut -d' ' -f1)"
  cp "$file_name" "$dest"

}

# Copy all files in the source directory to the destination directory, renaming
# them according to their hash.
function copy_with_unique_name() {
    local source_dir="$1"
    local dest_dir="$2"

    if [ -n "$(ls -A $source_dir)" ]; then
        local f
        for f in $source_dir/*; do
          copy_file_with_unique_name "$f" "$dest_dir"
        done
    fi
}

# Copy files from the source directory into the next generation.
function add_to_next_generation() {
    local source_dir="$1"
    copy_with_unique_name "$source_dir" "$work_dir/next"
}

# If an output directory is set, copy the files in the source directory there.
function maybe_export() {
    local source_dir="$1"
    if [[ -v out ]]; then
        copy_with_unique_name "$source_dir" "$out"
    fi
}

# Remove input files which has been already analysed. Used to prevent infinite loop.
function remove_analysed() {
    local source_dir="$1"
    local f
    for f in $source_dir/*; do
        if grep -q "$(basename $f)" $work_dir/analyzed_inputs; then
            rm $f
        fi
    done
}

# Copy those files from the input directory to the next generation that haven't
# been analyzed yet.
function maybe_import() {
    if [ -n "$(ls -A $in)" ]; then
        local f
        for f in $in/*; do
            if grep -q "$(basename $f)" $work_dir/analyzed_inputs; then
                continue
            fi

            if [ -e "$work_dir/next/$(basename $f)" ]; then
                continue
            fi

            echo "Importing $f from the input directory"
            cp "$f" "$work_dir/next"
        done
    fi
}

# If the input file caused non 0 return code, then copy it to the FAILED_DIR.
function save_failed() {
  local ret_code=$1
  local input_file="$2"
  if [ $ret_code -ne 0 ] && [[ -v failed_dir ]] ; then
    copy_file_with_unique_name "$input_file" "$failed_dir"
  fi
}

# Set up the shell environment
export SYMCC_OUTPUT_DIR=$work_dir/symcc_out
export SYMCC_ENABLE_LINEARIZATION=1
# export SYMCC_AFL_COVERAGE_MAP=$work_dir/map

# Run generation after generation until we don't generate new inputs anymore
gen_count=0
while true; do
    # Initialize the generation
    maybe_import
    mv $work_dir/{next,cur}
    mkdir $work_dir/next

    # Run it (or wait if there's nothing to run on)
    if [ -n "$(ls -A $work_dir/cur)" ]; then
        echo "Generation $gen_count..."

        for f in $work_dir/cur/*; do
            echo "Running on $f"
            if [[ "$target " =~ " @@ " ]]; then
                env SYMCC_INPUT_FILE=$f $timeout ${target[@]/@@/$f} >/dev/null 2>&1
                ret_code=$?
            else
                $timeout $target <$f >/dev/null 2>&1
                ret_code=$?
            fi

            # Make the new test cases part of the next generation
            add_to_next_generation $work_dir/symcc_out
            maybe_export $work_dir/symcc_out
            remove_analysed $work_dir/next
            save_failed $ret_code "$f"
            echo $(basename $f) >> $work_dir/analyzed_inputs
            rm -f $f
        done

        rm -rf $work_dir/cur
        gen_count=$((gen_count+1))
    else
        echo "Waiting for more input..."
        rmdir $work_dir/cur
        sleep 5
    fi
done
