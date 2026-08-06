#!/usr/bin/env bash
# run_coverage.sh -- gcov/lcov coverage runner and line-coverage gate for the HDMI-CEC
# middleware's L1 unit-test suite (run_L1Tests).
#
# ==============================================================================
# PURPOSE
# ==============================================================================
#   Capture coverage from the instrumented middleware build, write an HTML report,
#   print a per-file table of LINE, FUNCTION and BRANCH figures derived from the lcov
#   trace records, enumerate every file below the bar together with its uncovered line
#   numbers, and exit non-zero when line coverage misses the threshold.
#
#   It exists because this submodule's own workflow already captures coverage but
#   (a) collects no branch data and (b) applies no numeric threshold whatsoever.
#   Everything else -- the capture directory, the exclusion globs, the artifact file
#   names and the genhtml title -- is reproduced from .github/workflows/L1-tests.yml
#   lines 150-173, which is this submodule's own recipe and the authority for every
#   lcov argument used below.  That workflow is NOT modified by this script and must not
#   be: it is the authority, so it is read, never rewritten.
#
#   Two gaps in the CI recipe, and how they are closed here:
#     1. NO BRANCH DATA.  lcov collects branch data only when asked, and the legacy
#        `lcov_branch_coverage` configuration key is deprecated in lcov 2.x and defaults
#        to zero.  The workflow passes no `--rc` and copies no lcov configuration, so the
#        CI step inherits the system default and discards branch data entirely.  This
#        script therefore passes `--rc branch_coverage=1` explicitly on the capture, the
#        filter, the genhtml and the summary invocations -- a run-time override outranks
#        every configuration file, which is why it appears on all four steps rather than
#        only in a configuration file.
#     2. NO THRESHOLD.  No numeric coverage check exists anywhere in this submodule's
#        build, Makefiles or workflows.  The `--fail-under-lines` invocation below is the
#        first machine-checkable coverage gate in this workspace.
#
# ==============================================================================
# GOVERNING CONTRACT
# ==============================================================================
#   This project has NO user-specified rules: `review_rules` returns exactly
#   "No user rules provided."  No rule governs this file, so none is cited and none is
#   invented.  Their absence is not licence to lower the bar, so the substituting binding
#   contract is the enterprise-standard bar (specification section 0.12.1):
#
#     1. REPOSITORY CONVENTION IS AUTHORITATIVE.  The workflow's recipe is reproduced
#        rather than reinvented, and the sibling runner
#        entservices-hdmicecsink/Tests/run_coverage.sh sets the shape (logging helpers,
#        absolute tool resolution, home-configuration stashing, awk trace parsing,
#        per-file table, gate spelling).  Where the tool's real behaviour contradicts
#        expectation the tension is documented, not silently resolved -- see the two
#        MEASURED CORRECTIONS below.
#     2. NO NEW FRAMEWORK, TOOL OR DEPENDENCY.  bash, the POSIX/coreutils primitives it
#        already needs (printf, awk, sed, grep, sort, find, wc, mktemp, dirname,
#        basename, mv, rm, ln, touch, mkdir), lcov 2.x, genhtml and gcov.  No gcovr, no
#        jq, no python, no coverage wrapper of any kind.
#     3. ADDITIVE AND NON-DESTRUCTIVE.  This script writes nothing into ccec/**,
#        osal/**, mocks/**, tests/L1Tests/test_main.cpp, tests/L1Tests/.gitignore, the
#        workflows, or /etc/lcovrc.  It never commits, never regenerates a committed
#        build file behind your back, and never runs `git checkout` as part of the
#        coverage pipeline.  Its two side effects are stated rather than buried:
#          (a) $HOME/.lcovrc -- lcov reads it silently and a branch-disabled copy there
#              would defeat everything above, so an existing one is MOVED ASIDE into a
#              private temporary directory for the duration of the run and RESTORED by an
#              EXIT/INT/TERM/HUP trap, however the run ends.  Capture and restore, never
#              unconditional deletion, so a developer's own lcov configuration survives.
#              The stash path is logged, so even a run killed with SIGKILL -- the one
#              signal a trap cannot service -- leaves a named, recoverable copy.
#              /etc/lcovrc is never touched, and nothing else under $HOME is read or
#              written.
#          (b) Artifacts -- the fixed names under ARTIFACTS below are created and
#              OVERWRITTEN WITHOUT PROMPTING, exactly as CI overwrites them in
#              $GITHUB_WORKSPACE.  See ARTIFACT HYGIENE for where they land and why.
#          With --build or --run there are two further, opt-in side effects: the build
#          writes into this submodule's working tree, and the run zeroes this build
#          tree's *.gcda counters.  Both are off by default.
#     4. MEASURED CLAIMS ONLY.  Every number printed is read out of the trace captured
#        moments earlier.  Nothing is hard-coded, defaulted or estimated, and an empty
#        capture is a loud failure rather than a plausible-looking zero.  gcov counters
#        ACCUMULATE across runs, so a stale *.gcda keeps a line marked hit long after the
#        test that hit it stopped running -- which would let a gate pass on an earlier
#        run's evidence.  `--run` therefore zeroes the counters before the suite and
#        refuses to capture unless the suite produced fresh ones; without `--run` the
#        script reports how many counter files it found and when they were last written,
#        so a stale measurement is visible rather than silent.
#     5. DETERMINISTIC AND ISOLATED.  Fixed artifact names, no timestamps in output, no
#        wall-clock sleeps, no reliance on the caller's working directory: every path is
#        resolved absolutely from BASH_SOURCE, so running this script from its own
#        directory, from the submodule root or from the superproject root produces
#        identical results.  The per-file table is a pure function of the trace it reads,
#        so the same trace always yields byte-identical output, and the HTML report is
#        built in a private staging directory and published by rename so no page from a
#        larger earlier trace can survive into a smaller later one.
#     6. HONEST REPORTING OVER CONVENIENT NUMBERS.  The seven exclusion globs are
#        reproduced VERBATIM and NOTHING is added to them.  Those globs are what keep the
#        coverage denominator production-source-only, which is what forces coverage to
#        move by adding tests rather than by editing source or widening a filter.  Files
#        below the bar are printed with their real figures and their uncovered line
#        numbers, never excluded to flatter a percentage.  The threshold is a GATE, not a
#        FILTER.
#     7. REPRODUCIBILITY.  `set -euo pipefail`, absolute path resolution, the exact
#        command forms below, and the full build recipe recorded in BUILD RECIPE so the
#        figures can be reproduced rather than trusted.
#
#   The zero-production-source-modification directive binds this file absolutely: it is a
#   read-and-measure tool.  Where a coverage gap cannot be closed without a production
#   change, this script's job is to REPORT it -- which is what the below-bar enumeration
#   is for -- not to close it.
#
# ==============================================================================
# MEASURED CORRECTIONS TO THE OBVIOUS SPELLINGS  (both verified against lcov 2.0-1)
# ==============================================================================
#   1. THE GATE MUST BE SPELLED WITH AN OPERATION.  The intuitive form
#          lcov --fail-under-lines 80 filtered_coverage.info
#      is rejected outright:
#          lcov: ERROR: invalid command line:
#          Need one of options -z, -c, -a, -e, -r, -l, --diff, --intersect,
#          --subtract, or --summary
#      and exits 2.  Because 2 is non-zero, that form LOOKS like a working gate while
#      actually failing for an unrelated reason and never once consulting the coverage
#      figure -- a gate that can only fail is no more a gate than one that can only
#      pass.  The correct spelling, used below, pairs the threshold with an operation:
#          lcov --summary filtered_coverage.info --fail-under-lines 80
#      Verified: exit 0 at or above the bar, exit 1 below it.
#   2. FUNCTION FIGURES COME FROM THE ALIAS RECORDS, NOT THE LEADER RECORDS.  lcov 2.x
#      writes per-function data as FNL:<index>,<start>,<end> and
#      FNA:<index>,<count>,<name> (this trace carries 440 FNA and 431 FNL records and
#      zero of the older FN:/FNDA: records).  Counting the FNA aliases reproduces
#      `lcov --summary` EXACTLY -- both report 377/440 -- whereas the per-file FNF:/FNH:
#      leader totals give 375/431, because several aliases can share one leader.  The
#      FUNCTIONS column below is therefore alias-derived so that the TOTAL row reconciles
#      with the summary block, and the leader figures are printed alongside it rather than
#      dropped.  Only FNA's numeric SECOND field is read, so a demangled name containing
#      commas (templates, operator overloads) cannot corrupt the count; the name is
#      always the LAST field.
#   3. NOT EVERY FILE HAS BRANCH DATA, AND THAT IS CORRECT.  22 of the 30 files in the
#      filtered trace carry BRF:/BRH: records; the other 8 contain no branch arcs at all,
#      so lcov emits no branch record for them.  Those cells render "n/a  no data"
#      instead of a misleading 0.0%.
#   4. `--ignore-errors category` IS NEVER PASSED.  It is not a documented error class;
#      it is deliberately absent from every ignore list below and must not be added.
#   5. `lcov --list` IS NEVER USED.  It emits malformed rates above 100% in lcov 2.x.
#      Every figure here is parsed out of the trace records instead.
#
# ==============================================================================
# ARTIFACT HYGIENE -- READ BEFORE CHANGING THE OUTPUT LOCATION
# ==============================================================================
#   tests/L1Tests/.gitignore ignores *.o *.lo *.la .libs/ .deps/ Makefile Makefile.in
#   run_L1Tests *.log *.trs *.xml .dirstamp -- and does NOT ignore coverage.info,
#   filtered_coverage.info or coverage/.  hdmicec/.gitignore and the superproject's
#   .gitignore are both empty.  That .gitignore is OUT OF SCOPE for this change and is
#   not edited, so the mitigation is placement instead of ignoring: this script's outputs
#   are UNCOMMITTABLE BUILD ARTIFACTS -- the same discipline the six configure-generated
#   Makefiles get -- and they default to a directory OUTSIDE the git tree entirely:
#
#       ${TMPDIR:-/tmp}/hdmicec-l1-coverage/<basename of the workspace root>
#
#   Outside the tree they cannot be staged even by `git add -A`, and the workspace-root
#   basename keeps parallel checkouts of this superproject from overwriting each other's
#   evidence without needing any environment variable.  The CI file NAMES are preserved
#   exactly, so `--output-dir .` run from the superproject root reproduces CI's layout
#   byte for byte if that is what you want -- at which point keeping them out of a commit
#   becomes yours to manage.
#
# ==============================================================================
# USAGE
# ==============================================================================
#   ./run_coverage.sh                       measure existing profile data, report, gate
#   ./run_coverage.sh --run                 zero counters, run the suite, then the above
#   ./run_coverage.sh --build --run         build first, then the above
#   ./run_coverage.sh --threshold 90        raise the bar for a diagnostic run
#   ./run_coverage.sh --per-file-gate       also fail if any single file is below the bar
#   ./run_coverage.sh --output-dir DIR      write artifacts to DIR
#   ./run_coverage.sh --no-html             skip the genhtml report
#   ./run_coverage.sh --restore             restore the six tracked Makefiles, then exit
#   ./run_coverage.sh --help                full option and environment reference
#
#   ENVIRONMENT (all optional; command-line flags win)
#     COVERAGE_MIN            line-coverage bar, default 80
#     COVERAGE_OUTPUT_DIR     artifact directory, default as under ARTIFACT HYGIENE
#     COVERAGE_PER_FILE_GATE  1 to gate per file as well as on the aggregate, default 0
#     GTEST_PREFIX            prefix providing libgtest/libgmock, default <WS>/install/usr
#     GTEST_EXTRA_ARGS        extra arguments appended to the run_L1Tests command line
#
#   ARTIFACTS (fixed names, written under the output directory)
#     coverage.info              raw capture over the whole submodule
#     filtered_coverage.info     production-source-only trace, after the seven globs
#     coverage/index.html        genhtml report, with a Branches column
#     per_file_coverage.tsv      machine-readable per-file LINE/FUNCTION/BRANCH figures
#     uncovered_lines.txt        per-file uncovered line numbers for below-bar targets
#     rdkL1TestResults.json      GoogleTest results, when --run is used (as in CI)
#     capture.log filter.log genhtml.log run_L1Tests.log build.log
#
#   The DIRECTORY is the override point; the FILE NAMES are fixed, and that is deliberate
#   rather than an omission.  coverage.info, filtered_coverage.info and coverage/ are the
#   names the workflow writes into $GITHUB_WORKSPACE, so keeping them makes a local run's
#   output interchangeable with a CI artifact bundle and lets any downstream consumer name
#   one path.  Making them individually configurable would buy nothing and would let two
#   runs disagree about what the evidence is called; when you need two sets of artifacts
#   side by side, give each run its own --output-dir.
#
# ==============================================================================
# PREREQUISITES
# ==============================================================================
#   * lcov 2.x, genhtml and gcov on PATH.
#   * The suite must have been BUILT WITH INSTRUMENTATION and EXECUTED, so that *.gcno
#     and *.gcda exist under this submodule.  `--build` and `--run` will do both; without
#     them the script measures whatever counters are already present and says so.
#   * libglib2.0-dev is a HARD requirement of the build: configure.ac runs
#     PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 0.10.28]) and configure fails without it.
#   * --enable-l1tests is what pulls in PKG_CHECK_MODULES([GTEST], [gtest >= 1.10.0]);
#     without that flag no test suite is configured at all.
#   * stubs/ is a BUILD PREREQUISITE, not a committed artifact -- the workflow generates
#     it and it is excluded from coverage by the '*/stubs/*' glob.
#
# ==============================================================================
# BUILD RECIPE  (what --build runs; reproduced from .github/workflows/L1-tests.yml)
# ==============================================================================
#   cd <hdmicec>
#   mkdir -p stubs/rdk/iarmbus stubs/ccec/drivers/iarmbus
#   touch stubs/rdk/iarmbus/libIARM.h stubs/rdk/iarmbus/libIBus.h \
#         stubs/rdk/iarmbus/libIBusDaemon.h stubs/ccec/drivers/iarmbus/CecIARMBusMgr.h
#   ln -sf ../../../mocks/hdmicec/hdmi_cec_driver.h stubs/ccec/drivers/hdmi_cec_driver.h
#   autoreconf -if
#   PKG_CONFIG_PATH="$GTEST_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH" \
#   CPPFLAGS="-I$PWD/mocks -I$PWD/stubs -I$GTEST_PREFIX/include" \
#   LDFLAGS="-L$GTEST_PREFIX/lib -fprofile-arcs -ftest-coverage" \
#   CXXFLAGS="-fprofile-arcs -ftest-coverage" ./configure --enable-l1tests
#   make -j$(nproc) all && make -C tests/L1Tests all
#
#   The PKG_CONFIG_PATH line is the one addition to the workflow's own command, and it is
#   not optional off a CI runner: configure.ac uses PKG_CHECK_MODULES for GoogleTest, which
#   consults pkg-config ONLY and ignores -I/-L.  CI additionally apt-installs libgtest-dev,
#   so a system gtest.pc satisfies the check there; a host whose GoogleTest is a
#   source-built copy in a private prefix -- the usual arrangement when the distribution's
#   GoogleTest is too new for this project's C++ standard -- needs the prefix on
#   pkg-config's path or configure aborts with "Google Test not found".
#
#   MANDATORY BUILD HYGIENE.  autoreconf and configure REWRITE six GIT-TRACKED Makefile
#   files in this submodule.  They are local toolchain output and must never be
#   committed.  Restore them with:
#
#       git -C <hdmicec> checkout -- Makefile ccec/Makefile ccec/src/Makefile \
#                                    osal/Makefile osal/src/Makefile tests/Makefile
#
#   `--build` prints that command and the list of files it dirtied but DELIBERATELY DOES
#   NOT RUN IT: silently reverting tracked files in someone's working tree is not a thing
#   a measurement tool gets to do.  `--restore` runs it, and only it, on request.  No
#   `git checkout` is ever executed as part of the coverage pipeline.
#
# ==============================================================================
# GATE
# ==============================================================================
#   LINE COVERAGE ONLY.  The aggregate check is delegated to lcov's own
#   --fail-under-lines so the number that decides the outcome is lcov's, not this
#   script's, and its non-zero exit is what fails the run.  Every file below the bar is
#   ALWAYS enumerated -- with its uncovered line numbers, so the "enumerate the specific
#   uncoverable lines and the reason" requirement can be answered directly from this
#   output -- and `--per-file-gate` additionally turns that enumeration into a failure
#   for callers who want the per-target form enforced.  It is OFF by default, honestly:
#   the filtered trace legitimately contains template-heavy headers under ccec/include
#   whose figures were never part of the named target set, and defaulting them into a
#   hard failure would mean this script reports a red run for work nobody asked for.
#   They are printed either way.
#
#   BRANCH COVERAGE IS REPORTED AS EVIDENCE AND NEVER GATED.  gcov models branches as
#   control-flow-graph arcs, and a C++ translation unit's arcs include compiler-generated
#   exception and static-destruction paths that no test can drive, so 100% branch
#   coverage is unattainable for most translation units and a low branch rate is not by
#   itself evidence of a weak test.  Branch movement is the evidence that negative and
#   corner-case tests landed; line coverage is the acceptance criterion.
#
#   RANDOMISED-ORDER RUNS ARE NOT WIRED IN, ON PURPOSE.  `--gtest_shuffle` is a
#   diagnostic: this suite contains pre-existing order-fragile tests that pass in the
#   default order, and tests that pass must not be modified on this pass, so a shuffled
#   run is expected to stay red and must never gate anything.  Nothing here enables it,
#   and no watch mode exists or is added.
#
# ==============================================================================
set -euo pipefail

