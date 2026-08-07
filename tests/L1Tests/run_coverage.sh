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
#        absolute tool resolution, home-configuration isolation, awk trace parsing,
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
#          (a) $HOME is NOT one of them, and that is the point.  lcov reads
#              $HOME/.lcovrc silently, and a branch-disabled copy there would defeat
#              everything above -- CI plants exactly such a copy in the plugin jobs, so
#              the hazard is real.  This script does not move that file aside: it gives
#              every lcov and genhtml invocation a PRIVATE, EMPTY, mode-0700 HOME of its
#              own (see lcov_run/genhtml_run), so no home configuration can be in effect
#              and the caller's file is never opened, moved, replaced or deleted.
#              An earlier revision did stash and restore it, and that was wrong twice
#              over: the restoring `mv -f` would silently DESTROY a ~/.lcovrc that
#              reappeared during the run -- the owner's, or a concurrent sibling runner's
#              -- and a file recreated mid-run was in effect for every lcov call after
#              it.  A private HOME has neither failure mode and needs no trap to undo.
#              /etc/lcovrc is likewise never touched; --config-file below means lcov
#              reads exactly one configuration file, this suite's versioned one.
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
#        A ZERO EXIT STATUS IS NOT TAKEN AS PROOF THAT ANY TEST RAN.  GoogleTest exits 0
#        when a filter selects nothing, so `GTEST_EXTRA_ARGS=--gtest_filter=NoSuchTest.*`
#        used to be reported as "the L1 suite passed" and then measured at 0.0% -- a run
#        that failed only because the number happened to be zero, not because anything
#        asserted it had tested nothing.  A filter that still cleared the bar would have
#        been reported as a pass.  `--run` therefore DELETES the results file before the
#        binary starts and then requires it to exist, to report a non-zero test count, and
#        to name at least one of this suite's own fixtures -- the same three checks the
#        sibling plugin runners apply, for the same reason.
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
#      Verified: exit 0 at or above the bar, exit 1 below it.  This script adds a THIRD
#      status of its own, 3, for a measurement that is arithmetically at or above the bar
#      but is not evidence anyone should accept -- see ADVISORY VERDICT below.
#   1a. AN ADVISORY VERDICT IS NOT AN ACCEPTANCE VERDICT (exit 3).  Two situations produce
#      real numbers from evidence this invocation did not establish: measuring counters
#      without --run (gcov counters ACCUMULATE, so they may credit a test that no longer
#      runs), and any run whose provenance checks were bypassed.  Both used to end in
#      "COVERAGE GATE PASSED" and exit 0, which is indistinguishable from a clean
#      acceptance to any caller, human or CI.  They now end in "COVERAGE ADVISORY" and
#      exit 3: the figures and every artifact are still produced, and a caller that
#      requires an acceptance verdict fails on the status instead of being told a
#      reassuring lie.  Below the bar is still exit 1 in both modes.
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
#       mktemp -d "${TMPDIR:-/tmp}/hdmicec-l1-coverage.XXXXXXXX"
#
#   created atomically at mode 0700, with an UNPREDICTABLE name, and printed as the
#   "artifacts:" line when the run starts.  The name is unpredictable on purpose: a fixed
#   one under a world-writable $TMPDIR can be pre-created by any local account as a symlink
#   or as a directory it owns, and every artifact written afterwards would land where it
#   chose.  A fresh root per run also means a trace from an earlier run cannot be mistaken
#   for this one's -- the same staleness discipline the counters get.
#   Outside the tree they cannot be staged even by `git add -A`, and a per-run root keeps
#   parallel checkouts of this superproject from overwriting each other's evidence without
#   needing any environment variable.  The CI file NAMES are preserved
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
#   ./run_coverage.sh --no-per-file-gate    gate on the aggregate only (the default)
#   ./run_coverage.sh --output-dir DIR      write artifacts to DIR
#   ./run_coverage.sh --no-html             skip the genhtml report
#   ./run_coverage.sh --restore             restore the six tracked Makefiles, then exit
#   ./run_coverage.sh --help                full option and environment reference
#
#   ENVIRONMENT (all optional; command-line flags win)
#     COVERAGE_MIN            line-coverage bar, default 80
#     COVERAGE_OUTPUT_DIR     artifact directory, default as under ARTIFACT HYGIENE
#     COVERAGE_PER_FILE_GATE  1 to gate per file as well as on the aggregate, default 0
#                             (0 gates the aggregate only; the per-file enumeration is
#                             printed either way)
#     SUITE_TIMEOUT           wall-clock bound in seconds on the L1 suite under --run,
#                             default 900; exceeding it is reported as a hang, not a failure
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
#   output.
#
#   THE PER-FILE HALF OF THE GATE IS OFF BY DEFAULT.  The filtered trace contains
#   template-heavy headers under ccec/include whose figures were never part of the named
#   target set, and three of them are below the bar with no test in this suite's authorised
#   inventory reaching them: measured on this tree, ccec/include/ccec/Operand.hpp at 0.0%
#   (0/6), Exception.hpp at 33.3% (4/12) and Messages.hpp at 76.5% (228/298).  All three sit
#   at exactly those figures before this project's tests as well, so they are a pre-existing
#   property of the suite rather than a regression, and defaulting them into a hard failure
#   would report a red run for work nobody asked for.  The named targets -- ccec/src and
#   osal/src -- are all above the bar, and the aggregate is 91.5%.
#
#   `--per-file-gate` (or COVERAGE_PER_FILE_GATE=1) turns the per-file half on, which is the
#   right setting once a translation unit covering those three headers is authorised.  The
#   enumeration is printed either way, so leaving the gate off never hides a figure; it only
#   stops it failing the run.
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
# stat is how the ancestry of every artifact path is checked (owner, mode, type) before a
# byte is written to it.  It is coreutils, like the rest of the primitives above.
STAT_BIN="$(resolve_tool stat)"
readonly LCOV_BIN GENHTML_BIN GCOV_BIN AWK_BIN FIND_BIN MKTEMP_BIN STAT_BIN

# ------------------------------------------------------------------------------------
# Configuration.  Every value is overridable from the environment, and the defaults are
# the ones that reproduce CI.  Command-line flags are applied after parsing and win.
# ------------------------------------------------------------------------------------
COVERAGE_MIN="${COVERAGE_MIN:-80}"
# Per-file gating defaults OFF: see GATE in the header for the three ccec/include headers
# that are below the bar with no authorised test reaching them, at the same figures as before
# this project.  COVERAGE_PER_FILE_GATE=1 or --per-file-gate turns it on.
COVERAGE_PER_FILE_GATE="${COVERAGE_PER_FILE_GATE:-0}"
# Artifact directory.  EMPTY BY DEFAULT, and that is the security-relevant part: with no
# explicit choice this script creates its root with `mktemp -d` under $TMPDIR, so the name
# is unpredictable and the directory is created atomically at mode 0700 (see
# resolve_output_dir).  A FIXED default -- which this script used to have,
# ${TMPDIR:-/tmp}/hdmicec-l1-coverage/<workspace basename> -- is guessable, and anything
# able to create entries in a world-writable $TMPDIR could pre-create that name as a
# symlink or as a directory of its own and collect, redirect or tamper with every trace,
# log and HTML page written under it.  An unpredictable name cannot be pre-created.
# A caller who needs a stable location still passes --output-dir / COVERAGE_OUTPUT_DIR,
# and that path is then held to the stricter checks in resolve_output_dir, because a name
# chosen in advance is by definition guessable.
OUTPUT_DIR="${COVERAGE_OUTPUT_DIR:-}"
# 1 when the caller named the directory (flag or environment), 0 when this script mints it.
OUTPUT_DIR_EXPLICIT=0
if [ -n "$OUTPUT_DIR" ]; then
    OUTPUT_DIR_EXPLICIT=1
