#!/bin/bash
# Builds and runs the Synth1 engine unit tests (tests/unit) with the current Xcode.
# The engine is compiled the way src/Synth1.xcodeproj does for Release.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_unit_tests.sh [options] [filter ...]

Options:
  --arch ARCH       arm64 or x86_64 (default: this Mac's). x86_64 runs under Rosetta on Apple Silicon.
  --files GLOB      Only compile the test files matching GLOB, relative to tests/ (default: unit/*.cpp).
  --coverage        Build an instrumented -O0 variant and print a coverage report after the run.
  --build-dir DIR   Where to put the build (default: build/tests-modern-ARCH,
                    or build/tests-modern-coverage-ARCH with --coverage).
  -h, --help        Show this help.

Filters only run the tests whose "suite.name" contains one of them.
EOF
}

# Sets arch, test_files, coverage, build_dir and filters from the command line.
parse_options() {
    arch=$(uname -m)
    test_files="unit/*.cpp"
    coverage=0
    build_dir=""
    filters=()

    while [ $# -gt 0 ]; do
        case "$1" in
            --arch)      arch=$2; shift ;;
            --files)     test_files=$2; shift ;;
            --coverage)  coverage=1 ;;
            --build-dir) build_dir=$2; shift ;;
            -h|--help)   usage; exit 0 ;;
            -*)          echo "error: unknown option $1" >&2; usage >&2; exit 2 ;;
            *)           filters+=("$1") ;;
        esac
        shift
    done

    case "$arch" in
        arm64|x86_64) ;;
        *) echo "error: --arch must be arm64 or x86_64 (it is '$arch')" >&2; exit 2 ;;
    esac

    if [ -z "$build_dir" ] && [ $coverage = 1 ]; then
        build_dir=$root/build/tests-modern-coverage-$arch
    elif [ -z "$build_dir" ]; then
        build_dir=$root/build/tests-modern-$arch
    fi
}

# Prints the files matching a glob (relative to src/), skipping the given base names.
#   $1 = glob, $2 = space-separated base names to skip (without extension)
list_sources() {
    local file name
    for file in $(cd "$src" && ls $1); do
        name=$(basename "${file%.*}")
        case " $2 " in
            *" $name "*) ;;
            *) echo "$file" ;;
        esac
    done
}

# Prints the object file name for a source path: techpop/vcf.cpp becomes techpop_vcf.o
object_name() {
    local path=${1%.*}
    echo "${path//\//_}.o"
}

# Compiles one file, unless its object file is newer than the source and every header.
#   $1 = source file, $2 = object file, $3... = extra compiler flags
compile() {
    local source=$1 object=$2
    shift 2
    objects+=("$object")
    if [ -f "$object" ] && [ ! "$source" -nt "$object" ] &&
       [ -z "$(find "$src" "$tests/harness" "$tests/unit" -name '*.h' -newer "$object" -print -quit)" ]; then
        return
    fi
    case "$source" in
        *.c) xcrun clang -x c "${flags[@]}" "${includes[@]}" "$@" -c "$source" -o "$object" ;;
        *)   xcrun clang++ -x c++ -stdlib=libc++ "${flags[@]}" "${includes[@]}" "$@" -c "$source" -o "$object" ;;
    esac
}

# Compiles the engine, zlib and the tests, and links the test binary.
build() {
    local file stamp
    objects=()
    mkdir -p "$build_dir/engine" "$build_dir/tests"

    # Different compiler flags make every object file stale
    stamp="${flags[*]} | ${fake_flags[*]}"
    if [ "$(cat "$build_dir/.flags" 2>/dev/null)" != "$stamp" ]; then
        rm -f "$build_dir"/engine/*.o "$build_dir"/tests/*.o
        printf '%s' "$stamp" > "$build_dir/.flags"
    fi

    for file in $engine_sources $zlib_sources; do
        case " $faked_sources " in
            *" $file "*) compile "$src/$file" "$build_dir/engine/$(object_name "$file")" "${fake_flags[@]}" ;;
            *)           compile "$src/$file" "$build_dir/engine/$(object_name "$file")" ;;
        esac
    done

    for file in "$tests"/harness/*.cpp $(cd "$tests" && ls $test_files 2>/dev/null | sed "s#^#$tests/#"); do
        [ -f "$file" ] || continue
        compile "$file" "$build_dir/tests/$(basename "${file%.*}").o"
    done

    xcrun clang++ "${link_flags[@]}" "${objects[@]}" -o "$test_binary"
}

# Runs the tests from tests/data, where their fixtures are. The binary picks native or Rosetta
# from its own architecture.
run_tests() {
    cd "$tests/data" 2>/dev/null || cd "$tests"
    "$test_binary" "${filters[@]+"${filters[@]}"}"
}

# Runs the tests and prints the coverage of src/techpop without the GUI.
run_tests_with_coverage() {
    local status=0 profile=$build_dir/tests.profdata report_files
    report_files=$(list_sources 'techpop/*.cpp' "$gui_files" | sed "s#^#$src/#")

    rm -f "$build_dir"/*.profraw
    LLVM_PROFILE_FILE="$build_dir/tests.profraw" run_tests || status=$?

    xcrun llvm-profdata merge -o "$profile" "$build_dir/tests.profraw"
    echo
    echo "== Coverage of the engine sources (src/techpop, without the GUI) =="
    xcrun llvm-cov report "$test_binary" -arch "$arch" -instr-profile="$profile" $report_files | sed "s#$src/##"
    echo "(Per-line detail: xcrun llvm-cov show $test_binary -instr-profile=$profile <source>)"
    return $status
}

root=$(cd "$(dirname "$0")/.." && pwd)
src=$root/src
tests=$root/tests
zlib=zlib125/zlib-1.2.5     # relative to src/

parse_options "$@"
test_binary=$build_dir/synth1_tests

# Source paths are relative to src/. The GUI in src/techpop isn't tested.
gui_files="ccwindow cdib drawgui guiparts stepvoxwin synthdlg synwin"

# The engine: src/techpop without the GUI, plus the Win32 emulation in src/*.cpp that it needs.
# The Objective-C files (window emulation, AU and Cocoa glue) are left out.
engine_sources="
    $(list_sources 'techpop/*.cpp' "$gui_files")
    $(list_sources 'techpop/*.c' '')
    $(list_sources '*.cpp' '')"

# zlib without its two sample programs, and the zip reader from minizip
zlib_sources="
    $(list_sources "$zlib/*.c" 'example minigzip')
    $zlib/contrib/minizip/unzip.c
    $zlib/contrib/minizip/ioapi.c"

# These files use the user's settings and folders. The tests point them at fakes instead (see
# tests/harness/os_fakes.h), so a run never touches the real ones.
faked_sources="winemu_au.cpp techpop/common.cpp techpop/sound.cpp"
fake_flags=(
    -DCFPreferencesCopyAppValue=t1_CFPreferencesCopyAppValue
    -DCFPreferencesSetAppValue=t1_CFPreferencesSetAppValue
    -DCFPreferencesAppSynchronize=t1_CFPreferencesAppSynchronize
    -DFSFindFolder=t1_FSFindFolder
)

sdk=$(xcrun --show-sdk-path)

# The Release settings of src/Synth1.xcodeproj
flags=(
    -arch "$arch" -isysroot "$sdk" -mmacosx-version-min=12.0
    -DUSE_FILE32API -DSYNTH1_MODERN_BUILD
    -ffp-contract=off -fstrict-aliasing
    -g -w
)
link_flags=(
    -arch "$arch" -isysroot "$sdk" -mmacosx-version-min=12.0 -stdlib=libc++
    -framework CoreFoundation -framework CoreServices -framework ApplicationServices
    -framework AudioToolbox -framework Carbon
)
if [ "$arch" = x86_64 ]; then
    flags+=(-fasm-blocks)       # MSVC-style inline assembly, x86_64 only
fi
if [ $coverage = 1 ]; then
    flags+=(-O0 -fprofile-instr-generate -fcoverage-mapping)
    link_flags+=(-fprofile-instr-generate)
else
    flags+=(-Os)
fi

includes=(
    -I"$src" -I"$src/techpop" -I"$src/audiounit"
    -I"$src/$zlib" -I"$src/$zlib/contrib/minizip"
    -I"$tests/harness"
)

build
if [ $coverage = 1 ]; then
    run_tests_with_coverage
else
    run_tests
fi