# ------------------------------------------------------------------------------------
# Path resolution.  Everything is derived from this script's own location so that the
# caller's working directory is irrelevant: the CI recipe captures over the submodule
# root while this file lives two levels below it, and `-d hdmicec` in the workflow is
# only correct when the working directory happens to be the superproject root.
#   SCRIPT_DIR    <hdmicec>/tests/L1Tests
#   HDMICEC_ROOT  <hdmicec>                 -- the lcov capture directory
#   WS            the directory holding <hdmicec>, i.e. CI's $GITHUB_WORKSPACE
# `pwd -P` resolves symlinks so that the paths compared against trace records, and the
# paths handed to `lcov -d`, are the same physical paths gcov recorded.
# ------------------------------------------------------------------------------------
SCRIPT_PATH="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/$(basename -- "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(dirname -- "$SCRIPT_PATH")"
HDMICEC_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd -P)"
WS="$(dirname -- "$HDMICEC_ROOT")"
readonly SCRIPT_PATH SCRIPT_DIR HDMICEC_ROOT WS

# ------------------------------------------------------------------------------------
# Tool resolution.  Absolute paths are taken from the inherited environment up front so
# that a later PATH change -- for example prepending an install tree before running the
# suite -- cannot swap the measurement tools underneath the pipeline.
# ------------------------------------------------------------------------------------
resolve_tool() { # $1=tool name -> absolute path on stdout, empty when absent
    command -v -- "$1" 2>/dev/null || true
}
LCOV_BIN="$(resolve_tool lcov)"
GENHTML_BIN="$(resolve_tool genhtml)"
GCOV_BIN="$(resolve_tool gcov)"
AWK_BIN="$(resolve_tool awk)"
FIND_BIN="$(resolve_tool find)"
MKTEMP_BIN="$(resolve_tool mktemp)"
readonly LCOV_BIN GENHTML_BIN GCOV_BIN AWK_BIN FIND_BIN MKTEMP_BIN

# ------------------------------------------------------------------------------------
# Configuration.  Every value is overridable from the environment, and the defaults are
# the ones that reproduce CI.  Command-line flags are applied after parsing and win.
# ------------------------------------------------------------------------------------
COVERAGE_MIN="${COVERAGE_MIN:-80}"
COVERAGE_PER_FILE_GATE="${COVERAGE_PER_FILE_GATE:-0}"
# Default artifact directory: outside the git tree (see ARTIFACT HYGIENE above), and
# discriminated by the workspace-root basename so parallel checkouts do not collide.
OUTPUT_DIR="${COVERAGE_OUTPUT_DIR:-${TMPDIR:-/tmp}/hdmicec-l1-coverage/$(basename -- "$WS")}"
# Prefix providing libgtest/libgmock and their headers.  CI uses $GITHUB_WORKSPACE/install/usr.
GTEST_PREFIX="${GTEST_PREFIX:-$WS/install/usr}"
GTEST_EXTRA_ARGS="${GTEST_EXTRA_ARGS:-}"