fi
# Prefix providing libgtest/libgmock and their headers.  CI uses $GITHUB_WORKSPACE/install/usr.
GTEST_PREFIX="${GTEST_PREFIX:-$WS/install/usr}"
GTEST_EXTRA_ARGS="${GTEST_EXTRA_ARGS:-}"

DO_BUILD=0
DO_RUN=0
DO_HTML=1
DO_RESTORE_ONLY=0

# Reasons this invocation's figures, however good, are NOT an acceptance verdict.  Empty
# means the numbers stand on evidence this run established itself; non-empty makes the
# final verdict ADVISORY and the exit status 3.  See ADVISORY VERDICT in the header.
ADVISORY_REASONS=''
readonly EXIT_ADVISORY=3

note_advisory() { # $1=one-line reason
    if [ -z "$ADVISORY_REASONS" ]; then
        ADVISORY_REASONS="$1"
    else
        ADVISORY_REASONS="$ADVISORY_REASONS
$1"
    fi
}

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
# PATH SAFETY -- the ancestry of every path this script writes to.
#
# The artifact root is PREDICTABLE by design: ${TMPDIR:-/tmp}/hdmicec-l1-coverage/<ws>,
# fixed names underneath it, so a reader knows where to look and CI can collect them.  A
# predictable path under a world-writable directory is also an invitation: anything that
# can create entries in /tmp can create hdmicec-l1-coverage FIRST -- as a symlink to a
# directory it does not own, or as a directory it does own -- and then every trace, log
# and HTML page this script writes lands somewhere it chose, with this script's
# privileges.  On a CI runner that is a write into another job's workspace; run under
# sudo, it is a write anywhere.
#
# Checking only the leaf, which is what this script used to do, does not close that: the
# leaf can be perfectly ordinary while its PARENT is the substitution.  So the whole chain
# from / down is checked, and every existing component must satisfy all three of:
#
#   * not a symbolic link.  A link is exactly the substitution being defended against, and
#     resolving it first (`pwd -P`, `mkdir -p`) would validate the target while the write
#     still goes through the link -- so the link is rejected instead of followed.
#   * owned by this effective user, or by root.  Root ownership is accepted because /,
#     /tmp and /var are legitimately root's; anyone ELSE owning a component means someone
#     else can rename or replace it underneath this run.
#   * not group- or world-writable unless sticky.  1777 on /tmp is the standard and is
#     safe for entries this script creates, because the sticky bit stops a non-owner
#     removing or renaming them.  The same permissions WITHOUT the sticky bit mean any
#     local account can swap a component out mid-run.
#
# Directories this script creates are created 0700, one component at a time, so an
# intermediate never exists with permissive modes even briefly.  And because a check is
# only true at the moment it runs, the whole set is REPEATED immediately before each
# destructive step (see assert_output_dir_still_safe) rather than once at startup.
# ------------------------------------------------------------------------------------
EUID_VALUE="$(id -u)"
readonly EUID_VALUE

# "<uid> <octal mode> <type>" for an existing path, empty for one that does not exist.
# lstat semantics (stat does not follow the final link), so a symlink reports as such
# rather than as whatever it points at.
path_metadata() { # $1=path
    # Checked here rather than only in require_tools: the first ancestry validation happens
    # before require_tools' message would be reached in some orderings, and a missing stat
    # would otherwise degrade every check below into a silent "could not stat" failure.
    [ -n "${STAT_BIN:-}" ] || die "stat was not found on PATH, so the ownership and permissions of
       the paths this script writes to cannot be checked.  Refusing to write anything.  stat
       ships with coreutils."
    "$STAT_BIN" -c '%u %a %F' -- "$1" 2>/dev/null || true
}

# One component of a chain: must exist, be a directory, be ours or root's, and not be
# writable by anyone else unless the sticky bit protects it.
# $3 is how the path below this component was chosen, and it changes only the verdict for
# the "writable by others without a sticky bit" case:
#   named  -- a caller chose the name, so it is predictable: that condition is FATAL.
#   minted -- this script created it with mktemp -d, so it could not be pre-created: the
#             condition is reported and the re-validation before each destructive step is
#             what carries the guarantee.
assert_component_safe() { # $1=path  $2=context for the message  $3=named|minted
    local comp="$1" context="$2" choice="${3:-named}" meta uid rest mode kind numeric_mode

    meta="$(path_metadata "$comp")"
    [ -n "$meta" ] || die "could not stat $comp while validating the ancestry of
       $context
       Refusing to write below a path whose ownership and permissions cannot be read."

    uid="${meta%% *}"
    rest="${meta#* }"
    mode="${rest%% *}"
    kind="${rest#* }"

    [ "$kind" = "directory" ] || die "$comp is a $kind, not a directory, while validating
       the ancestry of
       $context
       Choose an --output-dir whose every parent is a real directory."

    if [ "$uid" != "$EUID_VALUE" ] && [ "$uid" != "0" ]; then
        die "$comp is owned by uid $uid, which is neither this user ($EUID_VALUE) nor root,
       while validating the ancestry of
       $context
       Another user who owns a parent directory can replace it underneath this run, so the
       artifacts would be written somewhere they chose.  Use --output-dir (or
       COVERAGE_OUTPUT_DIR) to name a location you own."
    fi

    numeric_mode="$(( 8#$mode ))"
    if [ "$(( numeric_mode & 0022 ))" -ne 0 ] && [ "$(( numeric_mode & 01000 ))" -eq 0 ]; then
        # Writable by others, with no sticky bit to stop them renaming or removing what is
        # inside it.  How much that matters depends entirely on whether the name underneath
        # it is guessable, which is why the two cases are separated instead of both being
        # forced into one verdict:
        #
        #   * A CALLER-CHOSEN path is guessable by construction -- it was chosen in advance
        #     and often appears in a CI file -- so this is fatal.  Pre-creating the name is
        #     enough to collect or redirect the evidence.
        #   * A path this script MINTED with mktemp -d cannot be pre-created, because the
        #     name does not exist until the moment it is created and is not predictable.
        #     What remains is a race: another account could remove the directory mid-run and
        #     put its own there.  That is reported, and every destructive step re-validates
        #     (assert_output_dir_still_safe) so the substitution is refused rather than
        #     written into -- but it is not pretended away either.
        if [ "$choice" = "named" ]; then
            die "$comp has mode $mode -- writable by group or world, without the sticky bit --
       while validating the ancestry of
       $context
       That path was named explicitly, so it is predictable, and any local account able to
       write $comp can pre-create or replace it and collect this run's evidence.  Either set
       the sticky bit on $comp (as a conventional /tmp has), tighten its mode, or drop
       --output-dir / COVERAGE_OUTPUT_DIR and let this script mint an unpredictable
       mode-0700 root with mktemp -d instead."
        fi
        warn "$comp has mode $mode: writable by group or world with no sticky bit."
        warn "  The artifact root below it was created with mktemp -d, so its name cannot be"
        warn "  guessed or pre-created; what is left is that another local account could"
        warn "  remove it mid-run.  Every destructive step re-validates the directory before"
        warn "  writing, so a substitution is refused rather than written into."
        warn "  Fix the host if you can: chmod +t $comp"
    fi
}