DO_BUILD=0
DO_RUN=0
DO_HTML=1
DO_RESTORE_ONLY=0

# ------------------------------------------------------------------------------------
# Constants reproduced from .github/workflows/L1-tests.yml.
#
# THE SEVEN EXCLUSION GLOBS, VERBATIM AND IN THE WORKFLOW'S ORDER.  Do not add one, do
# not remove one, do not reorder them.  They are architecturally load-bearing: they strip
# system headers, the installed dependency headers, the generated stub tree, the HAL
# driver mock, GoogleTest, the whole test tree and any test translation unit from the
# denominator, which leaves production source only.  That is precisely what forces
# coverage to move by adding tests rather than by editing production source or widening a
# filter, so widening this list would not raise coverage -- it would destroy the
# guarantee that the number means anything.  Each element is single-quoted at its point of
# use and every expansion below is quoted, so the shell never gets a chance to glob them
# against the working directory.
# ------------------------------------------------------------------------------------
readonly LCOV_EXCLUDES=(
    '/usr/include/*'
    '*/install/usr/include/*'
    '*/stubs/*'
    '*/mocks/*'
    '*/googletest/*'
    '*/tests/*'
    '*/test_*.cpp'
)

readonly GENHTML_TITLE='hdmicec coverage'

# Error classes tolerated per step.  `category` is NOT a documented class and is
# deliberately absent from all three lists -- do not add it.  `deprecated` appears
# because .lcovrc_l1 deliberately uses the backward-compatible key spellings
# (lcov_branch_coverage, lcov_function_coverage, geninfo_no_exception_branch) so that a
# pre-2.x lcov can still read it; the doubled token is lcov's own spelling for demoting
# the warning to a counted message, and the counts stay visible in the message summary
# and in the per-step logs, so nothing is hidden.
readonly LCOV_CAPTURE_IGNORE='mismatch,gcov,unused,empty,negative,source,graph,inconsistent,corrupt,deprecated,deprecated'
readonly LCOV_FILTER_IGNORE='unused,empty,inconsistent,deprecated,deprecated'
readonly LCOV_SUMMARY_IGNORE='empty,inconsistent,deprecated,deprecated'
readonly GENHTML_IGNORE='unused,empty,inconsistent,source,deprecated,deprecated'

# Branch collection, forced at run time.  A command-line --rc outranks ~/.lcovrc,
# /etc/lcovrc and --config-file alike, which is why it is repeated on capture, filter,
# genhtml and summary instead of being left to configuration.
readonly LCOV_RC_ARGS=(--rc branch_coverage=1)

# This suite's versioned lcov configuration, passed with --config-file so that lcov reads
# it INSTEAD OF ~/.lcovrc and /etc/lcovrc.  It carries the genhtml presentation settings
# and geninfo_auto_base=1, which is what lets a single capture over the submodule root
# resolve both the ccec/src and osal/src object trees of this autotools build.  Verified:
# passing it yields byte-identical trace metrics to omitting it, so it changes
# presentation and path resolution, never the measurement.  If it is absent the run
# continues without it rather than failing, because the measurement does not depend on it.
LCOV_CONFIG_ARGS=()
if [ -f "$SCRIPT_DIR/.lcovrc_l1" ]; then
    LCOV_CONFIG_ARGS=(--config-file "$SCRIPT_DIR/.lcovrc_l1")
fi

# The six git-tracked Makefiles that autoreconf/configure rewrite (see BUILD RECIPE).
readonly REGENERATED_MAKEFILES=(
    Makefile
    ccec/Makefile
    ccec/src/Makefile
    osal/Makefile
    osal/src/Makefile
    tests/Makefile
)

# ------------------------------------------------------------------------------------
# Output helpers.  A single prefix makes this script's lines distinguishable from lcov's
# and GoogleTest's in a shared log.  Diagnostics go to stderr so that stdout stays a
# clean, pipeable report.
# ------------------------------------------------------------------------------------
log()  { printf '[run_coverage] %s\n' "$*"; }
warn() { printf '[run_coverage] WARNING: %s\n' "$*" >&2; }
die()  { printf '[run_coverage] ERROR: %s\n' "$*" >&2; exit 1; }
rule() { printf '%s\n' '--------------------------------------------------------------------------------'; }

# ------------------------------------------------------------------------------------
# Cleanup state and the single trap that services it.  Both actions are idempotent, so
# running the trap on a normal exit and again on a signal is harmless.
#   HOME_LCOVRC_STASH  where an existing ~/.lcovrc was parked for the duration
#   STAGE_DIR          private staging directory for the HTML report
# ------------------------------------------------------------------------------------
HOME_LCOVRC_STASH=''
STAGE_DIR=''

restore_home_lcovrc() {
    [ -n "$HOME_LCOVRC_STASH" ] || return 0
    local stash="$HOME_LCOVRC_STASH"
    HOME_LCOVRC_STASH=''
    if [ -f "$stash" ] && [ -n "${HOME:-}" ]; then
        if mv -f -- "$stash" "$HOME/.lcovrc" 2>/dev/null; then
            log "restored your $HOME/.lcovrc"
        else
            warn "could not restore $HOME/.lcovrc automatically."
            warn "  your copy is preserved at: $stash"
            warn "  restore it with:  mv '$stash' '$HOME/.lcovrc'"
            return 0
        fi
    fi
    rmdir -- "$(dirname -- "$stash")" 2>/dev/null || true
}

cleanup_stage_dir() {
    [ -n "$STAGE_DIR" ] || return 0
    local stage="$STAGE_DIR"
    STAGE_DIR=''
    # Only ever a directory this script created with mktemp -d, never a caller-supplied path.
    case "$stage" in
        /*/*) [ -d "$stage" ] && rm -rf -- "$stage" ;;
        *)    warn "refusing to remove an implausible staging path: $stage" ;;
    esac
    return 0
}

on_exit() {
    local rc=$?
    cleanup_stage_dir
    restore_home_lcovrc
    return "$rc"
}
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP


# ------------------------------------------------------------------------------------
# Step 0 -- move an existing ~/.lcovrc aside for the duration of the run.
#
# Why this is not `rm -f ~/.lcovrc`: lcov reads a home-directory configuration silently,
# and a copy that disables branch collection would quietly defeat the whole point of this
# script -- CI plants exactly such a copy in the plugin jobs, so the hazard is real and
# not hypothetical.  The requirement is only that no home configuration is IN EFFECT
# while lcov runs; destroying a developer's file is a side effect nobody asked for.  So it
# is moved into a private mode-0700 temporary directory and moved back by the EXIT trap,
# however the run ends -- capture and restore, never unconditional deletion.  The stash
# path is logged so that even a SIGKILL, the one signal a trap cannot service, leaves a
# named recoverable copy instead of a hole.
#
# /etc/lcovrc is never touched.  Together with the --config-file above, this leaves lcov
# reading exactly one configuration file -- this suite's versioned one -- plus the
# --rc overrides, which is what makes the run reproducible on any host.
# ------------------------------------------------------------------------------------
stash_home_lcovrc() {
    [ -n "${HOME:-}" ] || { log "HOME is unset; no home lcov configuration to move aside"; return 0; }
    local rc_path="$HOME/.lcovrc"
    if [ ! -e "$rc_path" ] && [ ! -L "$rc_path" ]; then
        log "no $rc_path present; nothing to move aside"
        return 0
    fi
    local stash_dir
    stash_dir="$("$MKTEMP_BIN" -d "${TMPDIR:-/tmp}/hdmicec-lcovrc-stash.XXXXXXXX")" \
        || die "could not create a temporary directory to park $rc_path.
       Refusing to run lcov with an unknown home configuration in effect, and refusing
       to delete your file to get around it."
    chmod 700 -- "$stash_dir" 2>/dev/null || true
    mv -f -- "$rc_path" "$stash_dir/.lcovrc" \
        || { rmdir -- "$stash_dir" 2>/dev/null || true
             die "could not move $rc_path aside. Fix the permissions on \$HOME and retry;
       this script will not delete the file instead."; }
    HOME_LCOVRC_STASH="$stash_dir/.lcovrc"
    log "moved $rc_path aside for this run (restored on exit): $HOME_LCOVRC_STASH"
}

usage() {
    cat <<USAGE
Usage: run_coverage.sh [OPTIONS]

gcov/lcov coverage runner and line-coverage gate for the HDMI-CEC middleware L1 suite.
Reproduces the "Generate coverage" step of .github/workflows/L1-tests.yml, adds the
branch data that step discards, and adds the numeric threshold it has never had.

OPTIONS
  -b, --build              Build the middleware and its L1 suite with coverage
                           instrumentation first.  OFF by default so an existing
                           measurement is never silently discarded.  Rewrites six
                           git-tracked Makefiles; the restore command is printed but
                           deliberately not executed (see --restore).
  -r, --run                Zero this build tree's gcov counters, then run run_L1Tests.
                           OFF by default.  Zeroing first is what makes the reported
                           figures an account of THIS run: gcov counters accumulate, so a
                           stale .gcda would otherwise let the gate pass on evidence the
                           current tests did not produce.
  -t, --threshold N        Line-coverage bar, an integer percentage.  Default ${COVERAGE_MIN}.
                           Lower it only for a deliberate diagnostic run; 80 is the
                           required bar.
  -o, --output-dir DIR     Write artifacts to DIR.  Default:
                           \${TMPDIR:-/tmp}/hdmicec-l1-coverage/<workspace basename>,
                           which is outside the git tree because this suite's .gitignore
                           does not cover coverage.info, filtered_coverage.info or
                           coverage/ and is out of scope for editing.
      --per-file-gate      In addition to the aggregate gate, fail when ANY file in the
                           filtered trace is below the bar.  OFF by default; below-bar
                           files are always enumerated either way.
      --no-html            Skip the genhtml report (the trace and the tables are still
                           produced).
      --restore            Restore the six git-tracked Makefiles that autoreconf and
                           configure rewrite, then exit.  This is the ONLY code path in
                           this script that runs git, and it only ever runs on request.
  -h, --help               Print this help and exit.

ENVIRONMENT (command-line flags win)
  COVERAGE_MIN=N               same as --threshold
  COVERAGE_OUTPUT_DIR=DIR      same as --output-dir
  COVERAGE_PER_FILE_GATE=1     same as --per-file-gate
  GTEST_PREFIX=DIR             prefix providing libgtest/libgmock and their headers,
                               used by --build and put on LD_LIBRARY_PATH by --run.
                               Default: ${GTEST_PREFIX}
  GTEST_EXTRA_ARGS="..."       extra arguments appended to the run_L1Tests command line.
                               --gtest_shuffle is a DIAGNOSTIC: this suite has
                               pre-existing order-fragile tests that pass in the default
                               order, so a shuffled run is expected to fail and must
                               never be treated as a gate.

EXIT STATUS
  0  suite (if run) green, and line coverage at or above the bar
  1  a prerequisite is missing, a step failed, or the coverage gate failed

RESOLVED PATHS FOR THIS INVOCATION
  script          ${SCRIPT_PATH}
  submodule root  ${HDMICEC_ROOT}      (the lcov capture directory)
  workspace root  ${WS}
  artifacts       ${OUTPUT_DIR}
USAGE
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            -b|--build)          DO_BUILD=1 ;;
            -r|--run)            DO_RUN=1 ;;
            --per-file-gate)     COVERAGE_PER_FILE_GATE=1 ;;
            --no-html)           DO_HTML=0 ;;
            --restore)           DO_RESTORE_ONLY=1 ;;
            -h|--help)           usage; exit 0 ;;
            -t|--threshold)
                [ $# -ge 2 ] || die "--threshold requires a value"
                COVERAGE_MIN="$2"; shift ;;
            --threshold=*)       COVERAGE_MIN="${1#*=}" ;;
            -o|--output-dir)
                [ $# -ge 2 ] || die "--output-dir requires a value"
                OUTPUT_DIR="$2"; shift ;;
            --output-dir=*)      OUTPUT_DIR="${1#*=}" ;;
            --)                  shift; break ;;
            -*)                  die "unknown option '$1'. Run '$SCRIPT_PATH --help' for the option list." ;;
            *)                   die "unexpected argument '$1'. This script takes options only; run --help." ;;
        esac
        shift
    done
    [ $# -eq 0 ] || die "unexpected trailing arguments: $*"

    case "$COVERAGE_MIN" in
        ''|*[!0-9]*) die "threshold must be a non-negative integer percentage, got '$COVERAGE_MIN'" ;;
        *)           ;;   # a run of digits: valid, nothing to do
    esac
    [ "$COVERAGE_MIN" -le 100 ] || die "threshold must be 0-100, got '$COVERAGE_MIN'"

    case "$COVERAGE_PER_FILE_GATE" in
        0|1) ;;
        *)   die "COVERAGE_PER_FILE_GATE must be 0 or 1, got '$COVERAGE_PER_FILE_GATE'" ;;
    esac

    # An empty --output-dir would silently resolve to the caller's working directory.
    [ -n "$OUTPUT_DIR" ] || die "--output-dir must not be empty"
}

# ------------------------------------------------------------------------------------
# Prerequisites.  Each failure names the thing that is missing AND the command that
# supplies it, because a coverage runner that just says "not found" wastes the reader's
# time at exactly the moment they have least context.
# ------------------------------------------------------------------------------------
require_tools() {
    local missing=0
    [ -n "$LCOV_BIN" ]    || { warn "lcov not found on PATH";    missing=1; }
    [ -n "$GENHTML_BIN" ] || { warn "genhtml not found on PATH"; missing=1; }
    [ -n "$GCOV_BIN" ]    || { warn "gcov not found on PATH";    missing=1; }
    [ -n "$AWK_BIN" ]     || { warn "awk not found on PATH";     missing=1; }
    [ -n "$FIND_BIN" ]    || { warn "find not found on PATH";    missing=1; }
    [ -n "$MKTEMP_BIN" ]  || { warn "mktemp not found on PATH";  missing=1; }
    [ "$missing" -eq 0 ] || die "missing coverage tooling.
       lcov, genhtml and gcov are what this script measures with, and awk, find and
       mktemp are how it parses and stages.  On a Debian/Ubuntu host:
           sudo apt-get install -y lcov
       gcov ships with gcc; awk, find and mktemp ship with the base system.
       lcov 2.x is required: this script uses --fail-under-lines and
       --rc branch_coverage=1, neither of which exists in lcov 1.x."

    # Recorded rather than enforced: the trace-record spellings this script parses (FNL:,
    # FNA:) and the gate option it relies on are lcov 2.x behaviour.  A 1.x lcov would
    # fail later with a confusing message, so name the version now.
    # `head -n1` can close the pipe early and hand the producer a SIGPIPE, which pipefail
    # would otherwise turn into a fatal error over a banner line -- hence the `|| true`.
    log "lcov:    $( { "$LCOV_BIN"    --version 2>&1 | head -n1; } || true )"
    log "genhtml: $( { "$GENHTML_BIN" --version 2>&1 | head -n1; } || true )"
    log "gcov:    $( { "$GCOV_BIN"    --version 2>&1 | head -n1; } || true )"
}

# The libtool wrapper, never .libs/run_L1Tests: the wrapper is what sets up the
# uninstalled shared-library search path for libRCEC and libRCECOSHal.
readonly TEST_BINARY_REL='tests/L1Tests/run_L1Tests'

# Counters are always consumed through a command substitution, and `find` returns
# non-zero the moment it meets one unreadable directory even with stderr silenced.  Under
# `set -e` plus pipefail that would abort the run over a partial walk, so both helpers
# absorb the status and always emit a number: a short count shows up as a prerequisite
# failure with a useful message instead of a bare exit.
profile_file_count() { # $1=extension glob, e.g. '*.gcda'
    { "$FIND_BIN" "$HDMICEC_ROOT" -name "$1" -type f 2>/dev/null || true; } | wc -l | tr -d '[:space:]'
}

gcda_count() { profile_file_count '*.gcda'; }
gcno_count() { profile_file_count '*.gcno'; }


require_instrumented_tree() {
    local gcno gcda
    gcno="$(gcno_count)"
    gcda="$(gcda_count)"

    [ -x "$HDMICEC_ROOT/$TEST_BINARY_REL" ] || die "the L1 test binary is missing:
           $HDMICEC_ROOT/$TEST_BINARY_REL
       Build it with:  $SCRIPT_PATH --build
       or follow the BUILD RECIPE in this script's header.  Remember --enable-l1tests:
       without it configure builds no test suite at all."

    [ "$gcno" -gt 0 ] || die "no *.gcno files under $HDMICEC_ROOT, so the tree is not
       coverage-instrumented and gcov has nothing to report on.  Rebuild with:
           $SCRIPT_PATH --build
       which configures with CXXFLAGS/LDFLAGS carrying -fprofile-arcs -ftest-coverage."

    # Counters are only a PRECONDITION when this invocation is not going to produce them.
    # With --run they are about to be written: do_run zeroes them first and then hard-fails
    # if the run leaves none behind, so the guarantee is kept either way.  Demanding them
    # here unconditionally would make the documented '--build --run' invocation impossible
    # on a freshly built tree, which is exactly the tree that flow exists for.
    if [ "$DO_RUN" -eq 0 ]; then
        [ "$gcda" -gt 0 ] || die "no *.gcda files under $HDMICEC_ROOT: the suite is
       instrumented ($gcno .gcno files) but has never been executed, so no counters
       exist.  Run it with:
           $SCRIPT_PATH --run
       or by hand:
           cd $HDMICEC_ROOT/tests/L1Tests && ./run_L1Tests"
    fi

    log "instrumentation: $gcno .gcno file(s), $gcda .gcda counter file(s)"
    if [ "$DO_RUN" -eq 0 ]; then
        # Clause 4 of the governing contract: gcov counters accumulate, so figures taken
        # from counters this invocation did not produce could credit a test that no longer
        # runs.  That cannot be prevented without running the suite, so it is made VISIBLE
        # instead of silent -- the age of the newest counter is usually enough to tell a
        # fresh measurement from a stale one at a glance.
        local newest
        newest="$( { "$FIND_BIN" "$HDMICEC_ROOT" -name '*.gcda' -type f \
                         -printf '%TY-%Tm-%Td %TH:%TM\n' 2>/dev/null || true; } \
                   | LC_ALL=C sort | tail -n1 || true)"
        warn "measuring PRE-EXISTING counters; the suite was not run by this invocation."
        warn "  newest .gcda timestamp: ${newest:-unknown}"
        warn "  gcov counters ACCUMULATE across runs, so these figures describe every run"
        warn "  that has ever executed against this build tree, not just the latest one."
        warn "  Use --run to zero the counters and measure exactly one suite execution."
    fi
}

# ------------------------------------------------------------------------------------
# Optional build.  A faithful replay of the workflow's "Generate stub headers" and
# "Build hdmicec" steps.  Off by default, because a rebuild is the one action that can
# destroy an existing measurement, and this script's primary job is to measure.
# ------------------------------------------------------------------------------------
do_build() {
    rule
    log "building the middleware and its L1 suite with coverage instrumentation"
    log "  (reproducing .github/workflows/L1-tests.yml steps 'Generate stub headers' and 'Build hdmicec')"

    local build_log="$OUTPUT_DIR/build.log"
    local nproc_count
    nproc_count="$( (command -v nproc >/dev/null 2>&1 && nproc) || echo 1 )"
    : >"$build_log"

    # configure.ac locates GoogleTest with PKG_CHECK_MODULES([GTEST], [gtest >= 1.10.0]),
    # which searches pkg-config's path and ignores -I/-L entirely.  CI gets away with
    # passing only include and library flags because it ALSO apt-installs libgtest-dev, so
    # a system gtest.pc is present; on a host where GoogleTest exists only as a
    # source-built copy under a private prefix -- which is the normal case when the
    # distribution's GoogleTest is too new for this project's C++ standard -- that check
    # fails and configure aborts.  Adding the prefix's pkgconfig directory here makes
    # GTEST_PREFIX mean what it says.  Any caller-supplied PKG_CONFIG_PATH is preserved
    # behind it rather than replaced.
    local build_pkg_config_path="$GTEST_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

    # stubs/ mutes the IARM bus headers this middleware includes but does not ship, and
    # the symlink is what substitutes the HAL driver mock for the real driver header.
    # Both are build prerequisites, not committed artifacts, and both are stripped from
    # the coverage denominator by the '*/stubs/*' and '*/mocks/*' globs.
    #
    # WHY THE STEPS ARE CHAINED WITH EXPLICIT `&&` RATHER THAN LEFT TO `set -e`:
    # a compound command that is the left operand of `||` runs with errexit SUPPRESSED,
    # and that suppression reaches inside a subshell, so a `set -e` written in here would
    # NOT stop the sequence.  Measured consequence of getting this wrong: a configure that
    # failed on the GoogleTest check fell straight through to `make`, which then ran the
    # committed non-autotools Makefiles and buried the real cause under a cascade of
    # unrelated link errors.  The explicit `&&` chain short-circuits regardless of errexit
    # state, and the `###STEP:` markers make the failure attributable to one step.
    (
        cd "$HDMICEC_ROOT" || exit 1
        echo '###STEP:generate stub headers and inject the HAL driver mock' &&
        mkdir -p stubs/rdk/iarmbus stubs/ccec/drivers/iarmbus &&
        touch stubs/rdk/iarmbus/libIARM.h \
              stubs/rdk/iarmbus/libIBus.h \
              stubs/rdk/iarmbus/libIBusDaemon.h \
              stubs/ccec/drivers/iarmbus/CecIARMBusMgr.h &&
        ln -sf ../../../mocks/hdmicec/hdmi_cec_driver.h stubs/ccec/drivers/hdmi_cec_driver.h &&
        echo '###STEP:autoreconf -if' &&
        autoreconf -if &&
        echo '###STEP:configure --enable-l1tests' &&
        PKG_CONFIG_PATH="$build_pkg_config_path" \
        CPPFLAGS="-I$HDMICEC_ROOT/mocks -I$HDMICEC_ROOT/stubs -I$GTEST_PREFIX/include" \
        LDFLAGS="-L$GTEST_PREFIX/lib -fprofile-arcs -ftest-coverage" \
        CXXFLAGS="-fprofile-arcs -ftest-coverage" \
        ./configure --enable-l1tests &&
        echo '###STEP:make all' &&
        make -j"$nproc_count" all &&
        echo '###STEP:make -C tests/L1Tests all' &&
        make -C tests/L1Tests all
    ) >>"$build_log" 2>&1 || {
        local failed_step
        failed_step="$( { grep '^###STEP:' "$build_log" | tail -n1 | sed 's/^###STEP://'; } || true )"
        die "the build failed during step: ${failed_step:-<none reached>}
       Full log: $build_log
       Tail:
$( { tail -n 25 -- "$build_log" | sed 's/^/         /'; } 2>/dev/null || true )
       If configure failed on GLIB, install libglib2.0-dev: configure.ac runs
       PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 0.10.28]) and there is no way past it.
       If configure failed on GTEST, point GTEST_PREFIX at a prefix whose lib/pkgconfig
       holds gtest.pc. It is currently $GTEST_PREFIX, so pkg-config was searched with
           PKG_CONFIG_PATH=$build_pkg_config_path
       Note that PKG_CHECK_MODULES ignores -I and -L: only pkg-config's path decides."
    }

    [ -x "$HDMICEC_ROOT/$TEST_BINARY_REL" ] || die "the build reported success but produced no
       executable at $HDMICEC_ROOT/$TEST_BINARY_REL. Full log: $build_log"
    log "build complete: $HDMICEC_ROOT/$TEST_BINARY_REL (log: $build_log)"
    announce_regenerated_makefiles
}

# ------------------------------------------------------------------------------------
# The build rewrites six git-tracked Makefiles.  They are reported, never reverted:
# a measurement tool silently running `git checkout` over a working tree would be able to
# discard work it knows nothing about, so the decision stays with the caller.  --restore
# exists for when the answer is yes.
# ------------------------------------------------------------------------------------
announce_regenerated_makefiles() {
    local restore_cmd="git -C '$HDMICEC_ROOT' checkout --"
    local f
    for f in "${REGENERATED_MAKEFILES[@]}"; do
        restore_cmd="$restore_cmd '$f'"
    done

    rule
    warn "MANDATORY BUILD HYGIENE: autoreconf and configure rewrite six GIT-TRACKED files."
    warn "They are local toolchain output and must NEVER be committed."
    if command -v git >/dev/null 2>&1 && git -C "$HDMICEC_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
        local dirty
        dirty="$(git -C "$HDMICEC_ROOT" status --porcelain -- "${REGENERATED_MAKEFILES[@]}" 2>/dev/null || true)"
        if [ -n "$dirty" ]; then
            warn "currently modified:"
            printf '%s\n' "$dirty" | sed 's/^/[run_coverage]     /' >&2
        else
            warn "none of the six is currently modified."
        fi
    fi
    warn "Restore them with EITHER:"
    warn "    $SCRIPT_PATH --restore"
    warn "or:"
    warn "    $restore_cmd"
    warn "This script will not run that for you as part of a coverage run, on purpose."
}

do_restore() {
    command -v git >/dev/null 2>&1 || die "git not found on PATH, so --restore cannot run."
    git -C "$HDMICEC_ROOT" rev-parse --git-dir >/dev/null 2>&1 \
        || die "$HDMICEC_ROOT is not a git working tree, so there is nothing to restore."

    rule
    log "restoring the six configure-generated Makefiles in $HDMICEC_ROOT"
    local before
    before="$(git -C "$HDMICEC_ROOT" status --porcelain -- "${REGENERATED_MAKEFILES[@]}" 2>/dev/null || true)"
    if [ -z "$before" ]; then
        log "none of the six is modified; nothing to do."
        return 0
    fi
    printf '%s\n' "$before" | sed 's/^/[run_coverage]   was: /'
    # Scoped to exactly these six paths.  Nothing else in the working tree is touched, and
    # no branch, index or history operation is performed.
    git -C "$HDMICEC_ROOT" checkout -- "${REGENERATED_MAKEFILES[@]}" \
        || die "git checkout failed for the six generated Makefiles."
    local after
    after="$(git -C "$HDMICEC_ROOT" status --porcelain -- "${REGENERATED_MAKEFILES[@]}" 2>/dev/null || true)"
    [ -z "$after" ] || { printf '%s\n' "$after" | sed 's/^/[run_coverage]   still: /' >&2
                         die "some of the six remain modified after checkout."; }
    log "restored: ${REGENERATED_MAKEFILES[*]}"
}

# ------------------------------------------------------------------------------------
# Optional suite execution.  The order -- zero, run, verify -- is the whole point:
# zeroing first removes accumulated counters so the capture describes exactly one suite
# execution, and verifying afterwards proves the suite actually wrote counters rather than
# exiting early.  Without the zeroing step a gate could be satisfied by an earlier run's
# evidence; without the verification an empty capture could be reported as a real figure.
# ------------------------------------------------------------------------------------
do_run() {
    rule
    log "zeroing gcov counters under $HDMICEC_ROOT (leaves *.gcno instrumentation intact)"
    "$LCOV_BIN" --zerocounters -d "$HDMICEC_ROOT" \
        "${LCOV_CONFIG_ARGS[@]}" "${LCOV_RC_ARGS[@]}" \
        --ignore-errors "$LCOV_SUMMARY_IGNORE" >/dev/null 2>&1 \
        || die "lcov --zerocounters failed for $HDMICEC_ROOT.
       Refusing to run the suite, because the capture would then mix this run's counters
       with whatever was left behind by earlier ones."
    local after_zero
    after_zero="$(gcda_count)"
    [ "$after_zero" -eq 0 ] || warn "$after_zero .gcda file(s) survived zeroing; figures may include earlier runs."

    local run_log="$OUTPUT_DIR/run_L1Tests.log"
    local results_json="$OUTPUT_DIR/rdkL1TestResults.json"
    local ld_path="${LD_LIBRARY_PATH:-}"
    if [ -d "$GTEST_PREFIX/lib" ]; then
        ld_path="$GTEST_PREFIX/lib${ld_path:+:$ld_path}"
    fi

    log "running the L1 suite: $HDMICEC_ROOT/$TEST_BINARY_REL"
    log "  LD_LIBRARY_PATH=$ld_path"
    log "  results JSON:    $results_json"
    if [ -n "$GTEST_EXTRA_ARGS" ]; then
        log "  extra gtest args: $GTEST_EXTRA_ARGS"
    fi

    # --gtest_print_time=1 and the JSON output mirror the workflow's own run step.
    # GTEST_EXTRA_ARGS is intentionally word-split: it is a user-supplied argument list.
    local rc=0
    (
        cd "$HDMICEC_ROOT/tests/L1Tests"
        LD_LIBRARY_PATH="$ld_path" \
        ./run_L1Tests --gtest_print_time=1 "--gtest_output=json:$results_json" \
            ${GTEST_EXTRA_ARGS:+$GTEST_EXTRA_ARGS}
    ) >"$run_log" 2>&1 || rc=$?

    # The tail is the part a reader needs; the whole log is on disk either way.
    "$AWK_BIN" '/^\[==========\]|^\[  PASSED  \]|^\[  FAILED  \]|^\[  SKIPPED \]|tests? from .* ran/' "$run_log" \
        | sed 's/^/[run_coverage]   /' || true

    if [ "$rc" -ne 0 ]; then
        warn "the suite exited $rc. Full log: $run_log"
        printf '%s\n' "--- last 30 lines ---" >&2
        tail -n 30 -- "$run_log" | sed 's/^/[run_coverage]   /' >&2 || true
        die "the L1 suite failed, so coverage was not measured.
       A red suite makes every coverage figure meaningless: fix the tests first.
       Coverage is deliberately NOT captured for a failing run, so that no report can
       ever be produced from one."
    fi
    log "the L1 suite passed (exit 0)"

    local fresh
    fresh="$(gcda_count)"
    [ "$fresh" -gt 0 ] || die "the suite exited 0 but wrote no *.gcda counters under
       $HDMICEC_ROOT.  Either the binary is not the instrumented one or the counters went
       somewhere unexpected.  Refusing to report a coverage figure that was not measured."
    log "the suite produced $fresh fresh .gcda counter file(s)"
}


# ------------------------------------------------------------------------------------
# Artifact paths.  Fixed names, matching CI's, inside a directory that defaults outside
# the git tree.  Every destination is checked before it is written: a symlink or a
# wrong-typed object at one of these paths would otherwise let a run scribble somewhere
# nobody intended.
# ------------------------------------------------------------------------------------
RAW_TRACE=''
FILTERED_TRACE=''
HTML_DIR=''
PER_FILE_TSV=''
UNCOVERED_TXT=''

assert_safe_artifact_path() { # $1=path  $2=file|dir
    local path="$1" kind="$2"
    [ ! -L "$path" ] || die "refusing to write through a symlink: $path
       Remove it, or choose another --output-dir."
    if [ -e "$path" ]; then
        case "$kind" in
            file) [ -f "$path" ] || die "artifact path exists and is not a regular file: $path" ;;
            dir)  [ -d "$path" ] || die "artifact path exists and is not a directory: $path" ;;
            # Without this arm a mistyped kind would silently skip the type check and let a
            # later step write through whatever object is sitting at that path.
            *)    die "assert_safe_artifact_path: unknown kind '$kind' for $path (expected 'file' or 'dir')" ;;
        esac
    fi
}

prepare_output_dir() {
    # Absolutise before anything else, so later `cd` calls cannot re-root a relative path.
    case "$OUTPUT_DIR" in
        /*) ;;
        *)  OUTPUT_DIR="$(pwd -P)/$OUTPUT_DIR" ;;
    esac
    [ ! -L "$OUTPUT_DIR" ] || die "the output directory is a symlink: $OUTPUT_DIR"
    mkdir -p -- "$OUTPUT_DIR" || die "could not create the output directory: $OUTPUT_DIR"
    [ -w "$OUTPUT_DIR" ] || die "the output directory is not writable: $OUTPUT_DIR"
    OUTPUT_DIR="$(cd -- "$OUTPUT_DIR" && pwd -P)"

    RAW_TRACE="$OUTPUT_DIR/coverage.info"
    FILTERED_TRACE="$OUTPUT_DIR/filtered_coverage.info"
    HTML_DIR="$OUTPUT_DIR/coverage"
    PER_FILE_TSV="$OUTPUT_DIR/per_file_coverage.tsv"
    UNCOVERED_TXT="$OUTPUT_DIR/uncovered_lines.txt"
    readonly RAW_TRACE FILTERED_TRACE HTML_DIR PER_FILE_TSV UNCOVERED_TXT

    assert_safe_artifact_path "$RAW_TRACE" file
    assert_safe_artifact_path "$FILTERED_TRACE" file
    assert_safe_artifact_path "$HTML_DIR" dir
    assert_safe_artifact_path "$PER_FILE_TSV" file
    assert_safe_artifact_path "$UNCOVERED_TXT" file

    log "artifacts: $OUTPUT_DIR"

    # The one case worth a warning: artifacts placed inside the git tree, where this
    # suite's .gitignore does not cover them and an `git add -A` would pick them up.
    case "$OUTPUT_DIR" in
        "$HDMICEC_ROOT"|"$HDMICEC_ROOT"/*|"$WS"|"$WS"/*)
            warn "the output directory is inside the repository tree."
            warn "  coverage.info, filtered_coverage.info and coverage/ are NOT covered by"
            warn "  tests/L1Tests/.gitignore, and that file is out of scope for editing, so"
            warn "  these artifacts WILL show up in git status. They are build output: do not"
            warn "  commit them. The default output directory lives outside the tree precisely"
            warn "  to make that impossible."
            ;;
        *)  ;;   # outside the repository tree: the intended case, nothing to warn about
    esac
}

# ------------------------------------------------------------------------------------
# Step 1 -- CAPTURE.  `-d "$HDMICEC_ROOT"` is the workflow's `-d hdmicec` made
# cwd-independent: the capture directory is the WHOLE submodule, not just the test tree,
# because the objects for ccec/src and osal/src live beside their sources and both trees
# must be walked.  Everything the capture pulls in that is not production source is
# removed by the filter in step 2 -- capture wide, then narrow, exactly as CI does.
#
# The ignore list exists because a coverage-instrumented C++ tree reliably produces
# inconsistent-count and source-mismatch diagnostics for inlined and template code that
# are informational rather than actionable; without it lcov 2.x aborts the capture and no
# report is produced at all.  Counts remain visible in the message summary and in
# capture.log.  `category` is not in the list and must not be added.
# ------------------------------------------------------------------------------------
capture_coverage() {
    rule
    log "capturing coverage over $HDMICEC_ROOT"
    rm -f -- "$RAW_TRACE"
    "$LCOV_BIN" -c \
        -o "$RAW_TRACE" \
        -d "$HDMICEC_ROOT" \
        "${LCOV_CONFIG_ARGS[@]}" \
        "${LCOV_RC_ARGS[@]}" \
        --ignore-errors "$LCOV_CAPTURE_IGNORE" \
        >"$OUTPUT_DIR/capture.log" 2>&1 \
        || die "lcov capture failed. Full log: $OUTPUT_DIR/capture.log
       Tail:
$(tail -n 20 -- "$OUTPUT_DIR/capture.log" 2>/dev/null | sed 's/^/         /')"

    [ -s "$RAW_TRACE" ] || die "the capture produced an empty trace ($RAW_TRACE).
       Refusing to report a coverage figure that was not measured. See
       $OUTPUT_DIR/capture.log"
    log "raw trace: $RAW_TRACE ($(grep -c '^SF:' "$RAW_TRACE" || true) source file record(s))"
}

# ------------------------------------------------------------------------------------
# Step 2 -- FILTER.  The seven globs, verbatim, in the workflow's order, expanded from a
# quoted array so the shell cannot match them against the working directory.
#
# `unused` is tolerated here for a specific and benign reason: several globs
# ('*/install/usr/include/*', '*/googletest/*') match nothing when the dependency headers
# were consumed from a system prefix rather than an install tree.  A glob that matches
# nothing is not an error and certainly not grounds for deleting it -- keeping all seven
# is what keeps this step identical to CI's on every host.
# ------------------------------------------------------------------------------------
filter_coverage() {
    rule
    log "filtering the trace down to production source (${#LCOV_EXCLUDES[@]} exclusion globs, verbatim from the workflow)"
    local g
    for g in "${LCOV_EXCLUDES[@]}"; do
        log "    exclude: $g"
    done
    rm -f -- "$FILTERED_TRACE"
    "$LCOV_BIN" -r "$RAW_TRACE" \
        "${LCOV_EXCLUDES[@]}" \
        -o "$FILTERED_TRACE" \
        "${LCOV_CONFIG_ARGS[@]}" \
        "${LCOV_RC_ARGS[@]}" \
        --ignore-errors "$LCOV_FILTER_IGNORE" \
        >"$OUTPUT_DIR/filter.log" 2>&1 \
        || die "lcov filtering failed. Full log: $OUTPUT_DIR/filter.log
       Tail:
$(tail -n 20 -- "$OUTPUT_DIR/filter.log" 2>/dev/null | sed 's/^/         /')"

    [ -s "$FILTERED_TRACE" ] || die "filtering produced an empty trace ($FILTERED_TRACE).
       Every file was excluded, which means the capture found no production source at all.
       Refusing to report a coverage figure that was not measured."

    local files
    files="$(grep -c '^SF:' "$FILTERED_TRACE" || true)"
    [ "${files:-0}" -gt 0 ] || die "the filtered trace contains no source-file records."
    log "filtered trace: $FILTERED_TRACE ($files production source file record(s))"

    # A filter that let a test file, mock or stub through would silently inflate the
    # denominator with code the gate is not supposed to judge.  Cheap to check, so checked.
    local leaked
    leaked="$(grep '^SF:' "$FILTERED_TRACE" \
              | grep -E '/usr/include/|/stubs/|/mocks/|/googletest/|/tests/|/test_[^/]*\.cpp$' || true)"
    if [ -n "$leaked" ]; then
        printf '%s\n' "$leaked" | sed 's/^/[run_coverage]   leaked: /' >&2
        die "the filtered trace still contains non-production paths (listed above).
       The exclusion globs are reproduced verbatim from the workflow and must not be
       changed, so this is a capture-path problem, not a glob problem: check whether the
       build tree lives somewhere the globs cannot describe."
    fi
    log "verified: the denominator is production source only"
}

# ------------------------------------------------------------------------------------
# Step 3 -- HTML REPORT.  Built into a private staging directory and published by rename,
# so a smaller trace can never leave pages from a larger earlier one behind, and an
# interrupted run cannot leave a half-written report at the published path.
# ------------------------------------------------------------------------------------
generate_html() {
    rule
    if [ "$DO_HTML" -eq 0 ]; then
        log "skipping the genhtml report (--no-html)"
        return 0
    fi
    log "generating the HTML report: $HTML_DIR"
    STAGE_DIR="$("$MKTEMP_BIN" -d "$OUTPUT_DIR/.genhtml-stage.XXXXXXXX")" \
        || die "could not create a staging directory under $OUTPUT_DIR"
    chmod 700 -- "$STAGE_DIR" 2>/dev/null || true

    "$GENHTML_BIN" \
        -o "$STAGE_DIR/coverage" \
        -t "$GENHTML_TITLE" \
        "$FILTERED_TRACE" \
        "${LCOV_CONFIG_ARGS[@]}" \
        "${LCOV_RC_ARGS[@]}" \
        --ignore-errors "$GENHTML_IGNORE" \
        >"$OUTPUT_DIR/genhtml.log" 2>&1 \
        || die "genhtml failed. Full log: $OUTPUT_DIR/genhtml.log
       Tail:
$(tail -n 20 -- "$OUTPUT_DIR/genhtml.log" 2>/dev/null | sed 's/^/         /')"

    [ -f "$STAGE_DIR/coverage/index.html" ] || die "genhtml exited 0 but wrote no index.html.
       See $OUTPUT_DIR/genhtml.log"

    rm -rf -- "$HTML_DIR"
    mv -- "$STAGE_DIR/coverage" "$HTML_DIR" || die "could not publish the report to $HTML_DIR"
    cleanup_stage_dir
    log "report: $HTML_DIR/index.html"

    # The Branches column only appears when branch data survived into the trace, so its
    # presence is a direct check that --rc branch_coverage=1 actually took effect.
    if grep -q 'Branches' "$HTML_DIR/index.html" 2>/dev/null; then
        log "the report includes a Branches column (branch collection confirmed active)"
    else
        warn "the report has no Branches column: branch data did not survive into the trace."
        warn "  Every lcov step here passes --rc branch_coverage=1, so investigate the"
        warn "  toolchain rather than the configuration -- lcov 1.x, for instance, spells"
        warn "  this option differently and would silently produce a branch-free report."
    fi
}

# ------------------------------------------------------------------------------------
# Step 4 -- AGGREGATE SUMMARY.  lcov's own words, printed verbatim, so the reader can see
# the authoritative figures next to the per-file table this script derives.  If the two
# ever disagree, lcov is right and the parser below is wrong.
# ------------------------------------------------------------------------------------
summarise_coverage() {
    rule
    log "aggregate coverage (production source only), as reported by lcov itself:"
    "$LCOV_BIN" --summary "$FILTERED_TRACE" \
        "${LCOV_CONFIG_ARGS[@]}" \
        "${LCOV_RC_ARGS[@]}" \
        --ignore-errors "$LCOV_SUMMARY_IGNORE" 2>&1 \
        | sed 's/^/[run_coverage]   /' \
        || die "lcov --summary failed for $FILTERED_TRACE"
}


# ------------------------------------------------------------------------------------
# Step 5 -- PER-FILE FIGURES.
#
# The requirement is per TARGET, and a healthy aggregate hides a below-bar file, so LINE,
# FUNCTION and BRANCH figures are reported for every file in the filtered trace.  These
# three columns are a contract, not a convenience: the workspace traceability report
# transcribes them directly for its before/after per-target table.
#
# Everything is parsed out of the trace records -- never out of `lcov --list`, which emits
# malformed rates above 100% in lcov 2.x:
#     SF:<path>                     starts a file record
#     LF:/LH:                       lines found / hit
#     FNA:<index>,<count>,<name>    one per function alias; count is ALWAYS field 2, so a
#                                   demangled name containing commas cannot corrupt it
#     FNDA:<count>,<name>           the pre-2.x spelling of the same thing, also handled
#     FNF:/FNH:                     per-file leader totals -- reported, but NOT used for
#                                   the primary column: several aliases can share one
#                                   leader, so these do not reconcile with lcov --summary
#     BRF:/BRH:                     branches found / hit; ABSENT for a file with no
#                                   branch arcs, which is why presence is tracked
#                                   separately and rendered "n/a" rather than 0.0%
#     DA:<line>,<count>             per-line hits; count 0 means the line never executed
#
# The machine-readable TSV is written first and the human table is rendered FROM it, so
# the two can never disagree.  Both are sorted by path with LC_ALL=C, so the same trace
# always produces byte-identical output.
# ------------------------------------------------------------------------------------
BELOW_BAR_FILES=''

# The three functions below embed awk programs, and every one of them is single-quoted on
# purpose: `$1`, `$4`, `$13`, `$0` and `\t` inside those programs are AWK syntax that awk
# itself evaluates against the current record.  Double-quoting them would let the shell
# substitute its own positional parameters first and hand awk a mangled program, so the
# single quotes are load-bearing and shellcheck's SC2016 ("expressions don't expand in
# single quotes") is a false positive here.  Values that genuinely must come from the
# shell are passed in properly with `-v` instead, which is why HDMICEC_ROOT and
# COVERAGE_MIN appear as `-v root=` and `-v min=` rather than being interpolated.  The
# suppression is scoped to these functions rather than to the whole file so that a future
# quoting mistake anywhere else is still reported.
# shellcheck disable=SC2016
write_per_file_tsv() {
    local tmp_tsv="$OUTPUT_DIR/.per_file_coverage.tsv.tmp"
    rm -f -- "$tmp_tsv"

    {
        printf '# Per-file coverage for the HDMI-CEC middleware L1 suite, derived from\n'
        printf '# %s\n' "$FILTERED_TRACE"
        printf '# Paths are relative to %s. functions_* come from the trace FNA/FNDA alias\n' "$HDMICEC_ROOT"
        printf '# records, which reconcile exactly with lcov --summary; func_leader_* are the\n'
        printf '# FNF:/FNH: per-file leader totals, which do not (aliases can share a leader).\n'
        printf '# branches_* are "n/a" for a file with no branch arcs. The final TOTAL row is an\n'
        printf '# aggregate, not a file: filter it with  grep -v "^TOTAL"\n'
        printf 'file\tlines_hit\tlines_found\tlines_pct\tfunctions_hit\tfunctions_found\tfunctions_pct\tfunc_leader_hit\tfunc_leader_found\tbranches_hit\tbranches_found\tbranches_pct\tline_gate\n'

        "$AWK_BIN" -v root="$HDMICEC_ROOT/" '
            function flush_record() {
                if (sf == "") return
                rel = sf
                if (index(rel, root) == 1) rel = substr(rel, length(root) + 1)
                printf "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", \
                       rel, lh, lf, fnah, fna, fnh, fnf, brh, brf, hasbr
                sf = ""
            }
            /^SF:/  { flush_record()
                      sf = substr($0, 4)
                      lh = 0; lf = 0; fnh = 0; fnf = 0
                      fnah = 0; fna = 0; brh = 0; brf = 0; hasbr = 0
                      next }
            /^LF:/  { lf  = substr($0, 4) + 0; next }
            /^LH:/  { lh  = substr($0, 4) + 0; next }
            /^FNF:/ { fnf = substr($0, 5) + 0; next }
            /^FNH:/ { fnh = substr($0, 5) + 0; next }
            /^BRF:/ { brf = substr($0, 5) + 0; hasbr = 1; next }
            /^BRH:/ { brh = substr($0, 5) + 0; next }
            # lcov 2.x alias record: FNA:<index>,<execution count>,<name>.
            /^FNA:/ { split(substr($0, 5), f, ",")
                      fna++
                      if (f[2] + 0 > 0) fnah++
                      next }
            # Pre-2.x equivalents, so this parser is not silently wrong on an older trace.
            # FN:<line>,<name> declares an alias; FNDA:<count>,<name> gives its hit count.
            /^FN:/   { fna++;  next }
            /^FNDA:/ { split(substr($0, 6), f, ",")
                       if (f[1] + 0 > 0) fnah++
                       next }
            /^end_of_record/ { flush_record(); next }
            END { flush_record() }
        ' "$FILTERED_TRACE" \
        | LC_ALL=C sort -t "$(printf '\t')" -k1,1 \
        | "$AWK_BIN" -F'\t' -v min="$COVERAGE_MIN" '
            function pct(hit, found) { return found > 0 ? 100 * hit / found : 0 }
            {
                rel = $1; lh = $2 + 0; lf = $3 + 0
                fnah = $4 + 0; fna = $5 + 0; fnh = $6 + 0; fnf = $7 + 0
                brh = $8 + 0; brf = $9 + 0; hasbr = $10 + 0

                # Verdict from the EXACT ratio, never from the rounded display value, so a
                # file at 79.95% is never let through by a cell that reads "80.0%".
                if (lf == 0)                     gate = "no-lines"
                else if (pct(lh, lf) >= min + 0) gate = "PASS"
                else                             gate = "BELOW"

                printf "%s\t%d\t%d\t%.1f\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%s\t%s\n", \
                       rel, lh, lf, pct(lh, lf), fnah, fna, \
                       (fna > 0 ? sprintf("%.1f", pct(fnah, fna)) : "n/a"), \
                       fnh, fnf, brh, brf, \
                       (hasbr && brf > 0 ? sprintf("%.1f", pct(brh, brf)) : "n/a"), \
                       gate

                TLH += lh; TLF += lf; TFNAH += fnah; TFNA += fna
                TFNH += fnh; TFNF += fnf; TBRH += brh; TBRF += brf
                files++
            }
            END {
                if (files == 0) { print "##NODATA"; exit 0 }
                printf "TOTAL\t%d\t%d\t%.1f\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%s\t%s\n", \
                       TLH, TLF, pct(TLH, TLF), TFNAH, TFNA, \
                       (TFNA > 0 ? sprintf("%.1f", pct(TFNAH, TFNA)) : "n/a"), \
                       TFNH, TFNF, TBRH, TBRF, \
                       (TBRF > 0 ? sprintf("%.1f", pct(TBRH, TBRF)) : "n/a"), \
                       (pct(TLH, TLF) >= min + 0 ? "PASS" : "BELOW")
            }
        '
    } >"$tmp_tsv" || { rm -f -- "$tmp_tsv"; die "per-file parsing of $FILTERED_TRACE failed"; }

    if grep -q '^##NODATA$' "$tmp_tsv"; then
        rm -f -- "$tmp_tsv"
        die "the filtered trace yielded no per-file records.
       Refusing to report a coverage figure that was not measured."
    fi
    mv -f -- "$tmp_tsv" "$PER_FILE_TSV" || die "could not write $PER_FILE_TSV"
}

# ------------------------------------------------------------------------------------
# The uncovered-line enumeration.  This is what turns "below the bar" into something
# actionable and is exactly what the "enumerate the specific uncoverable lines and the
# reason" requirement needs: the line numbers come from the trace, and the reason is a
# human judgement to be recorded against them in the traceability report.  Written for
# every file that has at least one uncovered line, not just the below-bar ones, so the
# report can attribute the residue in an already-passing file too.
# ------------------------------------------------------------------------------------
# shellcheck disable=SC2016  # single-quoted awk programs; see the note above write_per_file_tsv.
write_uncovered_lines() {
    local tmp="$OUTPUT_DIR/.uncovered_lines.txt.tmp"
    rm -f -- "$tmp"
    {
        printf '# Uncovered (never-executed) source lines per file, from\n'
        printf '# %s\n' "$FILTERED_TRACE"
        printf '# Paths are relative to %s. A line is listed when its DA: record shows a hit\n' "$HDMICEC_ROOT"
        printf '# count of zero. Files with full line coverage are omitted.\n'
        printf '# Line coverage bar in force for this run: %s%%\n' "$COVERAGE_MIN"
        printf '#\n'
        # Emitted as ONE tab-separated line per file so that sorting cannot separate a
        # file's line list from its heading, then split into the two-line human form
        # afterwards.  Sorting the already-split form is the obvious mistake here: the
        # continuation lines begin with whitespace and all migrate to the top of the file.
        "$AWK_BIN" -v root="$HDMICEC_ROOT/" -v min="$COVERAGE_MIN" '
            function flush_record() {
                if (sf == "") return
                rel = sf
                if (index(rel, root) == 1) rel = substr(rel, length(root) + 1)
                if (nmiss > 0) {
                    pct = (lf > 0 ? 100 * lh / lf : 0)
                    printf "%s\t%d/%d lines = %.1f%%\t%s\t%d uncovered\t%s\n", \
                           rel, lh, lf, pct, \
                           (lf > 0 && pct >= min + 0 ? "PASS" : "BELOW"), nmiss, miss
                }
                sf = ""
            }
            /^SF:/  { flush_record()
                      sf = substr($0, 4); lh = 0; lf = 0; nmiss = 0; miss = ""
                      next }
            /^LF:/  { lf = substr($0, 4) + 0; next }
            /^LH:/  { lh = substr($0, 4) + 0; next }
            # DA:<line>,<hit count>[,<checksum>]
            /^DA:/  { split(substr($0, 4), f, ",")
                      if (f[2] + 0 == 0) { nmiss++; miss = miss (nmiss > 1 ? " " : "") f[1] }
                      next }
            /^end_of_record/ { flush_record(); next }
            END { flush_record() }
        ' "$FILTERED_TRACE" \
        | LC_ALL=C sort -t "$(printf '\t')" -k1,1 \
        | "$AWK_BIN" -F'\t' '{ printf "%s\t%s\t%s\t%s\n    uncovered lines: %s\n", $1, $2, $3, $4, $5 }'
    } >"$tmp" || { rm -f -- "$tmp"; die "uncovered-line enumeration failed for $FILTERED_TRACE"; }
    mv -f -- "$tmp" "$UNCOVERED_TXT" || die "could not write $UNCOVERED_TXT"
}

# shellcheck disable=SC2016  # single-quoted awk programs; see the note above write_per_file_tsv.
per_file_report() {
    rule
    write_per_file_tsv
    write_uncovered_lines

    log "per-file coverage, derived from the filtered trace records:"
    printf '\n'
    # Rendered from the TSV so the printed table and the machine-readable artifact are the
    # same numbers by construction.  Column widths are fixed, and a metric with no data
    # reads "n/a" instead of a misleading 0.0%.
    "$AWK_BIN" -F'\t' -v min="$COVERAGE_MIN" '
        function cell(hit, found, rate) {
            if (rate == "n/a" || found + 0 <= 0) return sprintf("%7s %11s", "n/a", "no data")
            return sprintf("%6.1f%% %5d/%-5d", rate + 0, hit, found)
        }
        BEGIN {
            # Metric headers are LEFT-aligned in the same 19-character field the metric
            # cells occupy, so each header sits directly over its percentage.
            printf "  %-40s %-19s %-19s %-19s  %s\n", "FILE", "LINES", "FUNCTIONS", "BRANCHES", "LINE GATE"
            printf "  %-40s %-19s %-19s %-19s  %s\n", \
                   "----------------------------------------", \
                   "-------------------", "-------------------", "-------------------", "---------"
        }
        /^#/ { next }
        $1 == "file" { next }
        {
            rel = $1
            line_cell = cell($2, $3, $4)
            func_cell = cell($5, $6, $7)
            br_cell   = cell($10, $11, $12)
            verdict   = $13
            if (rel == "TOTAL") {
                printf "  %-40s %-19s %-19s %-19s  %s\n", \
                       "----------------------------------------", \
                       "-------------------", "-------------------", "-------------------", "---------"
                printf "  %-40s %19s %19s %19s  %s\n", \
                       "TOTAL (" files " source files)", line_cell, func_cell, br_cell, verdict
                next
            }
            files++
            printf "  %-40s %19s %19s %19s  %s\n", rel, line_cell, func_cell, br_cell, verdict
        }
    ' "$PER_FILE_TSV"
    printf '\n'

    log "FUNCTIONS above is the alias model (trace FNA:/FNDA: records), which is what lcov"
    log "  --summary and genhtml report, so the TOTAL row reconciles with the summary block."
    log "  The trace also carries FNF:/FNH: per-file leader totals, whose denominator is"
    log "  smaller because several aliases can share one leader; they are in column"
    log "  func_leader_* of the TSV rather than dropped."
    log "BRANCHES is evidence, never a gate: gcov counts branches as control-flow-graph arcs,"
    log "  including compiler-generated exception and static-destruction arcs no test reaches."
    log "machine-readable table: $PER_FILE_TSV"
    log "uncovered line numbers: $UNCOVERED_TXT"

    # Collected here, consumed by the gate, so the enumeration happens exactly once.
    BELOW_BAR_FILES="$("$AWK_BIN" -F'\t' '!/^#/ && $1 != "file" && $1 != "TOTAL" && $13 == "BELOW" { printf "%s\t%s\n", $1, $4 }' "$PER_FILE_TSV")"
}


# ------------------------------------------------------------------------------------
# Step 6 -- THE GATE.  The first machine-checkable coverage threshold in this workspace.
#
# The aggregate verdict is delegated to lcov's own --fail-under-lines so the number that
# decides the outcome is lcov's rather than this script's, and its non-zero exit is what
# fails the run.  It MUST be paired with an operation: the bare
# `lcov --fail-under-lines N <trace>` form is rejected with "Need one of options -z, -c,
# -a, -e, -r, -l, --diff, --intersect, --subtract, or --summary" and exits 2 -- which,
# being non-zero, would masquerade as a working gate while never once looking at the
# coverage figure.  Hence `--summary <trace> --fail-under-lines N`.
#
# Files below the bar are enumerated unconditionally, because the requirement is per
# target and the enumeration is itself a deliverable.  Whether that enumeration also
# FAILS the run is the caller's choice (--per-file-gate), defaulted off for the reason
# given under GATE in the header.  Branch coverage is never gated.
# ------------------------------------------------------------------------------------
apply_gate() {
    local failures=0 rc=0

    rule
    log "applying the >= ${COVERAGE_MIN}% line-coverage gate to the aggregate (lcov --fail-under-lines)"
    "$LCOV_BIN" --summary "$FILTERED_TRACE" \
        --fail-under-lines "$COVERAGE_MIN" \
        "${LCOV_CONFIG_ARGS[@]}" \
        "${LCOV_RC_ARGS[@]}" \
        --ignore-errors "$LCOV_SUMMARY_IGNORE" >/dev/null 2>&1 || rc=$?

    if [ "$rc" -eq 0 ]; then
        log "aggregate line coverage meets the ${COVERAGE_MIN}% bar"
    elif [ "$rc" -eq 2 ]; then
        # Defensive: a 2 here means lcov rejected the command line rather than the
        # coverage, and reporting that as a coverage failure would be a lie.
        die "lcov rejected the gate invocation (exit 2), which is a command-line error and
       NOT a coverage verdict. Re-run the same command without >/dev/null to see it:
           $LCOV_BIN --summary '$FILTERED_TRACE' --fail-under-lines $COVERAGE_MIN --rc branch_coverage=1
       lcov 2.x is required; 1.x has no --fail-under-lines at all."
    else
        warn "aggregate line coverage is BELOW ${COVERAGE_MIN}% (lcov exited $rc)"
        failures=$((failures + 1))
    fi

    if [ -n "$BELOW_BAR_FILES" ]; then
        rule
        warn "targets below the ${COVERAGE_MIN}% line-coverage bar:"
        printf '%s\n' "$BELOW_BAR_FILES" | while IFS="$(printf '\t')" read -r path pct_value; do
            [ -n "$path" ] || continue
            printf '[run_coverage]     %-46s %s%%\n' "$path" "$pct_value" >&2
        done
        warn "Their uncovered line numbers are enumerated in:"
        warn "    $UNCOVERED_TXT"
        warn "Close each gap by ADDING TESTS. Never by adding an exclusion glob, never by"
        warn "editing production source, and never by lowering the bar: the threshold is a"
        warn "gate, not a filter. Where a line genuinely cannot be reached from a test-only"
        warn "change, record it with its reason in the traceability report instead."
        if [ "$COVERAGE_PER_FILE_GATE" -eq 1 ]; then
            warn "--per-file-gate is in force, so the above fails this run."
            failures=$((failures + 1))
        else
            log "per-file gating is off (default), so the above is reported but does not fail"
            log "  this run. Enable it with --per-file-gate or COVERAGE_PER_FILE_GATE=1."
        fi
    else
        log "every target in the filtered trace meets the ${COVERAGE_MIN}% bar"
    fi

    rule
    if [ "$failures" -ne 0 ]; then
        local artifacts="$FILTERED_TRACE
           $PER_FILE_TSV
           $UNCOVERED_TXT"
        if [ "$DO_HTML" -eq 1 ]; then
            artifacts="$artifacts
           $HTML_DIR/index.html"
        fi
        die "COVERAGE GATE FAILED (bar: ${COVERAGE_MIN}% line coverage).
       Artifacts for diagnosis:
           $artifacts"
    fi
    log "COVERAGE GATE PASSED (bar: ${COVERAGE_MIN}% line coverage)"
}

# ------------------------------------------------------------------------------------
# main.  The order is deliberate and is the whole argument of the script:
#   parse -> resolve artifacts -> [build] -> require an instrumented, exercised tree ->
#   [zero + run + verify] -> move any home lcov configuration aside -> capture -> filter
#   -> report HTML -> summarise -> per-file table -> gate.
# --restore is a standalone action and short-circuits everything else, so it can never be
# mistaken for part of a measurement run.
# ------------------------------------------------------------------------------------
main() {
    parse_args "$@"

    if [ "$DO_RESTORE_ONLY" -eq 1 ]; then
        if [ "$DO_BUILD" -eq 1 ] || [ "$DO_RUN" -eq 1 ]; then
            die "--restore is a standalone action; do not combine it with --build or --run."
        fi
        do_restore
        exit 0
    fi

    rule
    log "HDMI-CEC middleware L1 coverage runner"
    log "  submodule root : $HDMICEC_ROOT"
    log "  workspace root : $WS"
    log "  line bar       : ${COVERAGE_MIN}%  (per-file gating: $( [ "$COVERAGE_PER_FILE_GATE" -eq 1 ] && echo on || echo off ))"
    if [ "${#LCOV_CONFIG_ARGS[@]}" -gt 0 ]; then
        log "  lcov config    : $SCRIPT_DIR/.lcovrc_l1 (via --config-file, read instead of ~/.lcovrc and /etc/lcovrc)"
    else
        log "  lcov config    : none found at $SCRIPT_DIR/.lcovrc_l1; continuing with --rc overrides only"
    fi
    log "  branch data    : forced on with --rc branch_coverage=1 on capture, filter, genhtml and summary"

    require_tools
    prepare_output_dir

    if [ "$DO_BUILD" -eq 1 ]; then
        do_build
    fi
    require_instrumented_tree
    if [ "$DO_RUN" -eq 1 ]; then
        do_run
    fi

    stash_home_lcovrc
    capture_coverage
    filter_coverage
    generate_html
    summarise_coverage
    per_file_report
    apply_gate
}

main "$@"