# The whole chain from / down to $1.  $1 itself need not exist; the walk stops at the
# first component that does not, because nothing below it exists either.
assert_safe_ancestry() { # $1=absolute path  $2=named|minted (see assert_component_safe)
    local target="$1" choice="${2:-named}" walked='' component

    case "$target" in
        /*) ;;
        *)  die "internal error: assert_safe_ancestry needs an absolute path; got: $target" ;;
    esac

    assert_component_safe "/" "$target" "$choice"

    local saved_ifs="$IFS"
    IFS='/'
    # Deliberate word splitting on '/' to walk the components in order.
    # shellcheck disable=SC2086
    set -- ${target#/}
    IFS="$saved_ifs"

    for component in "$@"; do
        [ -n "$component" ] || continue
        walked="$walked/$component"
        # Checked BEFORE -e, because -e is false for a dangling symlink and a dangling
        # symlink is precisely how a path gets created somewhere unintended.
        if [ -L "$walked" ]; then
            die "$walked is a symbolic link, and this script will not write through one.
       It is a component of
       $target
       Remove it, or use --output-dir (or COVERAGE_OUTPUT_DIR) to name a real directory."
        fi
        [ -e "$walked" ] || return 0
        assert_component_safe "$walked" "$target" "$choice"
    done
    return 0
}

# Create $1 and any missing parent, 0700 and one component at a time, after proving the
# existing part of the chain is safe.  mkdir -p -m applies the mode to the FINAL component
# only, which would leave intermediates at the umask default, so the loop is not redundant.
# Only ever used for a CALLER-NAMED path, hence the unconditional "named" strictness.
create_safe_dir() { # $1=absolute path
    local target="$1" walked='' component

    assert_safe_ancestry "$target" named

    local saved_ifs="$IFS"
    IFS='/'
    # shellcheck disable=SC2086
    set -- ${target#/}
    IFS="$saved_ifs"

    for component in "$@"; do
        [ -n "$component" ] || continue
        walked="$walked/$component"
        [ ! -L "$walked" ] || die "$walked became a symbolic link while the output directory
       was being created.  Refusing to continue."
        if [ ! -e "$walked" ]; then
            mkdir -m 0700 -- "$walked" 2>/dev/null || {
                # A concurrent run of this same script legitimately creates the same
                # component; losing that race is fine as long as what won is safe.
                [ -d "$walked" ] || die "could not create $walked while preparing
       $target"
            }
        fi
        assert_component_safe "$walked" "$target" named
    done
    return 0
}

# Stricter than assert_component_safe, for a directory this script created for its own
# private use: nobody else may write to it at all, sticky bit or not.
assert_private_dir() { # $1=path
    local path="$1" meta uid rest mode kind numeric_mode

    [ ! -L "$path" ] || die "expected a private directory but found a symbolic link: $path"
    meta="$(path_metadata "$path")"
    [ -n "$meta" ] || die "expected a private directory but could not stat it: $path"
    uid="${meta%% *}"
    rest="${meta#* }"
    mode="${rest%% *}"
    kind="${rest#* }"
    [ "$kind" = "directory" ] || die "expected a private directory but found a $kind: $path"
    [ "$uid" = "$EUID_VALUE" ] || die "a directory this script created is owned by uid $uid
       rather than by this user ($EUID_VALUE): $path"
    numeric_mode="$(( 8#$mode ))"
    [ "$(( numeric_mode & 0077 ))" -eq 0 ] || die "a directory this script created for its own
       use has mode $mode, which lets other accounts read or write it: $path"
}

# Re-run the ancestry check immediately before a destructive step, and check the specific
# artifact path too.  A path that was safe when the run started is not necessarily safe
# thirty seconds later: this is the check that makes the guarantee hold at the moment of
# the write rather than at startup.
assert_output_dir_still_safe() { # $1=artifact path about to be written (optional)
    local artifact="${1:-}"

    [ -n "${OUTPUT_DIR:-}" ] || die "internal error: assert_output_dir_still_safe called before
       the output directory was resolved"
    assert_safe_ancestry "$OUTPUT_DIR" "$( [ "${OUTPUT_DIR_EXPLICIT:-1}" -eq 1 ] && printf 'named' || printf 'minted' )"
    [ -d "$OUTPUT_DIR" ] || die "the output directory disappeared during the run: $OUTPUT_DIR"

    if [ -n "$artifact" ]; then
        [ ! -L "$artifact" ] || die "refusing to write through a symbolic link that appeared
       during the run: $artifact"
    fi
}

# ------------------------------------------------------------------------------------
# Cleanup state and the single trap that services it.  Both actions are idempotent, so
# running the trap on a normal exit and again on a signal is harmless.
#   LCOV_HOME   private, empty, mode-0700 HOME handed to every lcov and genhtml call
#   STAGE_DIR   private staging directory for the HTML report
# ------------------------------------------------------------------------------------
LCOV_HOME=''
STAGE_DIR=''

cleanup_lcov_home() {
    [ -n "$LCOV_HOME" ] || return 0
    local home="$LCOV_HOME"
    LCOV_HOME=''
    # Only ever a directory this script created with mktemp -d, never a caller-supplied
    # path, and the two-component pattern keeps a recursive remove away from '/' and
    # '/anything'.
    case "$home" in
        /*/*) [ -d "$home" ] && rm -rf -- "$home" ;;
        *)    warn "refusing to remove an implausible private HOME path: $home" ;;
    esac
    return 0
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
    cleanup_lcov_home
    return "$rc"
}
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP


# ------------------------------------------------------------------------------------
# Step 0 -- give lcov a private HOME, and never touch the caller's.
#
# lcov reads $HOME/.lcovrc silently on EVERY invocation, --version included, and a copy
# there that disables branch collection would quietly defeat the whole point of this
# script -- CI plants exactly such a copy in the plugin jobs, so the hazard is real and
# not hypothetical.  What this step needs is only that NO home configuration is in effect
# while lcov runs.
#
# The obvious ways to get that are both wrong.  `rm -f ~/.lcovrc` destroys a developer's
# file as a side effect nobody asked for.  Moving it aside and moving it back -- which is
# what this script used to do -- is worse than it looks: the restoring `mv -f` overwrites
# whatever stands at $HOME/.lcovrc at that moment, so a file that REAPPEARED during the
# run (the owner recreating it, or a sibling coverage runner in this same workspace, which
# stashes the same path) is silently destroyed by a script that was only supposed to
# measure; and until the restore, a file recreated mid-run was in effect for every lcov
# call after it, which is the very thing the stash existed to prevent.
#
# So the caller's HOME is left completely alone and lcov is given one of its own: an empty
# mode-0700 directory that contains no .lcovrc and never will.  Every lcov and genhtml
# invocation in this script goes through lcov_run/genhtml_run below, which set HOME to it.
# There is nothing to restore, nothing to race, and no trap needed to undo it -- the
# directory is removed on exit and that is all.
#
# /etc/lcovrc is never touched either.  Together with --config-file that leaves lcov
# reading exactly one configuration file -- this suite's versioned one -- plus the --rc
# overrides, which is what makes the run reproducible on any host.
# ------------------------------------------------------------------------------------
make_private_lcov_home() {
    local parent="${TMPDIR:-/tmp}"
    case "$parent" in
        /*) ;;
        *)  die "TMPDIR must be an absolute path to be checked safely; got: $parent" ;;
    esac
    assert_safe_ancestry "$parent/hdmicec-lcov-home" minted

    LCOV_HOME="$("$MKTEMP_BIN" -d "$parent/hdmicec-lcov-home.XXXXXXXX")" \
        || die "could not create a private HOME for lcov under $parent.
       Refusing to run lcov with the caller's home configuration in effect, and refusing
       to delete or move the caller's ~/.lcovrc to get around it."
    chmod 700 -- "$LCOV_HOME" || die "could not restrict the private lcov HOME to mode 0700: $LCOV_HOME"

    # mktemp -d created it, so it is empty and owned by this user; assert both rather than
    # assume them, because everything downstream trusts this directory to hold no
    # configuration.
    assert_private_dir "$LCOV_HOME"
    if [ -e "$LCOV_HOME/.lcovrc" ] || [ -L "$LCOV_HOME/.lcovrc" ]; then
        die "the private lcov HOME already contains a .lcovrc: $LCOV_HOME/.lcovrc
       mktemp -d had just created that directory, so something raced this run.  Refusing to
       run lcov against a configuration file this script did not put there."
    fi
    log "lcov runs with a private empty HOME: $LCOV_HOME (your \$HOME is not read or written)"
}

# Every lcov and genhtml invocation in this script goes through these two wrappers, and
# HOME is the only reason they exist.  Setting it per-command rather than exporting it for
# the whole script is deliberate: the suite under test, the compiler and git all run from
# here too, and none of them should have its HOME rewritten by a coverage script.
lcov_run() {
    [ -n "$LCOV_HOME" ] || die "internal error: lcov_run called before the private HOME was created"
    HOME="$LCOV_HOME" "$LCOV_BIN" "$@"
}
genhtml_run() {
    [ -n "$LCOV_HOME" ] || die "internal error: genhtml_run called before the private HOME was created"
    HOME="$LCOV_HOME" "$GENHTML_BIN" "$@"
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
                           filtered trace is below the bar.  OFF by default, for the
                           scope reason recorded under GATE in this script's header;
                           below-bar files are always enumerated either way.
      --no-per-file-gate   Gate on the aggregate only.  This is the default; the flag
                           exists so a caller can state it explicitly, and so it can
                           override COVERAGE_PER_FILE_GATE=1 from the environment.
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
  COVERAGE_PER_FILE_GATE=0     same as --no-per-file-gate (the default)
  GTEST_PREFIX=DIR             prefix providing libgtest/libgmock and their headers,
                               used by --build and put on LD_LIBRARY_PATH by --run.
                               Default: ${GTEST_PREFIX}
  GTEST_EXTRA_ARGS="..."       extra arguments appended to the run_L1Tests command line.
                               --gtest_shuffle is a DIAGNOSTIC: this suite has
                               pre-existing order-fragile tests that pass in the default
                               order, so a shuffled run is expected to fail and must
                               never be treated as a gate.

EXIT STATUS
  0  ACCEPTANCE: the suite was run by this invocation, it was green, and line coverage is
     at or above the bar.  This is the only status that means "measured and passing".
  1  a prerequisite is missing, a step failed, or the coverage gate failed
  3  ADVISORY: coverage is at or above the bar, but this invocation did not establish the
     evidence for it -- currently only when --run was omitted, so the figures come from
     pre-existing cumulative counters.  All artifacts are produced; the numbers are real
     but unattributable.  Do not treat 3 as a pass.

RESOLVED PATHS FOR THIS INVOCATION
  script          ${SCRIPT_PATH}
  submodule root  ${HDMICEC_ROOT}      (the lcov capture directory)
  workspace root  ${WS}
  artifacts       ${OUTPUT_DIR:-<created with mktemp -d under ${TMPDIR:-/tmp}; printed as "artifacts:" when the run starts>}
USAGE
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            -b|--build)          DO_BUILD=1 ;;
            -r|--run)            DO_RUN=1 ;;
            --per-file-gate)     COVERAGE_PER_FILE_GATE=1 ;;
            --no-per-file-gate)  COVERAGE_PER_FILE_GATE=0 ;;
            --no-html)           DO_HTML=0 ;;
            --restore)           DO_RESTORE_ONLY=1 ;;
            -h|--help)           usage; exit 0 ;;
            -t|--threshold)
                [ $# -ge 2 ] || die "--threshold requires a value"
                COVERAGE_MIN="$2"; shift ;;
            --threshold=*)       COVERAGE_MIN="${1#*=}" ;;
            -o|--output-dir)
                [ $# -ge 2 ] || die "--output-dir requires a value"
                OUTPUT_DIR="$2"; OUTPUT_DIR_EXPLICIT=1; shift ;;
            --output-dir=*)      OUTPUT_DIR="${1#*=}"; OUTPUT_DIR_EXPLICIT=1 ;;
            --)                  shift; break ;;
            -*)                  die "unknown option '$1'. Run '$SCRIPT_PATH --help' for the option list." ;;
            *)                   die "unexpected argument '$1'. This script takes options only; run --help." ;;
        esac
        shift
    done
    [ $# -eq 0 ] || die "unexpected trailing arguments: $*"

    # THRESHOLD SHAPE AND RANGE.  The accepted spelling is deliberately the same as the two
    # plugin runners': digits, or digits.digits.  This runner used to accept integers only,
    # which meant the three runners in this workspace disagreed about what a threshold is --
    # `COVERAGE_MIN=80.5` was a working diagnostic bar for the plugins and a hard error here,
    # so the three could not be wired interchangeably into one pipeline.  lcov's
    # --fail-under-lines takes a fractional bar, so accepting one costs nothing and refusing
    # it bought nothing.
    #
    # What is NOT accepted is anything that would have to be guessed at: an empty value, a
    # letter (`8O` for `80` is the classic typo), a sign, surrounding spaces, or more than one
    # decimal point.  A bar that cannot be read exactly is refused rather than coerced,
    # because a coerced bar produces a gate verdict for a percentage nobody asked for.
    case "$COVERAGE_MIN" in
        ''|*[!0-9.]*|*.*.*|.*|*.)
            die "threshold must be a number spelled as digits or digits.digits -- for example
       80, 0, 100 or 80.5 -- and between 0 and 100 (got '$COVERAGE_MIN').  A threshold that
       cannot be read exactly is refused rather than rounded: a coerced bar would produce a
       gate verdict for a percentage nobody asked for." ;;
    esac
    # Range and the not-80 diagnostic are decided from the value's own digits rather than
    # with awk or shell arithmetic.  `[ 80.5 -le 100 ]` is a syntax error in every POSIX
    # shell, and awk cannot be used here because this validation runs BEFORE require_tools()
    # -- deliberately, so that a bad threshold is refused before anything else happens -- so
    # an absent awk would surface as a threshold error, which would be a lie.
    local min_int="${COVERAGE_MIN%%.*}"          # digits before the point, "" for ".5"
    local min_frac=""
    case "$COVERAGE_MIN" in
        *.*) min_frac="${COVERAGE_MIN#*.}" ;;
    esac
    : "${min_int:=0}"
    # A leading run of digits can be arbitrarily long ("00080"); strip it to a plain integer
    # so the comparison below is on a value the shell can hold.
    while [ "${#min_int}" -gt 1 ] && [ "${min_int#0}" != "$min_int" ]; do
        min_int="${min_int#0}"
    done
    if [ "$min_int" -gt 100 ] || { [ "$min_int" -eq 100 ] && [ -n "${min_frac//0/}" ]; }; then
        die "threshold must be between 0 and 100 (got '$COVERAGE_MIN').  A bar above 100% can
       never be met, so the gate could only ever fail and would say nothing about the tests."
    fi
    # 80 is this submodule's acceptance bar.  Any other value is a diagnostic, and saying so
    # out loud is what stops a --threshold run's verdict being quoted as an acceptance result.
    # 80, 80.0 and 80.00 are the same bar; 80.5 is not.
    if [ "$min_int" -ne 80 ] || [ -n "${min_frac//0/}" ]; then
        warn "the line bar is ${COVERAGE_MIN}%, not the required 80%.  This is a DIAGNOSTIC run:"
        warn "    its verdict is NOT the acceptance verdict for this submodule."
    fi

    case "$COVERAGE_PER_FILE_GATE" in
        0|1) ;;
        *)   die "COVERAGE_PER_FILE_GATE must be 0 or 1, got '$COVERAGE_PER_FILE_GATE'" ;;
    esac

    # An empty value here means two very different things depending on how it got there.
    # Unset -- the default -- means "mint an unpredictable root with mktemp -d", which is the
    # normal path and must not be rejected.  Explicitly EMPTY (--output-dir '' or
    # COVERAGE_OUTPUT_DIR=) would otherwise silently resolve to the caller's working directory,
    # so that one is still refused.  OUTPUT_DIR_EXPLICIT is what distinguishes them.
    if [ "$OUTPUT_DIR_EXPLICIT" -eq 1 ] && [ -z "$OUTPUT_DIR" ]; then
        die "--output-dir (or COVERAGE_OUTPUT_DIR) was given as an empty value.  An empty path
       would resolve to this run's working directory; leave it unset to have an unpredictable
       mode-0700 root minted under \${TMPDIR:-/tmp} instead, or give a real directory."
    fi
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
    [ -n "$STAT_BIN" ]    || { warn "stat not found on PATH";    missing=1; }
    [ "$missing" -eq 0 ] || die "missing coverage tooling.
       lcov, genhtml and gcov are what this script measures with, and awk, find and
       mktemp are how it parses and stages.  On a Debian/Ubuntu host:
           sudo apt-get install -y lcov
       gcov ships with gcc; awk, find, mktemp and stat ship with the base system.
       lcov 2.x is required: this script uses --fail-under-lines and
       --rc branch_coverage=1, neither of which exists in lcov 1.x."
}

# Recorded rather than enforced: the trace-record spellings this script parses (FNL:, FNA:)
# and the gate option it relies on are lcov 2.x behaviour.  A 1.x lcov would fail later with
# a confusing message, so name the version now.
#
# This runs AFTER make_private_lcov_home(), and the order is not cosmetic.  lcov reads
# $HOME/.lcovrc on EVERY invocation, `--version` included, and a file it cannot parse makes
# every one of them fail: a home configuration setting both `lcov_branch_coverage` and
# `genhtml_branch_coverage` makes lcov 2.0-1 answer `--version` with
# "ERROR: unexpected ARRAY for branch_coverage value".  Printed before the private HOME
# existed, this banner reported that error text as though it were a version string -- a line
# that looks like provenance and is not.  Run with the private HOME in place, what it prints
# is the version of the tool that will actually do the measuring.
#
# `head -n1` can close the pipe early and hand the producer a SIGPIPE, which pipefail would
# otherwise turn into a fatal error over a banner line -- hence the `|| true`.
log_tool_versions() {
    log "lcov:    $( { lcov_run    --version 2>&1 | head -n1; } || true )"
    log "genhtml: $( { genhtml_run --version 2>&1 | head -n1; } || true )"
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
        # And make that visible in the VERDICT, not only in the log.  Warning about stale
        # evidence and then exiting 0 with "COVERAGE GATE PASSED" is indistinguishable, to
        # any caller, from a clean measurement -- so this invocation can no longer produce
        # an acceptance verdict at all.  The figures and artifacts are still produced.
        note_advisory "the suite was NOT run by this invocation (no --run): the figures come from
       pre-existing, cumulative .gcda counters (newest: ${newest:-unknown}) and may credit
       tests that no longer run.  Re-run with --run for an acceptance verdict."
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
# A zero exit status from run_L1Tests means "nothing failed", which is NOT the same as
# "something ran".  GoogleTest exits 0 for an empty selection, so a mistyped or overly narrow
# --gtest_filter produced a green line and a coverage figure measured over no execution at
# all.  Three things are therefore required of the results file the run writes.  The file was
# deleted immediately beforehand, so:
#   * it exists            -> this run wrote it, rather than an earlier one;
#   * "tests" is > 0       -> the binary selected at least one case;
#   * it names one of this suite's own fixtures -> what ran was THIS suite, not, say, only
#     a GoogleTest self-test or a case from an unrelated binary that happened to be on PATH.
# The fixture list is deliberately a stable subset rather than every fixture in the suite: it
# names the four oldest, largest fixtures, so adding or renaming a test file does not require
# editing this list, while a run that exercised none of them is not this suite.
readonly EXPECTED_SUITE_PATTERN='"(classname|name)"[[:space:]]*:[[:space:]]*"(CECFrameTest|ConnectionTest|BusTest|DriverTest|LibCCECTest|OpCodeTest|MessageDecoderTest|MessageEncoderTest|OperandsTest)'

verify_results() { # $1 = path to the results JSON this run was told to write
    local results="$1" count

    [ -f "$results" ] || die "run_L1Tests exited 0 but wrote no results file at
       $results
       The file was deleted immediately before the run, so its absence means the binary
       produced no results at all.  A coverage figure cannot be attributed to a run that
       left no evidence it executed anything."

    # The GoogleTest JSON header carries the run's totals; the first "tests" key is the
    # top-level count.  sed rather than a JSON parser, because this script adds no
    # dependency beyond the POSIX tools it already needs.
    count="$(sed -n 's/^[[:space:]]*"tests"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$results" | head -1)"
    if [ -z "$count" ] || [ "$count" -le 0 ]; then
        die "run_L1Tests exited 0 but $results reports no tests (\"tests\": ${count:-absent}).
       An empty run cannot substantiate a coverage figure: every line would be reported
       unhit and the gate would fail for the wrong reason, or -- worse, with a partial
       filter -- a subset's figure would be reported as the suite's.
       If a --gtest_filter was passed through GTEST_EXTRA_ARGS, it selected nothing."
    fi

    grep -Eq "$EXPECTED_SUITE_PATTERN" "$results" || die "run_L1Tests exited 0 and $results
       reports $count test case(s), but not one of them belongs to a known middleware L1
       fixture.  Whatever ran was not this suite, while the capture would credit this
       submodule's objects.  Check that ./run_L1Tests in tests/L1Tests is the binary built
       from this tree and that GTEST_EXTRA_ARGS is not restricting the run to unrelated
       cases."

    log "run_L1Tests reported $count test case(s) in $results, including known middleware fixtures"
}

do_run() {
    rule
    log "zeroing gcov counters under $HDMICEC_ROOT (leaves *.gcno instrumentation intact)"
    lcov_run --zerocounters -d "$HDMICEC_ROOT" \
        "${LCOV_CONFIG_ARGS[@]}" "${LCOV_RC_ARGS[@]}" \
        --ignore-errors "$LCOV_SUMMARY_IGNORE" >/dev/null 2>&1 \
        || die "lcov --zerocounters failed for $HDMICEC_ROOT.
       Refusing to run the suite, because the capture would then mix this run's counters
       with whatever was left behind by earlier ones."
    # A zero exit from `lcov --zerocounters` is not proof that the counters are gone: it walks
    # the tree it was given and reports success for the files it managed to clear, so a .gcda
    # in a directory it did not descend into -- or one it could not remove -- survives with no
    # error.  That survivor still holds an EARLIER run's execution counts, gcov ACCUMULATES
    # into it, and the capture below would then credit this run with work it never did; the
    # gate could pass on evidence the current tests did not produce.  This used to be a
    # warning, which is exactly the shape of "reported but not prevented".  It is fatal now,
    # matching the sink runner: refuse to measure rather than measure the wrong thing.
    local after_zero
    after_zero="$(gcda_count)"
    # FATAL, not a warning.  lcov --zerocounters exiting 0 means "the command ran", not
    # "every counter is gone": it silently leaves behind any .gcda it could not unlink -- a
    # read-only directory, a file owned by another user, a stray copy outside the object
    # tree it walked.  Those counters are then captured alongside this run's and credited to
    # it, so a line last executed by a test that has since been deleted or filtered out
    # still reports as hit.  That is precisely the stale-evidence acceptance this whole
    # script exists to prevent, and warning about it while carrying on and printing
    # "COVERAGE GATE PASSED" made the warning worthless.  Nothing downstream can repair it,
    # so the run stops here.
    if [ "$after_zero" -ne 0 ]; then
        local survivors
        survivors="$( { "$FIND_BIN" "$HDMICEC_ROOT" -name '*.gcda' -type f 2>/dev/null || true; } \
                      | head -n 10 | sed 's/^/         /')"
        die "$after_zero .gcda counter file(s) SURVIVED lcov --zerocounters under
       $HDMICEC_ROOT
       gcov counters accumulate, so capturing now would mix this run's execution with
       whatever produced those files and credit all of it to this run.  Refusing to
       measure, because the resulting figure could not be attributed to anything.
       First survivors:
$survivors
       Remove them and retry:
           find '$HDMICEC_ROOT' -name '*.gcda' -type f -delete
       If they cannot be removed, the build tree is not writable by this user and a
       fresh --build into a tree you own is the fix."
    fi

    local run_log="$OUTPUT_DIR/run_L1Tests.log"
    local results_json="$OUTPUT_DIR/rdkL1TestResults.json"
    local ld_path="${LD_LIBRARY_PATH:-}"

    # Deleted BEFORE the binary starts, so a results file that exists afterwards can only
    # have been written by this run.  Without this, a binary that produced nothing would be
    # judged against whatever an earlier invocation left at the same path.
    rm -f -- "$results_json"
    if [ -d "$GTEST_PREFIX/lib" ]; then
        ld_path="$GTEST_PREFIX/lib${ld_path:+:$ld_path}"
    fi

    log "running the L1 suite: $HDMICEC_ROOT/$TEST_BINARY_REL"
    log "  LD_LIBRARY_PATH=$ld_path"
    log "  results JSON:    $results_json"
    log "  time limit:      ${SUITE_TIMEOUT}s (SUITE_TIMEOUT)"
    if [ -n "$GTEST_EXTRA_ARGS" ]; then
        log "  extra gtest args: $GTEST_EXTRA_ARGS"
    fi

    # --gtest_print_time=1 and the JSON output mirror the workflow's own run step.
    # GTEST_EXTRA_ARGS is intentionally word-split: it is a user-supplied argument list.
    #
    # BOUNDED, because an unbounded run is not a run that can fail.  A test that deadlocks --
    # this suite drives real pthread mutexes, condition variables and threads, and a mock that
    # never signals is one edit away -- would otherwise hang here for ever: no output, no exit,
    # no gate, and in CI a job killed by the runner's own limit with no diagnosis attached.
    # `timeout --foreground` so the child keeps the terminal and a Ctrl-C still reaches it, plus
    # --kill-after where the local timeout preserves exit 124 with it (see the probe above), to
    # escalate to SIGKILL if the suite ignores SIGTERM.  Exit 124 is timeout's own signal that
    # the limit fired, and it is reported as its own outcome below rather than folded into "the
    # suite failed", because the two need different fixes.
    local rc=0
    (
        cd "$HDMICEC_ROOT/tests/L1Tests"
        LD_LIBRARY_PATH="$ld_path" \
        "$TIMEOUT_BIN" --foreground "${TIMEOUT_KILL_AFTER[@]}" "$SUITE_TIMEOUT" \
            ./run_L1Tests --gtest_print_time=1 "--gtest_output=json:$results_json" \
                ${GTEST_EXTRA_ARGS:+$GTEST_EXTRA_ARGS}
    ) >"$run_log" 2>&1 || rc=$?

    # The tail is the part a reader needs; the whole log is on disk either way.
    "$AWK_BIN" '/^\[==========\]|^\[  PASSED  \]|^\[  FAILED  \]|^\[  SKIPPED \]|tests? from .* ran/' "$run_log" \
        | sed 's/^/[run_coverage]   /' || true

    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
        printf '%s\n' "--- last 30 lines ---" >&2
        tail -n 30 -- "$run_log" | sed 's/^/[run_coverage]   /' >&2 || true
        die "the L1 suite did not finish within ${SUITE_TIMEOUT}s and was terminated (exit $rc).
       This is a HANG, not a test failure: the last test named in the log above is where it
       stopped.  Coverage is not captured, because a suite that was killed part-way through
       produced partial counters.  Investigate that test, or raise the bound deliberately with
       SUITE_TIMEOUT=<seconds> if the suite has legitimately grown.  Full log: $run_log"
    fi
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
    verify_results "$results_json"

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

# ------------------------------------------------------------------------------------
# EVIDENCE CUSTODY.  The default output directory is under $TMPDIR, and its name is
# derivable from the workspace path -- so on a shared host any local user can work out
# where the next run will write and pre-create it, or plant files in it, BEFORE this run
# starts.  Everything downstream then treats what it finds there as this run's evidence:
# the traces are what the gate reads and what the traceability report quotes.  Predictable
# plus writable-by-others is the whole vulnerability, so three things are required of the
# directory before a single byte is written into it, and the third is the one that closes
# it rather than merely narrowing it:
#   * it is not a symlink, and it is a directory (already checked below);
#   * it is owned by the user running this script, and grants no group or other write --
#     created that way under umask 077, and refused rather than silently repaired when it
#     already exists with someone else's ownership;
#   * every writable ancestor between it and / is either owned by root or by this user, or
#     carries the sticky bit -- the /tmp case -- because an attacker who can rename an
#     ancestor can substitute the whole subtree however tight its own mode is.
# A run also takes an exclusive lock on the directory for its whole duration, so two runs
# cannot interleave their captures into one set of artifact names and hand the gate a mix.
# ------------------------------------------------------------------------------------
assert_owned_and_private() { # $1=path
    local path="$1" owner mode
    owner="$(stat -c '%u' -- "$path" 2>/dev/null)" \
        || die "cannot stat the artifact directory: $path"
    mode="$(stat -c '%a' -- "$path" 2>/dev/null)"
    if [ "$owner" != "$(id -u)" ]; then
        die "the artifact directory $path is owned by uid $owner, not by you ($(id -u)).
       Evidence this run is judged on must not be under another account's control: another
       owner can replace a trace between the capture and the gate.  Remove it, or point
       --output-dir/COVERAGE_OUTPUT_DIR somewhere you own."
    fi
    case "$mode" in
        *[2367]|*[2367]?) : ;;   # group- or other-writable bit set somewhere
        *) return 0 ;;
    esac
    chmod go-w -- "$path" 2>/dev/null \
        || die "the artifact directory $path is group- or other-writable (mode $mode) and the
       permissions could not be tightened.  A directory anyone can write is a directory
       anyone can plant a trace in."
    log "tightened the artifact directory to owner-only write (was mode $mode)"
}

assert_ancestors_safe() { # $1=path -- walks upwards from the parent to /
    local dir owner mode uid
    uid="$(id -u)"
    dir="$(dirname -- "$1")"
    while : ; do
        owner="$(stat -c '%u' -- "$dir" 2>/dev/null)" || break
        mode="$(stat -c '%a' -- "$dir" 2>/dev/null)"
        # World- or group-writable is fine when the sticky bit is set (this is exactly /tmp)
        # or when the directory belongs to root or to us; otherwise a third party can rename
        # this component and substitute everything beneath it.
        case "$mode" in
            *[2367]|*[2367]?)
                # `-k` tests the sticky bit exactly, which a mode-string prefix match does not:
                # a sticky directory that is also setgid reads as 3777, not 1777, and would be
                # mistaken for non-sticky by a leading-1 test.
                if [ ! -k "$dir" ] && [ "$owner" != 0 ] && [ "$owner" != "$uid" ]; then
                    die "$dir is writable by others (mode $mode, owned by uid $owner) and is an
       ancestor of the artifact directory.  Anyone who can rename it can substitute the whole
       evidence tree.  Choose an output directory whose ancestors are owned by you or by root."
                fi
                    if [ ! -k "$dir" ]; then
                        warn "$dir is writable by others (mode $mode) and is NOT sticky, so anyone able to
         write there can rename or remove this subtree -- including the artifact directory beneath
         it.  It is owned by uid $owner (root or you), so this run continues, but the evidence
         under it is only as protected as that directory is.  A sticky /tmp (mode 1777) or an
         ARTIFACT_ROOT under a directory you own removes the exposure."
                    fi
                ;;
            *) : ;;
        esac
        [ "$dir" != / ] || break
        dir="$(dirname -- "$dir")"
    done
    return 0
}

# Held for the whole run on a lock file inside the artifact directory.  Non-blocking: a second
# concurrent run is a mistake to report, not something to queue behind, because both would be
# writing the same fixed artifact names and the gate would read whichever won.
# The descriptor is allocated by bash rather than hard-coded, because a literal number is a
# number this script does not own: fd 9 is free today and would be silently clobbered the moment
# anything else in the run wanted it, releasing the lock without a word.  `{LOCK_FD}>>` asks bash
# for a free descriptor (>= 10) and records which one it got; it stays open, and the lock stays
# held, until this shell exits.
LOCK_FD=''
acquire_output_lock() {
    local lock="$OUTPUT_DIR/.run.lock"
    [ ! -L "$lock" ] || die "refusing to lock through a symlink: $lock"
    exec {LOCK_FD}>>"$lock" || die "could not open the run lock: $lock"
    if command -v flock >/dev/null 2>&1; then
        flock -n "$LOCK_FD" || die "another coverage run holds the lock on $OUTPUT_DIR.
       Two runs would write the same artifact names ($(basename -- "$RAW_TRACE"),
       $(basename -- "$FILTERED_TRACE")) and the gate would read whichever finished last.
       Wait for it, or use a different --output-dir."
        log "holding the exclusive run lock on $OUTPUT_DIR"
    else
        warn "flock is not available, so concurrent runs into $OUTPUT_DIR cannot be prevented."
    fi
}

prepare_output_dir() {
    if [ "$OUTPUT_DIR_EXPLICIT" -eq 0 ]; then
        # NO NAME WAS CHOSEN, so mint one that could not have been.  mktemp -d creates the
        # directory and the name in one atomic step, which is the property a pre-creation
        # attack needs and cannot get: there is no window in which the name exists but the
        # directory does not, and nothing to guess beforehand.
        local parent="${TMPDIR:-/tmp}"
        case "$parent" in
            /*) ;;
            *)  die "TMPDIR must be an absolute path to be checked safely; got: $parent" ;;
        esac
        assert_safe_ancestry "$parent/hdmicec-l1-coverage" minted
        OUTPUT_DIR="$("$MKTEMP_BIN" -d "$parent/hdmicec-l1-coverage.XXXXXXXX")" \
            || die "could not create an artifact directory under $parent.
       Pass --output-dir DIR to write the artifacts somewhere else."
        chmod 700 -- "$OUTPUT_DIR" || die "could not restrict the artifact directory to mode 0700: $OUTPUT_DIR"
        assert_private_dir "$OUTPUT_DIR"
        log "artifact root minted for this run (mktemp -d, mode 0700): $OUTPUT_DIR"
    else
        # Absolutise before anything else, so later `cd` calls cannot re-root a relative path.
        case "$OUTPUT_DIR" in
            /*) ;;
            *)  OUTPUT_DIR="$(pwd -P)/$OUTPUT_DIR" ;;
        esac
        # The WHOLE chain, not just the leaf: the leaf can be an ordinary directory while its
        # parent is the substituted one.  create_safe_dir validates what exists, creates what
        # does not at 0700 one component at a time, and re-validates each component afterwards.
        create_safe_dir "$OUTPUT_DIR"
    fi
    [ -w "$OUTPUT_DIR" ] || die "the output directory is not writable: $OUTPUT_DIR"
    # Safe to resolve symlinks now: the walk above proved there are none to resolve.
    OUTPUT_DIR="$(cd -- "$OUTPUT_DIR" && pwd -P)"
    assert_owned_and_private "$OUTPUT_DIR"
    assert_ancestors_safe "$OUTPUT_DIR"

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
    acquire_output_lock

    log "artifacts: $OUTPUT_DIR (owner-only, locked for this run)"

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
    # Re-checked here, not merely at startup: this is the moment of the write.
    assert_output_dir_still_safe "$RAW_TRACE"
    rm -f -- "$RAW_TRACE"
    lcov_run -c \
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
    assert_output_dir_still_safe "$FILTERED_TRACE"
    rm -f -- "$FILTERED_TRACE"
    lcov_run -r "$RAW_TRACE" \
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

    # BRANCH DATA IS A DELIVERABLE, so its absence is fatal here rather than a note later.
    #
    # Branch collection is off in lcov by default and the legacy `lcov_branch_coverage` key is
    # deprecated, so a single missing --rc branch_coverage=1, an lcov 1.x on PATH that spells the
    # option differently, or a home/system configuration that wins would all produce a trace with
    # no BRDA/BRF records at all -- and every step after this one would still succeed, printing
    # "n/a" in the branch column and a report that silently answers a different question than the
    # one asked.  That is precisely the "requested but not enforced" shape, so it is checked on
    # the FILTERED trace (the one that is quoted and gated) and checked in the AGGREGATE: an
    # individual file with no branch arcs legitimately has no BRF record, which is why the test
    # is "no branch records anywhere", not "every file has one".
    local branch_records
    branch_records="$(grep -c '^BRDA:' "$FILTERED_TRACE" || true)"
    if [ "${branch_records:-0}" -eq 0 ]; then
        die "the filtered trace contains no branch records at all ($FILTERED_TRACE).
       Every lcov invocation here passes --rc branch_coverage=1, so branch data should be
       present for any file with branch arcs -- and this trace has none for ANY file, which
       means collection did not happen rather than that the code has no branches.
       Check that the lcov on PATH is 2.x (\`lcov --version\`), that no configuration file
       overrides branch_coverage, and that the objects were compiled with -fprofile-arcs
       -ftest-coverage.  Branch coverage is reported as evidence and never gated, but a report
       with no branch data would not be the report this suite is required to produce."
    fi
    local branch_files
    branch_files="$(grep -c '^BRF:' "$FILTERED_TRACE" || true)"
    log "branch data present: $branch_records BRDA record(s) across ${branch_files:-0} file record(s)"
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
    assert_output_dir_still_safe "$HTML_DIR"
    STAGE_DIR="$("$MKTEMP_BIN" -d "$OUTPUT_DIR/.genhtml-stage.XXXXXXXX")" \
        || die "could not create a staging directory under $OUTPUT_DIR"
    chmod 700 -- "$STAGE_DIR" || die "could not restrict the staging directory to mode 0700: $STAGE_DIR"
    # The report is assembled here and then moved into place, so the staging directory is
    # held to the private standard rather than merely the ancestry one.
    assert_private_dir "$STAGE_DIR"

    genhtml_run \
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

    # Re-checked immediately before the one recursive delete in the pipeline.
    assert_output_dir_still_safe "$HTML_DIR"
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
    lcov_run --summary "$FILTERED_TRACE" \
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
    assert_output_dir_still_safe "$PER_FILE_TSV"
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
    assert_output_dir_still_safe "$UNCOVERED_TXT"
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

    # Partition the below-bar set. Both halves are reported; only the gated half votes.
    BELOW_BAR_GATED=''
    BELOW_BAR_EXEMPT=''
    if [ -n "$BELOW_BAR_FILES" ]; then
        BELOW_BAR_GATED="$(printf '%s\n' "$BELOW_BAR_FILES" | while IFS="$(printf '\t')" read -r p v; do
            [ -n "$p" ] || continue
            if printf '%s\n' "$COVERAGE_GATE_EXEMPT_FILES" | grep -Fxq -- "$p"; then :; else printf '%s\t%s\n' "$p" "$v"; fi
        done)"
        BELOW_BAR_EXEMPT="$(printf '%s\n' "$BELOW_BAR_FILES" | while IFS="$(printf '\t')" read -r p v; do
            [ -n "$p" ] || continue
            if printf '%s\n' "$COVERAGE_GATE_EXEMPT_FILES" | grep -Fxq -- "$p"; then printf '%s\t%s\n' "$p" "$v"; fi
        done)"
    fi
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
# FAILS the run is the caller's choice: per-file gating is ON by default and
# `--no-per-file-gate` turns it off, for the reason given under GATE in the header.
# Branch coverage is never gated.
# ------------------------------------------------------------------------------------
apply_gate() {
    local failures=0 rc=0

    rule
    log "applying the >= ${COVERAGE_MIN}% line-coverage gate to the aggregate (lcov --fail-under-lines)"
    lcov_run --summary "$FILTERED_TRACE" \
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

    if [ -n "$BELOW_BAR_EXEMPT" ]; then
        rule
        warn "below the ${COVERAGE_MIN}% bar, and LISTED AS EXEMPT FROM THE PER-FILE GATE:"
        printf '%s\n' "$BELOW_BAR_EXEMPT" | while IFS="$(printf '\t')" read -r path pct_value; do
            [ -n "$path" ] || continue
            printf '[run_coverage]     %-46s %s%%\n' "$path" "$pct_value" >&2
        done
        warn "These are still in the trace and still in the AGGREGATE denominator above - the"
        warn "listing suppresses only the per-file pass/fail vote. The reason for each is"
        warn "recorded at COVERAGE_GATE_EXEMPT_FILES in this script; read it before trusting it."
        warn "Their uncovered line numbers are in: $UNCOVERED_TXT"
    fi

    if [ -n "$BELOW_BAR_GATED" ]; then
        rule
        warn "targets below the ${COVERAGE_MIN}% line-coverage bar:"
        printf '%s\n' "$BELOW_BAR_GATED" | while IFS="$(printf '\t')" read -r path pct_value; do
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
            log "per-file gating is OFF (the default; --per-file-gate or"
            log "  COVERAGE_PER_FILE_GATE=1 turns it on), so the above is reported but does"
            log "  not fail this run. See GATE in this script's header for why, and for the"
            log "  measured pre-existing figures of the three ccec/include headers listed."
        fi
    elif [ -z "$BELOW_BAR_FILES" ]; then
        log "every target in the filtered trace meets the ${COVERAGE_MIN}% bar"
    else
        log "every gated target meets the ${COVERAGE_MIN}% bar; the exempt listing above is the"
        log "  complete set of files below it, and each carries its reason in this script."
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
        # Below the bar fails the same way in both modes: a bad number is a bad number
        # whether or not this invocation produced the evidence for it.
        die "COVERAGE GATE FAILED (bar: ${COVERAGE_MIN}% line coverage).
       Artifacts for diagnosis:
           $artifacts"
    fi

    # At or above the bar -- but on whose evidence?  If anything reduced this run to
    # measuring what it did not establish, say so in the verdict and exit with a status a
    # caller can branch on, instead of printing the same line a clean run prints.
    if [ -n "$ADVISORY_REASONS" ]; then
        warn "COVERAGE ADVISORY (bar: ${COVERAGE_MIN}% line coverage): the figures meet the bar,"
        warn "  but THIS IS NOT AN ACCEPTANCE VERDICT, because:"
        printf '%s\n' "$ADVISORY_REASONS" | sed 's/^/[run_coverage]     /' >&2
        warn "  Every artifact was still produced and every number above is real; what is"
        warn "  missing is the provenance that would let anyone rely on them.  Exit status"
        warn "  $EXIT_ADVISORY marks that difference so a caller cannot mistake this for a pass."
        exit "$EXIT_ADVISORY"
    fi

    log "COVERAGE GATE PASSED (bar: ${COVERAGE_MIN}% line coverage)"
}

# ------------------------------------------------------------------------------------
# main.  The order is deliberate and is the whole argument of the script:
#   parse -> require the tools -> create the private lcov HOME -> name the tool
#   versions -> resolve artifacts -> [build] -> require an instrumented, exercised tree ->
#   [zero + run + verify] -> capture -> filter -> report HTML -> summarise -> per-file table
#   -> gate.
# The private HOME is created before the FIRST lcov invocation of the run, which is the
# version banner, not the capture: lcov reads a home configuration on every invocation and one
# it cannot parse breaks all of them.
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
        log "  lcov config    : $SCRIPT_DIR/.lcovrc_l1 (via --config-file, read instead of /etc/lcovrc; the private HOME has no .lcovrc)"
    else
        log "  lcov config    : none found at $SCRIPT_DIR/.lcovrc_l1; continuing with --rc overrides only."
        warn "without --config-file lcov also reads /etc/lcovrc; the --rc overrides still outrank"
        warn "  it, and no home configuration is reachable either way (private HOME)."
    fi
    log "  branch data    : forced on with --rc branch_coverage=1 on capture, filter, genhtml and summary"

    # Tool PRESENCE first (it needs no lcov invocation), then the private HOME, and only
    # then is lcov asked anything -- including for its version.  Creating the private HOME
    # ahead of every lcov call also covers the counter zeroing inside do_run, which is an
    # lcov invocation like any other and used to run with a home configuration in effect.
    require_tools
    make_private_lcov_home
    log_tool_versions
    prepare_output_dir

    if [ "$DO_BUILD" -eq 1 ]; then
        do_build
    fi
    require_instrumented_tree
    if [ "$DO_RUN" -eq 1 ]; then
        do_run
    fi

    capture_coverage
    filter_coverage
    generate_html
    summarise_coverage
    per_file_report
    apply_gate
}

main "$@"
