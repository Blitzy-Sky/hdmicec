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
#        build file behind your back, and IT NEVER RUNS `git checkout` AT ALL -- not in
#        the coverage pipeline, not in `--restore`, and not as advice it prints for
#        someone to paste.  That is a STRONGER promise than this clause used to make, and
#        the reason it had to be strengthened is measured rather than theoretical.
#        `configure` OVERWRITES the six git-tracked Makefiles listed at
#        REGENERATED_MAKEFILES, and one of them -- ccec/src/Makefile -- is not toolchain
#        output at all: it is a hand-written RDK build file that carries real source
#        content, and it is in AC_CONFIG_FILES anyway, so `configure` clobbers it by
#        design.  `git checkout --` restores whatever the INDEX holds, which makes it
#        correct only under an assumption about the index and makes it silently DESTROY
#        an uncommitted edit to any of the six.  A measurement tool does not get to lose
#        someone's work as a side effect of measuring.
#        So `--build` SNAPSHOTS the working-tree bytes of all six immediately before it
#        runs autoreconf, and restores from that snapshot at the end of the SAME
#        invocation -- exactly what was there, committed or not, index or no index.  The
#        snapshot lives in a run-scoped mktemp -d directory registered with the same
#        EXIT/INT/TERM trap as everything else here, so a cancelled build restores too.
#        A standalone `--restore` that finds the six dirty and has no snapshot from its
#        own invocation REFUSES and exits non-zero, because the only thing it could
#        otherwise reach for is the `git checkout` this clause has just forbidden.
#        Its two side effects are stated rather than buried:
#          (a) $HOME is NOT one of them, and that is the point.  lcov reads
#              $HOME/.lcovrc silently, and a branch-disabled copy there would defeat
#              everything above -- CI plants exactly such a copy in the plugin jobs, so
#              the hazard is real.  This script does not move that file aside: it gives
#              every lcov and genhtml invocation a PRIVATE, EMPTY, mode-0700 HOME of its
#              own (see lcov_run/genhtml_run), so no home configuration can be in effect
#              and the caller's file is never opened, moved, replaced or deleted.
#              DO NOT REPLACE THIS WITH A STASH-AND-RESTORE OF ~/.lcovrc: the restoring
#              `mv -f` silently DESTROYS a ~/.lcovrc that reappeared during the run --
#              the owner's, or a concurrent sibling runner's -- and a file recreated
#              mid-run is in effect for every lcov call after it.  A private HOME has
#              neither failure mode and needs no trap to undo.
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
#        run's evidence.
#        SO THE ORDER IS: ZERO ONCE, RUN THE MATRIX, CAPTURE ONCE.  That is a change from
#        the per-run zeroing this clause used to describe, and the reason is the reason
#        the matrix exists at all.  The HAL back-end selection resolves ONCE PER PROCESS,
#        inside LibCCEC::init, so one binary run can only ever exercise one arm of the
#        selection branch; covering both needs several processes, and ACCUMULATION ACROSS
#        THEM is the only way anything ever sees both arms.  Zeroing before each
#        invocation would leave the capture describing only the last one -- a report that
#        looks complete and covers half the design.  Never zeroing would let an earlier
#        run's evidence in, which is what this clause has always forbidden.  Zeroing once
#        before the first invocation and capturing once after the last one has PASSED has
#        neither failure mode, and the anti-stale guarantee is untouched: the figures are
#        still an account of THIS run and of nothing before it.
#        The verification is unchanged and still does not trust the tool: after zeroing,
#        the *.gcda files are RE-COUNTED rather than `lcov --zerocounters`'s exit status
#        being believed, and a survivor is fatal.  Nothing is captured unless the
#        invocations produced fresh counters.
#        ONE BUILD, ONE MACHINE, and this is a constraint rather than a convenience: gcov
#        counters cannot accumulate across machines, and a counter file from a different
#        build does not describe the same objects.  Every invocation therefore runs
#        against the tree this invocation of the script measured, which is also why the
#        hosted CI job cannot contribute its run to a capture made anywhere else.
#        Without `--run` the script reports how many counter files it found and when they
#        were last written, so a stale measurement is visible rather than silent.
#        A ZERO EXIT STATUS IS NOT TAKEN AS PROOF THAT ANY TEST RAN.  GoogleTest exits 0
#        when a filter selects nothing, so `GTEST_EXTRA_ARGS=--gtest_filter=NoSuchTest.*`
#        would otherwise be reported as "the L1 suite passed" and then measured at 0.0% -- a run
#        that failed only because the number happened to be zero, not because anything
#        asserted it had tested nothing.  A filter that still cleared the bar would have
#        been reported as a pass.  `--run` therefore DELETES the results file before the
#        binary starts and then requires it to exist, to report a non-zero test count, and
#        to name at least one of this suite's own fixtures -- the same three checks the
#        sibling plugin runners apply, for the same reason.
#     5. DETERMINISTIC AND ISOLATED.  Fixed artifact NAMES with no timestamp in any of them,
#        no wall-clock sleeps, no reliance on the caller's working directory: every path is
#        resolved absolutely from BASH_SOURCE, so running this script from its own
#        directory, from the submodule root or from the superproject root produces
#        identical results.  The coverage FIGURES are a pure function of the trace, so the
#        same trace always yields the same table -- with one deliberate exception, which is
#        stated rather than hidden: the provenance artifact and the leading `# PROVENANCE`
#        comment of the per-file TSV record the generation time in UTC, so an artifact found
#        later can be dated.  Those lines are the only non-reproducible bytes.  The HTML report is
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
#   ARTIFACT CUSTODY IS THE SAME ON BOTH BRANCHES, and that is a correction rather than a
#   restatement.  A directory this script MINTS was already owner-only; a directory a
#   caller NAMED used to be accepted exactly as found, and the files inside it were created
#   at the caller's umask.  Under `umask 000` and a pre-existing mode-0755 directory, every
#   artifact was written 0666 and an unprivileged local account could read them, append to
#   them, and forge the gate's own input (a fabricated 100.0% row in
#   per_file_coverage.tsv) or squat on .run.lock to defeat the concurrency guard.  Three
#   things now hold for both branches:
#     * `umask 077` is set before anything is resolved, so every file this run or any of
#       its children creates is 0600 and every directory 0700 -- see the block above
#       `set -euo pipefail`'s successor at the top of the executable section;
#     * a caller-named directory is brought to mode 0700 and then asserted private
#       (restrict_artifact_dir_to_owner), the same two steps the minted branch ends with;
#     * a caller-named path is COLLAPSED lexically and then checked for where it lands
#       (canonicalise_path_lexically, assert_artifact_location_plausible), so a near-root
#       value or one whose '..' components collapse into a system tree is refused instead
#       of created -- the refusal both plugin runners already made and this one did not.
#
# ==============================================================================
# USAGE
# ==============================================================================
#   ./run_coverage.sh                       measure existing profile data, report, gate
#   ./run_coverage.sh --run                 zero counters once, run the whole invocation
#                                           matrix, capture once, then the above
#   ./run_coverage.sh --build --run         build first, then the above
#   ./run_coverage.sh --threshold 90        raise the bar for a diagnostic run
#   ./run_coverage.sh --per-file-gate       also fail if any single file is below the bar
#                                           (the default -- Directive 4 states the bar PER TARGET)
#   ./run_coverage.sh --no-per-file-gate    opt out: gate on the aggregate only, and report the
#                                           per-file breaches without failing on them
#   ./run_coverage.sh --output-dir DIR      write artifacts to DIR
#   ./run_coverage.sh --no-html             skip the genhtml report
#   ./run_coverage.sh --restore             restore the six tracked Makefiles from this
#                                           invocation's snapshot, then exit (refuses when
#                                           they are dirty and no snapshot exists)
#   ./run_coverage.sh --help                full option and environment reference
#
#   ENVIRONMENT (all optional; command-line flags win)
#     COVERAGE_MIN            line-coverage bar, default 80
#     COVERAGE_OUTPUT_DIR     artifact directory, default as under ARTIFACT HYGIENE
#     COVERAGE_PER_FILE_GATE  1 to gate per file as well as on the aggregate, default 1
#                             (0 gates the aggregate only; the per-file enumeration is
#                             printed either way).  Directive 4 states the bar PER TARGET,
#                             so the per-file half is ON unless it is switched off explicitly
#     SUITE_TIMEOUT           wall-clock bound in seconds on the L1 suite under --run,
#                             default 600; exceeding it is reported as a hang, not a failure
#     GTEST_PREFIX            prefix providing libgtest/libgmock, default <WS>/install/usr
#     GTEST_EXTRA_ARGS        extra arguments appended to the run_L1Tests command line
#
#   ARTIFACTS (fixed names, written under the output directory)
#     coverage.info              raw capture over the whole submodule -- and the BRANCH GATE'S
#                                INPUT, deliberately unfiltered (see ONE TRACE, TWO CONSUMERS)
#     filtered_coverage.info     production-source-only trace, after the seven globs; the
#                                CI-interchangeable form, and the input to the step below
#     line_gate_coverage.info    filtered_coverage.info with the dependency headers removed --
#                                the LINE GATE'S INPUT, and what the per-file table, the
#                                uncovered-line list, the summary and the HTML report describe
#     coverage/index.html        genhtml report, with a Branches column
#     per_file_coverage.tsv      machine-readable per-file LINE/FUNCTION/BRANCH figures
#     uncovered_lines.txt        per-file uncovered line numbers for below-bar targets
#     rdkTestResults_invocation_<L>.json   GoogleTest results, ONE PER INVOCATION under --run
#     run_invocation_<L>.log               that invocation's captured output, one per invocation
#     capture.log filter.log genhtml.log build.log
#
#   PER-INVOCATION NAMES CARRY A LETTER, NEVER A TIMESTAMP.  The suite is now run once per
#   selection outcome (see THE INVOCATION MATRIX below), and a single results file would mean
#   the last run's evidence standing in for all of them -- an empty or failed invocation erased
#   by whichever green one came after it.  The letter keeps them distinct while keeping clause 5
#   intact: the file set is a fixed, predictable function of the matrix, so the same tree
#   produces the same names and a reader does not have to list the directory to find them.
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
#   CXXFLAGS="-fprofile-arcs -ftest-coverage" ./configure --enable-l1tests \
#       [HALIF_PREFIX=DIR] [HALIF_LIB_DIR=DIR] \
#       [BINDER_SDK_DIR=DIR] [BINDER_SDK_INCLUDE_DIR=DIR]
#   make -j$(nproc) all && make -C tests/L1Tests all && make -C tests/L2Tests all
#
#   THE STUB RECIPE IS SPELLED TWICE IN THIS FILE -- here, and executed in do_build -- and
#   the two are kept identical on purpose: a documented recipe that has drifted from the
#   one that runs is worse than no documentation, because it is trusted.  It gained no new
#   entry for the AIDL back-end, and that is not an omission: stubs/ mutes the IARM bus
#   headers this middleware includes but does not ship, and substitutes the HAL driver mock
#   for the real legacy driver header.  The AIDL headers are REAL and are found through the
#   prefixes below, so there is nothing to stub.
#
#   THE FOUR AIDL/BINDER PREFIXES ARE OPTIONAL ON THE COMMAND LINE AND MANDATORY IN EFFECT.
#   libRCEC links the generated AIDL client stubs and the Binder client library
#   unconditionally, for every SOC vendor, so the build needs their headers and libraries --
#   but this script names no path for them.  configure.ac declares all four with AC_ARG_VAR
#   and DERIVES from them the four include roots (the hdmicec and common snapshot roots, the
#   common/current root that is the only source of halcompat.h, and the Binder header root),
#   the -L directories, the four -l edges and the C++17 flag.  Omit them and configure looks
#   for the sibling rdk-halif-aidl checkout; get them wrong and configure fails naming the
#   roots it tried.  Either way there is ONE place that decides, which is what keeps the
#   Autotools build and the hand-written ccec/src/Makefile from being configured differently.
#
#   `make -C tests/L2Tests all` is the third make and builds two programs: the L2 runner and
#   the out-of-process fake service host.  Without it invocations D and E have no binary.
#
#   The PKG_CONFIG_PATH line is the one addition to the workflow's own command, and it is
#   not optional off a CI runner: configure.ac uses PKG_CHECK_MODULES for GoogleTest, which
#   consults pkg-config ONLY and ignores -I/-L.  CI additionally apt-installs libgtest-dev,
#   so a system gtest.pc satisfies the check there; a host whose GoogleTest is a
#   source-built copy in a private prefix needs the prefix on pkg-config's path or configure
#   aborts with "Google Test not found".
#
#   MANDATORY BUILD HYGIENE.  autoreconf and configure REWRITE six GIT-TRACKED Makefile
#   files in this submodule (REGENERATED_MAKEFILES below).  Five of them are pure local
#   toolchain output.  THE SIXTH IS NOT: ccec/src/Makefile is a hand-written RDK build
#   file with its own object list and link command -- real source content -- and it is
#   listed in configure.ac's AC_CONFIG_FILES all the same, so configure clobbers it by
#   design.  Measured, not assumed: after a `--build` on this host the regenerated copy
#   no longer contains the DriverAidlImpl.o entry the hand-written one carries.
#
#   SO `--build` SNAPSHOTS ALL SIX AND RESTORES THEM ITSELF.  The snapshot is taken from
#   the WORKING TREE immediately before autoreconf runs, held in a run-scoped mktemp -d
#   directory, and restored at the end of the same invocation from the trap that already
#   cleans up the private lcov HOME -- so a cancelled or failed build restores too.
#   Nothing is left for anyone to paste.
#
#   DO NOT REVERT THESE SIX WITH `git checkout`, AND THIS SCRIPT NO LONGER SUGGESTS IT.
#   A blanket checkout over those paths restores whatever the INDEX holds: it is right
#   only under an assumption about the index, and it silently DESTROYS an uncommitted
#   edit to any of the six -- an edit to ccec/src/Makefile most of all, that being the
#   one a person actually works on.  Restoring the bytes that were there is
#   unconditionally correct; restoring the bytes git happens to hold is not.  `--restore`
#   therefore restores from a snapshot or refuses: with the six dirty and no snapshot
#   from its own invocation it exits non-zero and says why, rather than reaching for git.
#   No `git checkout` is executed anywhere in this script, in any mode.
#
# ==============================================================================
# ONE TRACE, TWO CONSUMERS
# ==============================================================================
#   The capture is taken ONCE and read in two forms, because the two gates ask different
#   questions and neither form answers both.  This is stated up here rather than left to
#   the function that does it, because "two gates read two files" looks like a defect
#   until the reason is in front of the reader.
#
#   THE BRANCH GATE READS THE UNFILTERED CAPTURE.  Since libRCEC gained the AIDL
#   back-end, some of the branches that have to be accounted for live in header-only code
#   outside this submodule -- above all in halcompat.h's isCompatible<>(), whose empty-hash,
#   "-1", "notfrozen", era and major arms are exactly what the selection rests on.  Filter
#   first and those records are gone, and a branch gate that cannot see its own subject
#   passes silently and for ever.
#
#   THE LINE GATE READS A DERIVATIVE with the dependency headers removed, and only them.
#   Its 80% threshold is an aggregate, and an aggregate means nothing without a stable
#   denominator.  MEASURED: the capture holds 140 source-file records; the seven globs
#   reduce that to 42; and of those, 31 are this submodule's own and ELEVEN are dependency
#   headers -- six from the binder SDK, four generated AIDL stubs, and halcompat.h.  Left
#   in, they move the denominator away from the one the baseline was measured over and,
#   because per-file gating is on, start failing runs over how well this suite covers a
#   third-party header it neither owns nor tests.
#
#   halcompat.h IS RETAINED in the derivative: it is a consumed HALIF header called from
#   the selection path, not toolchain code.  ccec/src/Driver.cpp and
#   ccec/src/DriverAidlImpl.cpp are never filtered from either form, and their presence is
#   ASSERTED after filtering rather than inferred from the globs not naming them.
#
#   The seven globs themselves are UNTOUCHED and nothing was added to them -- clause 6
#   holds.  The dependency exclusions are a separate list applied in a separate step
#   producing a separate artifact, so filtered_coverage.info still means what its comment
#   has always said it means.
#
# ==============================================================================
# THE LEGACY-PATH BASELINE -- REFERENCE ONLY, AND LABELLED AS SUCH
# ==============================================================================
#   These are the figures this suite measured BEFORE the AIDL back-end existed, on the
#   legacy path alone.  They are recorded here so a later run has something to compare
#   against, and they are labelled because a number without its provenance is the thing
#   clause 4 exists to prevent:
#
#     483 tests from 18 suites, all passing, 6545 ms
#     aggregate over 30 source files: 94.5% line (1999/2115), 93.2% function (425/456),
#                                     58.4% branch (1122/1920)
#     ccec/src/DriverImpl.cpp  97.5% line / 94.4% function / 66.2% branch
#     ccec/src/LibCCEC.cpp     100%  line / 100%  function / 65.9% branch
#
#   THEY ARE NOT A POST-CHANGE EXPECTATION AND MUST NOT BE READ AS ONE.  The suite is
#   larger now, ccec/src/DriverAidlImpl.cpp is in the denominator, and -- decisively -- the
#   figures a run produces depend on HOW MANY INVOCATIONS THE HOST COULD RUN.  On a host
#   with no binder driver, three of the five are deferred and the AIDL back-end's source is
#   unmeasured by construction, so the aggregate is necessarily lower and says nothing about
#   the test set.  NO POST-CHANGE AGGREGATE IS WRITTEN DOWN HERE, on purpose: the only
#   honest source for one is a full five-invocation run on a binder-capable host, and this
#   script prints what it measured on whatever host it ran on rather than carrying a number
#   somebody would then compare the wrong thing against.
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
#   THE PER-FILE HALF OF THE GATE IS ON BY DEFAULT.  Directive 4 sets the bar PER TARGET,
#   so an aggregate that clears 80% while one file sits at 33% clears a total and misses the
#   requirement.  Every file in this suite's filtered trace is now at or above the bar, so
#   there is nothing for the per-file half to be lenient about.
#
#   That includes the three template-heavy headers under ccec/include that measured below it at
#   the engagement baseline: ccec/include/ccec/Operand.hpp 0.0% (0/6),
#   Exception.hpp 33.3% (4/12), Messages.hpp 76.5% (228/298).  Each was closed the only way a
#   coverage bar may be closed, by ADDING TESTS: Operand's three default virtuals and all
#   seven Exception::what() overrides are exercised from ccec/test_Operands.cpp, and the
#   previously unserialised message types from ccec/test_MessageEncoder.cpp.  None of them
#   was exempted, and none was excluded from the trace.
#
#   `--no-per-file-gate` (or COVERAGE_PER_FILE_GATE=0) downgrades the per-file half to a
#   report, for a diagnostic run that wants the table without the verdict.  It is not a
#   setting an acceptance run uses.  COVERAGE_GATE_EXEMPT_FILES -- empty by default -- can
#   suppress one named file's vote, and the rules for when that is admissible are stated at
#   its definition below.  The enumeration prints in every mode, so neither switch can hide a
#   figure; they only decide whether it fails the run.
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
#   default order and are not rewritten, so a shuffled run's outcome depends on the seed --
#   green for some, assertion failures for others, and an exit 139 from a production race
#   for others again (the measured seed table is in tests/L1Tests/README.md).  A result that
#   depends on the seed must never gate anything.  Nothing here enables it, and no watch
#   mode exists or is added.
#
# ==============================================================================
set -euo pipefail

# ------------------------------------------------------------------------------------
# FILE MODE FOR EVERYTHING THIS RUN CREATES.  Set here, before any path is resolved and
# long before any byte is written, because it is inherited by every child too -- lcov,
# genhtml, gcov and the test binary all create files in this run's name.
#
# WHY IT IS NOT ENOUGH TO CHMOD THE DIRECTORY.  The artifact DIRECTORY was already
# created 0700 on the branch that mints it, but the FILES inside it were created at
# whatever umask the caller happened to have.  Under a permissive umask -- `umask 000` is
# the case that was demonstrated -- coverage.info, filtered_coverage.info,
# per_file_coverage.tsv, uncovered_lines.txt, capture.log, filter.log and .run.lock were
# all written 0666.  Inside a caller-supplied directory that was left world-traversable,
# an unprivileged local account could then READ them, APPEND to them, and FORGE the very
# file the gate reads: a fabricated 100.0% row in per_file_coverage.tsv, or a .run.lock
# it can hold to break the concurrency guard.  For a script whose only product is
# trustworthy coverage evidence, that is the failure that matters -- not confidentiality
# (the artifacts hold source paths and counts, never secrets) but INTEGRITY.
#
# 077 rather than 022: group and other get nothing at all.  Nothing in this pipeline is
# read by another account -- the suite, lcov, genhtml and the gate all run as this user
# in this process tree -- so there is no consumer to break, and CI collects artifacts as
# the same user that produced them.  The HTML report stays fully readable by its owner.
# The one file that already chmod'd itself to 600 (provenance.txt) keeps doing so; this
# makes every other artifact match it instead of being the exception.
# ------------------------------------------------------------------------------------
umask 077

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

# `timeout` bounds the suite run under --run, and do_run() REFUSES to run the suite without it
# rather than degrading to an unbounded run -- an unbounded run of a suite that drives real
# mutexes, condition variables and threads can hang for ever with no output and no verdict.
# Resolved here with everything else so a later PATH change cannot swap it, and resolved
# unconditionally because an invocation that only measures existing counters does not need it.
TIMEOUT_BIN="$(resolve_tool timeout)"

# --kill-after is desirable (a suite that ignores SIGTERM still dies) but is NOT universally
# safe: this workspace's `timeout` is uutils coreutils, and with -k it reports a timeout as exit
# 125 rather than GNU's 124 -- and 125 also means "timeout itself failed", so the two become
# indistinguishable and a hang would be misreported as a broken invocation.  One cheap probe
# settles it for this host instead of inferring it from a version string: a 1s bound on a 3s
# sleep must yield exactly 124 before -k is used at all.  This is the probe the run step's
# comment refers to.
TIMEOUT_KILL_AFTER=()
if [ -n "$TIMEOUT_BIN" ]; then
    timeout_probe=0
    "$TIMEOUT_BIN" -k 1 1 sleep 3 >/dev/null 2>&1 || timeout_probe=$?
    if [ "$timeout_probe" -eq 124 ]; then
        TIMEOUT_KILL_AFTER=(-k 30)
    fi
    unset timeout_probe
fi
readonly TIMEOUT_BIN TIMEOUT_KILL_AFTER
# stat is how the ancestry of every artifact path is checked (owner, mode, type) before a
# byte is written to it.  It is coreutils, like the rest of the primitives above.
STAT_BIN="$(resolve_tool stat)"
readonly LCOV_BIN GENHTML_BIN GCOV_BIN AWK_BIN FIND_BIN MKTEMP_BIN STAT_BIN

# ------------------------------------------------------------------------------------
# Configuration.  Every value is overridable from the environment, and the defaults are
# the ones that reproduce CI.  Command-line flags are applied after parsing and win.
# ------------------------------------------------------------------------------------
COVERAGE_MIN="${COVERAGE_MIN:-80}"
# Per-file gating defaults ON, because Directive 4 states the bar PER TARGET: an aggregate
# that clears 80% while one target sits at 33% satisfies the letter of a total and none of
# the intent.  `--no-per-file-gate` / COVERAGE_PER_FILE_GATE=0 downgrades it to a report for
# a diagnostic run; it is not a setting an acceptance run uses.  See GATE in the header.
COVERAGE_PER_FILE_GATE="${COVERAGE_PER_FILE_GATE:-1}"

# ------------------------------------------------------------------------------------
# THE PER-FILE GATE'S EXEMPTION LIST -- newline-separated trace paths, matched WHOLE-LINE
# against the `file` column of per_file_coverage.tsv (which is relative to $WS).
#
# EMPTY BY DEFAULT, AND THAT IS THE POINT.  Every file in the filtered trace of this suite
# is at or above the bar, including the three template-heavy ccec/include headers that used
# to sit below it -- Operand.hpp (was 0/6), Exception.hpp (was 4/12) and Messages.hpp (was
# 228/298).  They were closed the only way a coverage bar may be closed: by adding tests
# (ccec/test_Operands.cpp and ccec/test_MessageEncoder.cpp), never by listing them here.
#
# WHAT AN ENTRY HERE MEANS, AND WHAT IT DOES NOT.  An entry suppresses one file's per-file
# pass/fail VOTE.  It does not remove the file from the trace, from the aggregate
# denominator, from the per-file table, or from the uncovered-line enumeration -- all four
# still report it, and apply_gate() prints every exempt file with its figure and a pointer
# back to this comment.  So an exemption is a visible, argued decision, not a filter.
#
# THE ONLY ADMISSIBLE REASON is that a line cannot be reached from a test-only change --
# Directive 6 puts production source out of scope, and Directive 4 then requires the
# specific lines and the reason to be enumerated in the traceability report.  "No test
# covers it yet" is not that reason: it is a gap, and the answer to a gap is a test.
# An entry added for any other reason lowers the bar while appearing to hold it.
#
# FORMAT, with the reason on the line above each path, e.g.
#     COVERAGE_GATE_EXEMPT_FILES="hdmicec/ccec/src/Example.cpp"
# and for more than one, one path per line inside the quotes.
# ------------------------------------------------------------------------------------
COVERAGE_GATE_EXEMPT_FILES="${COVERAGE_GATE_EXEMPT_FILES-}"

# Wall-clock bound in seconds on the run_L1Tests invocation under --run.  This suite normally
# finishes in about two seconds, so 600 is generous by two orders of magnitude and exists only
# so that a deadlock -- this suite drives real pthread mutexes, condition variables and threads
# -- is reported as a HANG with the last test named, instead of blocking for ever with no
# output, no exit status and no gate verdict.  0 is refused rather than honoured, because
# timeout(1) reads it as "no limit" and would silently restore the unbounded run.
SUITE_TIMEOUT="${SUITE_TIMEOUT:-600}"
# Artifact directory.  EMPTY BY DEFAULT, and that is the security-relevant part: with no
# explicit choice this script creates its root with `mktemp -d` under $TMPDIR, so the name
# is unpredictable and the directory is created atomically at mode 0700 (see
# resolve_output_dir).  DO NOT GIVE IT A FIXED DEFAULT: a predictable path such as
# ${TMPDIR:-/tmp}/hdmicec-l1-coverage/<workspace basename> is guessable, and anything able to
# create entries in a world-writable $TMPDIR could pre-create that name as a symlink or as a
# directory of its own and collect, redirect or tamper with every trace, log and HTML page
# written under it.  An unpredictable name cannot be pre-created.
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

# ------------------------------------------------------------------------------------
# THE AIDL/BINDER STAGING PREFIXES -- four names, and NOT ONE HARD-CODED PATH.
#
# libRCEC now links the generated AIDL client stubs and the Binder client library
# unconditionally, for every SOC vendor, so the build this script drives needs to find their
# headers and libraries.  It does NOT get to decide where those are.
#
# WHY THE NAMES ARE THESE FOUR AND NOT NAMES OF THIS SCRIPT'S CHOOSING.  configure.ac declares
# exactly these four as AC_ARG_VAR precious variables and derives everything else from them --
# the four include roots, the -L directories, the four -l edges and the C++17 dialect flag are
# all AC_SUBSTed outputs computed inside configure, not inputs anybody supplies twice.  Two
# staging prefixes are the whole of the orchestrator contract and the other two are overrides
# for split staging layouts, and the hand-written ccec/src/Makefile reads the same four names
# from the environment for exactly the same reason.  So this script passes the four through and
# lets configure derive the rest: a path spelled here as well would be a second place for the
# two build systems to disagree, which is the failure the single-prefix contract exists to
# prevent.
#
#   HALIF_PREFIX            root of the staged rdk-halif-aidl tree.  Left unset, configure
#                           falls back to the sibling checkout beside this submodule, which is
#                           the normal in-workspace arrangement -- so this stays EMPTY by
#                           default rather than duplicating that fallback here and risking a
#                           different answer.
#   HALIF_LIB_DIR           stub library directory, when it is not HALIF_PREFIX/lib/halif
#   BINDER_SDK_DIR          root of the staged Binder SDK, holding lib/binder and
#                           include/binder_sdk
#   BINDER_SDK_INCLUDE_DIR  Binder header prefix, when it is not under BINDER_SDK_DIR
#
# Every one of them is read from the environment and passed on untouched.  An unset variable is
# passed as unset rather than as an empty string, because `configure BINDER_SDK_DIR=` is a
# different instruction from omitting it: the first says "there is no Binder SDK", which fails
# the configure-time check, and the second lets configure look where it knows to look.
# ------------------------------------------------------------------------------------
HALIF_PREFIX="${HALIF_PREFIX:-}"
HALIF_LIB_DIR="${HALIF_LIB_DIR:-}"
BINDER_SDK_DIR="${BINDER_SDK_DIR:-}"
BINDER_SDK_INCLUDE_DIR="${BINDER_SDK_INCLUDE_DIR:-}"

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

# ------------------------------------------------------------------------------------
# THE DEPENDENCY-HEADER EXCLUSIONS -- a SECOND, SEPARATE list, and separate on purpose.
#
# WHY THEY ARE NOT ADDED TO THE SEVEN ABOVE.  Clause 6 says the seven exclusion globs are
# reproduced verbatim from the workflow and that NOTHING is added to them, and that is not
# ceremony: those seven are what make a local run's filtered trace interchangeable with CI's
# artifact bundle, and a list that has quietly grown by two is a list that no longer means what
# its comment says.  So this is a second list, applied in a second step, producing a third
# artifact -- and the seven-glob step keeps its name, its content and its meaning untouched.
#
# WHY A SECOND LIST IS NEEDED AT ALL.  Since libRCEC gained the AIDL back-end, the compiled
# objects reference header-only code from outside this submodule: halcompat.h's templates, the
# generated Bp*/Bn* inline definitions, and binder SDK headers.  gcov attributes records to
# those paths, and NONE of the seven globs describes them.  MEASURED on this host: the capture
# holds 140 source-file records, the seven globs reduce that to 42, and of those 42 exactly 31
# are inside this submodule while ELEVEN are dependency headers -- six binder SDK headers, four
# generated AIDL stub headers, and halcompat.h.  Left in, they change the file set the per-file
# gate judges and the denominator the aggregate is computed over, so a figure produced now would
# not be comparable with the recorded baseline and the per-file gate would start voting on
# whether a binder SDK header is well tested by this suite.
#
# WHAT IS FILTERED AND WHAT IS DELIBERATELY KEPT:
#
#   '*/binder_sdk/*'      the binder SDK header root.  configure.ac fixes that directory name
#                         -- the header root is <prefix>/include/binder_sdk -- so the glob
#                         describes the layout rather than one host's path.  This is third-party
#                         toolchain code that this suite neither owns nor tests.
#   '*/com/rdk/hal/*'     the GENERATED AIDL stub headers.  The C++ AIDL backend package-paths
#                         every generated header under com/rdk/hal/, so this one glob catches
#                         them from any prefix and at any snapshot version -- which a
#                         version-spelled glob would not.  They are committed pre-generated
#                         code, not source anyone in this repository writes.
#
#   halcompat.h IS RETAINED, and that is the important half of this policy.  It lives at
#   <halif prefix>/common/current/halcompat.h -- OUTSIDE com/rdk/hal/, which is precisely why
#   the glob above separates the two cleanly rather than by luck -- and it is a CONSUMED HALIF
#   HEADER rather than third-party toolchain code: getService<I>() and isCompatible<I>() are
#   called from the selection path, their branches are branches this migration is answerable
#   for, and SC6 asks for them by name.  Filtering it would delete the evidence.
#
# Anything the caller told us about the staging layout is added below, at resolve time, so a
# Yocto-style layout whose binder headers do not sit under a binder_sdk directory is still
# described.  A prefix that was never set contributes nothing.
# ------------------------------------------------------------------------------------
readonly DEPENDENCY_EXCLUDES=(
    '*/binder_sdk/*'
    '*/com/rdk/hal/*'
)

# Source files that must NEVER disappear from either trace, whatever any glob says.  They are
# the two production files this migration is answerable for, and a filter that removed one would
# leave a green gate over unmeasured code -- so their presence is ASSERTED after filtering rather
# than assumed from the globs not naming them.  Paths are relative to the submodule root.
readonly PROTECTED_TRACE_FILES=(
    'ccec/src/Driver.cpp'
    'ccec/src/DriverAidlImpl.cpp'
)

GENHTML_TITLE='hdmicec coverage'
# NOT readonly: write_provenance() appends the superproject's short SHA so that every page of
# the HTML report carries the revision it was produced from. A trace or a report that does not
# say which tree it came from cannot be relied on, which is exactly how a previous round's
# artifacts ended up unusable - they were produced in one clone and read as though they applied
# to another.
PROVENANCE_TXT=''

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

# ------------------------------------------------------------------------------------
# The six git-tracked Makefiles that autoreconf/configure rewrite (see BUILD RECIPE).
#
# FIVE OF THESE ARE TOOLCHAIN OUTPUT AND THE SIXTH IS NOT, which is the whole reason the
# snapshot machinery below exists.  ccec/src/Makefile is a hand-written RDK build file --
# its own CFLAGS, its own nine-entry object list, its own link command -- and it is in
# configure.ac's AC_CONFIG_FILES anyway, so configure overwrites it with generated content
# every time.  Restoring it is therefore not "throwing away generated noise": it is putting
# a source file back, and the only mechanism that does that correctly under every condition
# is one that remembers the bytes.
# ------------------------------------------------------------------------------------
readonly REGENERATED_MAKEFILES=(
    Makefile
    ccec/Makefile
    ccec/src/Makefile
    osal/Makefile
    osal/src/Makefile
    tests/Makefile
)

# ------------------------------------------------------------------------------------
# THE MAKEFILE SNAPSHOT -- what replaced `git checkout`, and why it is not the same thing
# wearing a different name.
#
# WHAT WAS WRONG WITH THE OLD MECHANISM.  `do_restore` used to run
# `git -C <hdmicec> checkout -- <the six>`, `announce_regenerated_makefiles` printed that
# exact command for the caller to paste, and the BUILD RECIPE block in the header
# documented it a third time.  All three restore WHATEVER THE INDEX HOLDS, which is a
# different claim from "what was there before configure ran", and the difference is not
# academic: an uncommitted edit to any of the six is destroyed outright, with no prompt and
# no copy.  ccec/src/Makefile is precisely the file someone edits -- it is hand-written
# source, not output -- so the one path where the old mechanism was most dangerous is also
# the one it was most likely to be used on.
#
# WHAT THIS DOES INSTEAD.  `--build` copies the working-tree bytes of all six into a
# run-scoped mktemp -d directory immediately before autoreconf, and the EXIT/INT/TERM trap
# restores from that copy at the end of the same invocation.  It is unconditionally correct:
# it does not consult the index, does not care whether the file is committed, modified,
# staged or pristine, and cannot revert anything that was not itself about to be clobbered.
# A cancelled build restores too, because the restore hangs off the trap rather than off the
# build's success path.
#
# WHAT IT DELIBERATELY DOES NOT DO.  It does not fall back to git when there is no snapshot.
# A fallback would reintroduce exactly the destruction this replaced, at the one moment a
# caller is least able to notice -- and "we could not do the safe thing so we did the unsafe
# thing" is not a recovery.  A standalone `--restore` that finds the six dirty with no
# snapshot of its own says so and exits non-zero.
#
#   MAKEFILE_SNAPSHOT_DIR    the run-scoped copy, empty when none has been taken
#   MAKEFILE_SNAPSHOT_TAKEN  newline-separated list of the paths actually captured, so the
#                            restore puts back exactly what was saved and a file that did
#                            not exist beforehand is not conjured into existence
# ------------------------------------------------------------------------------------
MAKEFILE_SNAPSHOT_DIR=''
MAKEFILE_SNAPSHOT_TAKEN=''

# ------------------------------------------------------------------------------------
# THE BUILD LOCK -- tree-scoped, not artifact-scoped, and that distinction is the point.
#
# acquire_output_lock() below already serialises two runs that share an --output-dir.  It
# cannot serialise these: two runs with two different minted artifact roots build in the
# SAME working tree, so one can snapshot while the other is half way through configure and
# then "restore" the other run's generated content as though it were the original.  The
# shared resource is the tree, so the lock has to be keyed to the tree.
#
# The name is derived from the absolute path of the submodule root, because that is what
# makes it tree-scoped: two clones of this superproject under /tmp/blitzy get two different
# locks and do not block each other, which is the behaviour parallel clones need.
#
# A DERIVED NAME IN A SHARED DIRECTORY IS PREDICTABLE, and that trade-off is accepted here
# rather than glossed: the ancestry checks (assert_safe_ancestry) still refuse a symlinked
# or foreign-owned component, the lock file itself is refused if it is a symlink or not a
# regular file, it is opened with `>>` so nothing is truncated, and if a local account
# squats the name then flock fails and THIS RUN REFUSES TO PROCEED.  Every failure mode
# lands on "do not build", which is the safe direction for a lock whose job is to stop two
# builds interleaving.
# ------------------------------------------------------------------------------------
BUILD_LOCK_FD=''
BUILD_LOCK_PATH=''

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
# Checking only the leaf does not close that, and must not be reduced to it: the leaf can be
# perfectly ordinary while its PARENT is the substitution.  So the whole chain
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

# ------------------------------------------------------------------------------------
# LEXICAL CANONICALISATION -- collapse '.', '..' and doubled slashes, and NOTHING ELSE.
#
# WHY IT IS NEEDED.  Absolutising a caller-supplied path is not the same as canonicalising
# it.  `--output-dir "$SANDBOX/ok/../../../../etc/name"` is already absolute, so it passed
# straight into the ancestry walk -- which then validated `$SANDBOX/ok/..`,
# `$SANDBOX/ok/../..` and so on as ordinary existing directories owned by root, created
# every missing component in turn, and wrote the artifacts into /etc/name.  Every
# individual check held; the PATH had simply left the tree the caller appeared to name.
# Collapsing the components first means the checks below, the location guard above and the
# messages a reader sees all describe the one directory that will actually be written to.
#
# WHY IT IS LEXICAL AND NOT `realpath`.  `realpath` (without --no-symlinks) RESOLVES
# symbolic links, which would quietly defeat this script's strongest guarantee: that it
# refuses to write THROUGH a link rather than following it (assert_safe_ancestry,
# create_safe_dir).  A resolved path has no links left to refuse, so the refusal would
# never fire again and a substituted component would be validated at its target instead.
# Collapsing textually keeps every link visible to those checks -- and needs no external
# tool, which also keeps the "no new dependency" clause intact.
#
# `local -` scopes the option change to this function, so `set -f` (no pathname expansion
# while the path is split on '/') cannot leak into the caller: without it a component
# containing '*' would be glob-expanded during the split.
# ------------------------------------------------------------------------------------
canonicalise_path_lexically() { # $1=absolute path -> canonical path on stdout
    local input="$1" out='' component saved_ifs
    local -
    set -f

    saved_ifs="$IFS"
    IFS='/'
    # Deliberate word splitting on '/' to walk the components in order.
    # shellcheck disable=SC2086
    set -- ${input#/}
    IFS="$saved_ifs"

    for component in "$@"; do
        case "$component" in
            ''|.)  : ;;                        # '' comes from a doubled slash; '.' is a no-op
            ..)    out="${out%/*}" ;;          # one level up, textually -- never via the filesystem
            *)     out="$out/$component" ;;
        esac
    done
    printf '%s\n' "${out:-/}"
}

# ------------------------------------------------------------------------------------
# WHERE AN ARTIFACT ROOT MAY NOT BE.  Both sibling plugin runners refuse a near-root
# artifact root outright ("implausibly short"); this file had no equivalent check, so
# `--output-dir /etc` was accepted and created /etc/.run.lock, and a '..'-collapsed value
# landed under /etc as well.  Three sibling scripts written together must not disagree
# about that, so the same refusal lives here now, with the system-location half made
# explicit rather than left to a length test.
#
# The list is deliberately SHORT and holds only trees the operating system owns.  /tmp,
# /var/tmp, /run/user/<uid>, /opt, /home and /root are all legitimate destinations and are
# NOT refused -- and neither is a path inside the checkout, because the header documents
# `--output-dir .` from the superproject root as the way to reproduce CI's layout byte for
# byte.  A guard that broke a documented usage would be a worse defect than the one it
# closes.
# ------------------------------------------------------------------------------------
readonly PROTECTED_SYSTEM_ROOTS=(
    /bin /boot /dev /etc /lib /lib32 /lib64 /libx32 /proc /run /sbin /sys /usr /var
)

assert_artifact_location_plausible() { # $1=canonical absolute path  $2=how it was chosen
    local path="$1" origin="$2" root

    case "$path" in
        /)  die "the artifact directory must not be '/' ($origin).  This script creates and
       overwrites fixed artifact names underneath it and takes a lock file there; the
       filesystem root is not a place to do that." ;;
        /*) : ;;
        *)  die "internal error: assert_artifact_location_plausible needs an absolute path;
       got '$path' ($origin)." ;;
    esac

    # Same test, same wording, as the two plugin runners: four characters or fewer cannot be
    # anything but a near-root directory.
    [ "${#path}" -gt 4 ] || die "the artifact directory '$path' is implausibly short ($origin).
       Fixed artifact names are created and overwritten underneath it, so a near-root path is
       refused.  Give a path that is unmistakably yours, for example
       \"\${TMPDIR:-/tmp}/hdmicec-l1-coverage\", or drop --output-dir entirely and let this
       script mint an unpredictable mode-0700 root with mktemp -d."

    # Checked BEFORE the protected-root loop, because /var/tmp and /run/user/<uid> are
    # ordinary per-user scratch directories that happen to live under a protected root.
    case "$path" in
        /var/tmp|/var/tmp/*|/run/user/*) return 0 ;;
    esac

    for root in "${PROTECTED_SYSTEM_ROOTS[@]}"; do
        case "$path" in
            "$root"|"$root"/*)
                die "refusing to write coverage artifacts to
           $path
       ($origin), because it is $root or lies underneath it -- a directory the operating
       system owns.  This script creates a directory there if it is missing, takes
       .run.lock inside it, and overwrites fixed artifact names on every run; none of that
       belongs in a system tree, and a value that reaches one is nearly always a '..' that
       collapsed out of the intended path or a mistyped root.
       Use \"\${TMPDIR:-/tmp}/hdmicec-l1-coverage\", a directory inside your own tree, or
       drop --output-dir / COVERAGE_OUTPUT_DIR and let this script mint an unpredictable
       mode-0700 root with mktemp -d." ;;
        esac
    done
    return 0
}

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

# ------------------------------------------------------------------------------------
# ONE PRIVACY POSTURE FOR BOTH BRANCHES OF THE ARTIFACT DIRECTORY.
#
# The minted branch already ended at `chmod 700` + assert_private_dir, so a directory this
# script created was owner-only.  A CALLER-SUPPLIED directory got neither: it was accepted
# as it was found, so a pre-existing mode-0755 directory stayed 0755 and every artifact
# written into it was reachable by any local account that could traverse it -- which is how
# an unprivileged user was able to read, append to and forge the gate's own input file.
# The caller-named branch is the predictable one, so if anything it warrants the stricter
# treatment, not the weaker.
#
# TIGHTENED RATHER THAN REFUSED, and only when the mode actually grants something away:
# the directory has already been proved to be OWNED by this user (assert_owned_and_private),
# so narrowing it is a change to the caller's own directory, it is announced when it
# happens, and it costs the caller nothing this pipeline needs.  Refusing instead would
# turn an ordinary `--output-dir ~/cov` into a hard failure over a bit this script can
# simply fix.  A chmod that does not take IS fatal -- proceeding would write evidence
# somewhere it can be replaced.
# ------------------------------------------------------------------------------------
restrict_artifact_dir_to_owner() { # $1=directory this run writes its artifacts into
    local dir="$1" mode
    mode="$(stat -c '%a' -- "$dir" 2>/dev/null)" \
        || die "cannot stat the artifact directory to check its mode: $dir"
    if [ "$(( 8#$mode & 0077 ))" -ne 0 ]; then
        chmod 700 -- "$dir" \
            || die "the artifact directory $dir is mode $mode -- readable or writable by other
       accounts -- and could not be tightened to 0700.  Everything written there is the
       evidence this run is judged on, and another account able to write it can replace a
       trace between the capture and the gate.  Fix its permissions, or point
       --output-dir / COVERAGE_OUTPUT_DIR at a directory you own."
        warn "tightened the artifact directory from mode $mode to 0700: $dir"
        warn "  Its contents are the trace, the table and the log the gate and the"
        warn "  traceability report rest on, so no other account may read or replace them."
    fi
    # The same assertion the minted branch makes, so both branches end in the same state
    # rather than in two states that merely look similar.
    assert_private_dir "$dir"
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

cleanup_makefile_snapshot() {
    [ -n "$MAKEFILE_SNAPSHOT_DIR" ] || return 0
    local snapshot="$MAKEFILE_SNAPSHOT_DIR"
    MAKEFILE_SNAPSHOT_DIR=''
    MAKEFILE_SNAPSHOT_TAKEN=''
    # Only ever a directory this script created with mktemp -d, never a caller-supplied path,
    # and the two-component pattern keeps a recursive remove away from '/' and '/anything'.
    case "$snapshot" in
        /*/*) [ -d "$snapshot" ] && rm -rf -- "$snapshot" ;;
        *)    warn "refusing to remove an implausible snapshot path: $snapshot" ;;
    esac
    return 0
}

# ------------------------------------------------------------------------------------
# Take the snapshot.  Called from do_build IMMEDIATELY BEFORE autoreconf -- not earlier,
# because a snapshot taken before the stub-header step would still be correct but would
# widen the window in which a concurrent build could interleave, and not later, because
# autoreconf is the first command that can rewrite any of the six.
#
# A file that does not exist is recorded as absent rather than faked: MAKEFILE_SNAPSHOT_TAKEN
# lists only what was actually copied, so the restore cannot conjure a file into a tree that
# never had one.  A file that exists and cannot be read is FATAL, because carrying on would
# mean building with no way back for that path.
# ------------------------------------------------------------------------------------
snapshot_regenerated_makefiles() {
    [ -n "$MKTEMP_BIN" ] || die "internal error: snapshot_regenerated_makefiles needs mktemp"
    [ -z "$MAKEFILE_SNAPSHOT_DIR" ] || die "internal error: a Makefile snapshot already exists at
       $MAKEFILE_SNAPSHOT_DIR
       Taking a second one would overwrite the record of what the tree looked like before
       this invocation touched it."

    local parent="${TMPDIR:-/tmp}"
    case "$parent" in
        /*) ;;
        *)  die "TMPDIR must be an absolute path to be checked safely; got: $parent" ;;
    esac
    parent="$(canonicalise_path_lexically "$parent")"
    assert_safe_ancestry "$parent/hdmicec-makefile-snapshot" minted

    MAKEFILE_SNAPSHOT_DIR="$("$MKTEMP_BIN" -d "$parent/hdmicec-makefile-snapshot.XXXXXXXX")" \
        || die "could not create a snapshot directory for the six tracked Makefiles under
       $parent
       Refusing to run autoreconf and configure, because they overwrite those six files and
       this run would then have no way to put them back.  'git checkout' is NOT the fallback:
       it restores what the index holds and would destroy any uncommitted edit."
    chmod 700 -- "$MAKEFILE_SNAPSHOT_DIR" \
        || die "could not restrict the Makefile snapshot directory to mode 0700: $MAKEFILE_SNAPSHOT_DIR"
    assert_private_dir "$MAKEFILE_SNAPSHOT_DIR"

    local f captured=0 absent=0
    for f in "${REGENERATED_MAKEFILES[@]}"; do
        if [ ! -e "$HDMICEC_ROOT/$f" ]; then
            absent=$((absent + 1))
            continue
        fi
        [ -f "$HDMICEC_ROOT/$f" ] || die "$HDMICEC_ROOT/$f exists but is not a regular file, so it
       cannot be snapshotted and this build would have no way to restore it.  Investigate that
       path before building."
        mkdir -p -- "$MAKEFILE_SNAPSHOT_DIR/$(dirname -- "$f")" \
            || die "could not create the snapshot subdirectory for $f under $MAKEFILE_SNAPSHOT_DIR"
        # -p preserves the mode, which matters: ccec/src/Makefile is mode 0755 in this tree
        # and a restore that silently dropped the executable bit would be a change of its own.
        cp -p -- "$HDMICEC_ROOT/$f" "$MAKEFILE_SNAPSHOT_DIR/$f" \
            || die "could not snapshot $HDMICEC_ROOT/$f.
       Refusing to run autoreconf and configure without a way back for every one of the six."
        MAKEFILE_SNAPSHOT_TAKEN="${MAKEFILE_SNAPSHOT_TAKEN}${f}
"
        captured=$((captured + 1))
    done

    log "snapshotted $captured of ${#REGENERATED_MAKEFILES[@]} tracked Makefile(s) before autoreconf"
    if [ "$absent" -ne 0 ]; then
        log "  ($absent did not exist yet and are recorded as absent, not fabricated)"
    fi
    log "  snapshot: $MAKEFILE_SNAPSHOT_DIR (removed when this run exits)"
}

# ------------------------------------------------------------------------------------
# Put the six back from the snapshot.  Idempotent, so the trap and an explicit --restore
# can both call it, and safe to call when no snapshot was ever taken.
#
# The pre-check/post-check discipline of the old git-based do_restore is KEPT VERBATIM in
# spirit, because it was the good part: report what was dirty going in (`was:`), do the work,
# then re-read git and report anything still dirty (`still:`) as a hard failure.  What
# changed is only the verb in the middle -- a copy from the snapshot instead of a checkout
# from the index.
#
# `git` is used here for REPORTING ONLY.  When it is absent or this is not a working tree the
# restore still happens and the reporting degrades to a note; the restore does not depend on
# git in any way, which is the whole point.
# ------------------------------------------------------------------------------------
restore_regenerated_makefiles_from_snapshot() {
    [ -n "$MAKEFILE_SNAPSHOT_DIR" ] || return 0
    [ -d "$MAKEFILE_SNAPSHOT_DIR" ] || return 0
    [ -n "$MAKEFILE_SNAPSHOT_TAKEN" ] || return 0

    local git_usable=0
    if command -v git >/dev/null 2>&1 \
       && git -C "$HDMICEC_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
        git_usable=1
    fi

    local before=''
    if [ "$git_usable" -eq 1 ]; then
        before="$(git -C "$HDMICEC_ROOT" status --porcelain -- "${REGENERATED_MAKEFILES[@]}" 2>/dev/null || true)"
    fi

    rule
    log "restoring the tracked Makefiles from this run's snapshot"
    if [ -n "$before" ]; then
        printf '%s\n' "$before" | sed 's/^/[run_coverage]   was: /'
    elif [ "$git_usable" -eq 1 ]; then
        log "  none of the six is currently modified; restoring from the snapshot anyway, because"
        log "  the snapshot is the authority on their pre-build content and a byte-identical copy"
        log "  is a no-op rather than a risk."
    fi

    local f restored=0
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        [ -f "$MAKEFILE_SNAPSHOT_DIR/$f" ] || die "the snapshot is missing $f although it was
       recorded as captured ($MAKEFILE_SNAPSHOT_DIR).  Refusing to restore a partial set: some
       of the six would be pre-build content and the rest generated content, which is worse
       than either."
        [ ! -L "$HDMICEC_ROOT/$f" ] || die "refusing to restore through a symlink: $HDMICEC_ROOT/$f"
        cp -p -- "$MAKEFILE_SNAPSHOT_DIR/$f" "$HDMICEC_ROOT/$f" \
            || die "could not restore $HDMICEC_ROOT/$f from the snapshot at
       $MAKEFILE_SNAPSHOT_DIR/$f
       The snapshot is still there: copy it back by hand.  Do NOT reach for
       'git checkout' over these paths -- it restores what the index holds and would
       destroy any uncommitted edit, which is the defect this mechanism replaced."
        restored=$((restored + 1))
    done <<< "$MAKEFILE_SNAPSHOT_TAKEN"

    log "restored $restored tracked Makefile(s) from the snapshot"

    if [ "$git_usable" -eq 1 ]; then
        local after
        after="$(git -C "$HDMICEC_ROOT" status --porcelain -- "${REGENERATED_MAKEFILES[@]}" 2>/dev/null || true)"
        if [ -n "$after" ]; then
            printf '%s\n' "$after" | sed 's/^/[run_coverage]   still: /' >&2
            # NOT fatal, and the difference from the old git-based check is deliberate.  The old
            # one compared against the index and so "still dirty" could only mean the checkout
            # had failed.  Here the snapshot is the reference, and a path that was ALREADY
            # modified relative to the index before this run started is still modified after a
            # faithful restore -- correctly so.  That is a report, not a failure.
            warn "the paths above still differ from the index.  That is expected when they were"
            warn "  already modified before this run started: the snapshot restores the bytes"
            warn "  this run found, which is not the same as the bytes git holds.  Compare them"
            warn "  yourself if you need to -- this script will not resolve it for you by"
            warn "  reverting anything."
        else
            log "the six now match the index"
        fi
    else
        log "git is not usable here, so the restore was not cross-checked against the index."
        log "  The copy itself does not depend on git and has already been verified above."
    fi
    return 0
}

# ------------------------------------------------------------------------------------
# Serialise builds in this working tree.  See THE BUILD LOCK above for why this cannot be
# the artifact-directory lock and why a derived name is the accepted trade-off.
# ------------------------------------------------------------------------------------
acquire_build_lock() {
    local parent="${TMPDIR:-/tmp}" key
    case "$parent" in
        /*) ;;
        *)  die "TMPDIR must be an absolute path to be checked safely; got: $parent" ;;
    esac
    parent="$(canonicalise_path_lexically "$parent")"

    # '/' is not legal in a filename, so the tree's own path becomes the key with the
    # separators flattened.  A path long enough to exceed the filename limit keeps its TAIL,
    # which is the part that distinguishes sibling clones, with the full length prefixed so
    # two different paths that share a tail cannot collide silently.
    key="${HDMICEC_ROOT//\//_}"
    if [ "${#key}" -gt 180 ]; then
        key="${#key}${key: -180}"
    fi
    BUILD_LOCK_PATH="$parent/hdmicec-l1-build$key.lock"

    # THE ANCESTRY CHECK IS ON THE PARENT DIRECTORY, NOT ON THE LOCK PATH, and getting this
    # wrong is a defect a concurrency test found rather than a subtlety worth guessing at.
    # assert_safe_ancestry validates every EXISTING component of the path it is given, which
    # means it requires each one to be a directory.  The lock's leaf is a regular FILE and --
    # unlike the artifact root, which is minted fresh -- it PERSISTS between runs, so on the
    # second run the walk reached an existing regular file where it expected a directory and
    # refused.  Measured symptom: the first --build succeeded because the leaf did not exist
    # yet, and every subsequent one died with "is a regular empty file, not a directory".
    #
    # So the parent chain gets the ancestry walk and the leaf gets the two checks that are
    # actually meaningful for a file -- the same division acquire_output_lock already uses,
    # where OUTPUT_DIR's ancestry is validated separately and the lock itself only has to not
    # be a symlink.
    assert_safe_ancestry "$parent" minted
    [ ! -L "$BUILD_LOCK_PATH" ] || die "refusing to lock through a symlink: $BUILD_LOCK_PATH
       Something pre-created that name.  Remove it, or point TMPDIR somewhere you own."
    if [ -e "$BUILD_LOCK_PATH" ] && [ ! -f "$BUILD_LOCK_PATH" ]; then
        die "the build lock path exists and is not a regular file: $BUILD_LOCK_PATH"
    fi
    # '>>' so an existing lock file is never truncated: another run may be holding it.
    exec {BUILD_LOCK_FD}>>"$BUILD_LOCK_PATH" || die "could not open the build lock: $BUILD_LOCK_PATH"

    if command -v flock >/dev/null 2>&1; then
        flock -n "$BUILD_LOCK_FD" || die "another run of this script is building in
       $HDMICEC_ROOT
       Two builds in one tree would interleave their Makefile snapshots and restores, and
       the loser would 'restore' the winner's generated content as though it were the
       original.  Wait for the other run to finish.
       (The lock is $BUILD_LOCK_PATH, held on an open descriptor and released when that
       run's shell exits, so a crashed run does not leave it stuck.)"
        log "holding the exclusive build lock for $HDMICEC_ROOT"
    else
        # Stated, not silently tolerated: without flock the snapshot/restore pairing across
        # two concurrent runs cannot be guaranteed, and the caller is the only one who can
        # know whether a second run is possible on this host.
        warn "flock is not available, so two concurrent builds in $HDMICEC_ROOT cannot be"
        warn "  prevented.  If anything else may be building this tree, do not run --build"
        warn "  now: the two runs' Makefile snapshots would interleave."
        note_advisory "flock was unavailable, so this run could not prove it was the only build in
       $HDMICEC_ROOT.  The Makefile snapshot and restore are correct for THIS run, but a
       concurrent build could have interleaved with them."
    fi
}

# ------------------------------------------------------------------------------------
# CANCELLATION.  A run that cannot be stopped is a run CI cannot cancel.
#
# Bash runs a trap only BETWEEN commands.  The suite used to be launched as a FOREGROUND child
# -- `( cd …; timeout … ./run_L1Tests ) >"$run_log"` -- so while this shell sat inside that
# command an external SIGTERM was recorded and then withheld from the handler until the child
# finished on its own.  For the length of a whole suite the runner therefore ignored its own
# cancellation, and nothing forwarded the signal to the suite: a cancelled job left the test
# binary running and the private lcov HOME and staging directory behind it.
#
# So the suite is launched in the BACKGROUND and in its OWN PROCESS GROUP -- `set -m` makes a
# background job a process-group leader -- and this shell waits on it.  `wait` is interruptible,
# so a signal reaches the handler at once; the handler then signals the whole GROUP, which is
# `timeout` and the test binary, both of which stay in that group because `timeout --foreground`
# deliberately does not create one of its own.  A bounded grace period follows, then the group is
# killed outright.
SUITE_PGID=''                  # process group of the running suite; empty when none is running
SUITE_SIGNAL=''                # name of the signal that cancelled this run, if any
# How long a signalled process group is given to exit before it is killed outright.  Bounded
# because the point of the exercise is that a cancellation completes.
SUITE_STOP_GRACE_SECONDS="${SUITE_STOP_GRACE_SECONDS:-10}"
readonly SUITE_STOP_GRACE_SECONDS

# Wait up to $2 seconds for kill-target $1 (a pid, or -pgid) to disappear.  0 when it is gone.
await_process_exit() { # $1 = kill target  $2 = seconds
    local target="$1" seconds="$2" waited=0
    while kill -0 -- "$target" 2>/dev/null; do
        [ "$waited" -lt "$seconds" ] || return 1
        sleep 1
        waited=$((waited + 1))
    done
    return 0
}

# Forward $1 to the suite's process group, then make sure it is actually gone.  Idempotent, so
# the signal handler and the EXIT handler can both call it.  The group is addressed by id -- never
# by a command-line pattern, because `pkill -f run_L1Tests` would also match anything that merely
# mentions the name, including the harness that started this script.
stop_suite_group() { # $1 = signal name to forward
    local signal="${1:-TERM}"
    [ -n "$SUITE_PGID" ] || return 0
    if ! kill -0 -- "-$SUITE_PGID" 2>/dev/null; then
        SUITE_PGID=''
        return 0
    fi
    warn "forwarding SIG$signal to the suite process group $SUITE_PGID"
    kill -"$signal" -- "-$SUITE_PGID" 2>/dev/null || true
    if ! await_process_exit "-$SUITE_PGID" "$SUITE_STOP_GRACE_SECONDS"; then
        warn "the suite process group $SUITE_PGID ignored SIG$signal for"
        warn "    ${SUITE_STOP_GRACE_SECONDS}s (SUITE_STOP_GRACE_SECONDS); killing it outright."
        kill -KILL -- "-$SUITE_PGID" 2>/dev/null || true
        await_process_exit "-$SUITE_PGID" 5 \
            || warn "process group $SUITE_PGID survived SIGKILL; report this, it should not happen."
    fi
    SUITE_PGID=''
    return 0
}

on_exit() {
    local rc=$?
    # Children first: removing the staging directory or the private lcov HOME while the suite is
    # still running is a cleanup that has to be done twice.  The signal that cancelled the run,
    # when there was one, is the signal forwarded on -- a run cancelled with SIGHUP should not
    # report that it sent SIGTERM.
    stop_suite_group "${SUITE_SIGNAL:-TERM}"
    # THE MAKEFILE RESTORE HANGS OFF THE TRAP, NOT OFF THE BUILD'S SUCCESS PATH, and that is
    # the reason it is here rather than at the end of do_build.  A build that fails half way
    # through configure has already had some of the six overwritten; a run cancelled with
    # Ctrl-C has too.  Both used to leave the caller holding generated content over their own
    # source and a printed `git checkout` command as the only way out.  Restoring from the trap
    # covers every exit path there is -- success, failure, gate failure and cancellation --
    # with one mechanism and no special cases.  It runs BEFORE the snapshot is removed, and
    # both are no-ops when no snapshot was ever taken (any invocation without --build).
    restore_regenerated_makefiles_from_snapshot || true
    cleanup_makefile_snapshot
    cleanup_stage_dir
    cleanup_lcov_home
    return "$rc"
}

# The signal handler `exit`s rather than re-raising, because a shell terminated by a signal with
# its default disposition never runs its EXIT trap: re-raising would have skipped the staging
# cleanup, the private lcov HOME and -- now -- the suite itself.  `exit 130/143/129` reports the
# same status a signalled shell would while guaranteeing the handler runs.
on_signal() { # $1 = signal name  $2 = exit status
    SUITE_SIGNAL="$1"
    warn "received SIG$1 -- cancelling this run"
    stop_suite_group "$1"
    exit "$2"
}
trap on_exit EXIT
trap 'on_signal INT 130' INT
trap 'on_signal TERM 143' TERM
trap 'on_signal HUP 129' HUP


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
# file as a side effect nobody asked for.  Moving it aside and moving it back is worse than
# it looks, and must not be introduced here: the restoring `mv -f` overwrites whatever
# stands at $HOME/.lcovrc at that moment, so a file that REAPPEARED during the run -- the
# owner recreating it, or a sibling coverage runner in this workspace if one were ever
# changed to write there -- would be silently destroyed by a script whose only job is to
# measure; and until the restore, a file recreated mid-run is in effect for every lcov
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


# ------------------------------------------------------------------------------------
# Words the reference audit must NOT treat as a command or as a shell variable, each
# with the reason it is not one.  The scanner reads shell text with grep, so it cannot
# distinguish a shell command from a word inside a single-quoted awk program, a heredoc
# or a filename literal; this list is the complete set of such words in this script and
# anything not listed has to resolve.  Keep it short -- a growing list is the sign that
# the audit is being worked around rather than satisfied.
#   br_cell            awk-local in the per-file table program (write_per_file_tsv / per_file_report)
#   func_cell          awk-local in the per-file table program
#   line_cell          awk-local in the per-file table program
# ------------------------------------------------------------------------------------
SELFTEST_NOT_A_COMMAND='br_cell
func_cell
line_cell'
readonly SELFTEST_NOT_A_COMMAND

# ------------------------------------------------------------------------------------
# SELF-REFERENCE AUDIT, and the `selftest` subcommand built on it.
#
# WHY THIS EXISTS.  This script is long, it runs under `set -euo pipefail`, and bash resolves
# a command name only when control reaches it.  A call to a function that does not exist, or an
# expansion of a variable that was never defined, is therefore invisible to `bash -n`, invisible
# to shellcheck, and invisible until the run is already under way -- at which point it aborts
# with `command not found` or `unbound variable` after the banner has printed, having produced
# no measurement.  Two real defects of exactly that shape were found in this workspace's
# runners: calls to a `prepare_lcov_home` that was never defined alongside wrappers built over
# an undefined `LCOV_HOME_DIR`, and a per-file gate that expanded an undefined
# `COVERAGE_GATE_EXEMPT_FILES` the moment any file fell below the bar.
#
# WHAT IT CHECKS.  Two things, statically, over this script's own text:
#   1. Every snake_case word in a command position resolves -- to a function defined here, a
#      variable this script assigns, a shell builtin, or an executable on PATH.
#   2. Every ${UPPER_CASE} expansion is either assigned by this script, written with a default
#      (${X:-...}, ${X-...}, ${X+...}), or already exported in the environment.
#
# THE ONE BLIND SPOT, NAMED RATHER THAN GLOSSED OVER.  The scanner reads shell text with grep,
# so it cannot tell a shell command from a word inside a single-quoted awk program, a
# heredoc, or a filename literal.  Those tokens are listed in SELFTEST_NOT_A_COMMAND below,
# each with the reason it is not a call.  Everything else must resolve; the list is the
# complete set of exceptions and a reader can check every entry against the source.
#
# WHEN IT RUNS.  Always, as main()'s first act, before any validation, any filesystem write and
# any lcov invocation -- so a script that cannot resolve its own references reports exactly
# that and touches nothing.  `selftest` runs the audit plus the tooling pre-flight and stops
# there: no suite, no counters zeroed, no capture, no artifact directory.
# ------------------------------------------------------------------------------------
reference_audit() {
    local body defined assigned locals called globals defaulted setglob w failures=0
    [ -r "$SCRIPT_PATH" ] || die "cannot read this script back for the reference audit: $SCRIPT_PATH"

    body="$(grep -vE '^[[:space:]]*#' -- "$SCRIPT_PATH" || true)"
    defined="$(grep -oE '^[a-zA-Z_][a-zA-Z0-9_]*\(\)' -- "$SCRIPT_PATH" | tr -d '()' | sort -u)"
    assigned="$(printf '%s\n' "$body" | grep -oE '(^|[[:space:]]|\(|;)[a-z][a-z0-9_]*=' | grep -oE '[a-z][a-z0-9_]*' | sort -u)"
    locals="$(printf '%s\n' "$body" | grep -oE '\b(local|read -r|read)[[:space:]]+([a-z][a-z0-9_]*[[:space:]]*)+' | grep -oE '[a-z][a-z0-9_]*' | sort -u)"
    called="$(printf '%s\n' "$body" \
        | grep -oE '(^|[[:space:]]|;|\||&|\(|!)[[:space:]]*[a-z][a-z0-9]*(_[a-z0-9]+)+([[:space:]]|$|\))' \
        | grep -oE '[a-z][a-z0-9]*(_[a-z0-9]+)+' | sort -u)"

    for w in $called; do
        printf '%s\n' "$defined"                | grep -qx -- "$w" && continue
        printf '%s\n' "$assigned"               | grep -qx -- "$w" && continue
        printf '%s\n' "$locals"                 | grep -qx -- "$w" && continue
        printf '%s\n' "$SELFTEST_NOT_A_COMMAND" | grep -qx -- "$w" && continue
        command -v -- "$w" >/dev/null 2>&1 && continue
        warn "reference audit: '$w' is used in a command position but is not a function defined"
        warn "    here, not a variable this script assigns, not a builtin and not on PATH."
        failures=$((failures + 1))
    done

    globals="$(printf '%s\n' "$body" | grep -oE '\$\{?[A-Z][A-Z0-9_]*' | grep -oE '[A-Z][A-Z0-9_]*' | sort -u)"
    defaulted="$(printf '%s\n' "$body" | grep -oE '\$\{[A-Z][A-Z0-9_]*[:+-]' | grep -oE '[A-Z][A-Z0-9_]*' | sort -u)"
    setglob="$(grep -oE '^[[:space:]]*(export[[:space:]]+|readonly[[:space:]]+|local[[:space:]]+|declare[[:space:]]+-[a-zA-Z]+[[:space:]]+)?[A-Z][A-Z0-9_]*(=|\+=|\()' -- "$SCRIPT_PATH" \
        | grep -oE '[A-Z][A-Z0-9_]*' | sort -u)"
    for w in $globals; do
        printf '%s\n' "$setglob"                | grep -qx -- "$w" && continue
        printf '%s\n' "$defaulted"              | grep -qx -- "$w" && continue
        printf '%s\n' "$SELFTEST_NOT_A_COMMAND" | grep -qx -- "$w" && continue
        [ -n "${!w+x}" ] && continue
        warn "reference audit: \$$w is expanded but is never assigned here, has no default form"
        warn "    (\${$w:-...}) and is not set in the environment; under 'set -u' that aborts the run."
        failures=$((failures + 1))
    done

    # STAGE 3: a reference written WITH a default but never assigned anywhere.
    #
    # Stage 2 deliberately accepts \${X:-...} because it cannot abort under 'set -u'.  That is
    # exactly what let a real defect through: the sink runner read \${STAT_BIN:-} in path_metadata()
    # but never assigned STAT_BIN, so the guard was permanently empty and every run died at the
    # first artifact-ancestry check with "stat was not found on PATH" while /usr/bin/stat was on
    # PATH all along.  Safe from 'set -u', and wrong in every run - so the default form has to be
    # audited too, not treated as proof of resolution.
    #
    # Names that are genuinely read from the environment and are MEANT to be unassigned here are
    # listed below with the reason.  Every other tool handle, path and tunable this script uses is
    # assigned in one place, so the list stays short by construction.
    #   TMPDIR       - a standard environment variable; \${TMPDIR:-/tmp} is the documented way
    #                  to read it
    #   X            - not a variable: it appears as \${X:-...} inside this audit's own
    #                  explanatory comment, describing the default FORM rather than naming a
    #                  real reference
    #   CLONE_INDEX  - set by the workspace environment to distinguish parallel clones.  Read
    #                  only by write_provenance, and only to RECORD which clone produced an
    #                  artifact; "unset" is a truthful value there, so an empty read is correct
    #                  behaviour rather than a silent failure
    #   CXX          - the standard compiler environment variable.  write_provenance reads it to
    #                  record which compiler produced the instrumentation, falling back to g++,
    #                  which is the same convention the build itself uses
    for w in $defaulted; do
        printf '%s\n' "$setglob" | grep -qx -- "$w" && continue
        case " $w " in
            " TMPDIR " | " X " | " CLONE_INDEX " | " CXX ") continue ;;
        esac
        warn "reference audit: \$$w is only ever read with a default (\${$w:-...}) and is never"
        warn "    assigned by this script.  It cannot abort the run, so it will silently be empty"
        warn "    every time - which makes whatever depends on it dead or permanently failing."
        warn "    Either assign it, or add it to this stage's environment-only list with a reason."
        failures=$((failures + 1))
    done

    if [ "$failures" -ne 0 ]; then
        die "the reference audit found $failures unresolved name(s) in this script.
       Every one of them would abort a real run part-way through, after the banner and
       before any measurement.  Fix the name, or -- if it is genuinely not a shell
       command (a word inside an embedded awk program, a heredoc, or a filename) -- add
       it to SELFTEST_NOT_A_COMMAND with the reason."
    fi
    return 0
}

selftest() {
    rule
    log "SELF-TEST: no suite is run, no counters are zeroed, nothing is captured and no"
    log "  artifact directory is created."
    rule
    log "1/3 re-parsing this script"
    bash -n -- "$SCRIPT_PATH" || die "this script does not parse: $SCRIPT_PATH"
    log "    parses cleanly"
    log "2/3 auditing every internal function and variable reference"
    reference_audit
    log "    every reference resolves"
    log "3/3 measurement tooling pre-flight"
    require_tools
    log "    tooling present and usable"
    rule
    log "SELF-TEST PASSED"
}

usage() {
    # The matrix is rendered into a variable BEFORE the heredoc rather than inside a command
    # substitution within it.  Two reasons, and the second is the load-bearing one: a heredoc
    # holding a loop is unreadable, and the reference audit reads this script's text with grep,
    # so a loop variable inside a heredoc looks exactly like a call to a command that does not
    # exist.  It caught this one when it was written the other way -- which is the audit doing
    # its job, and the fix is to write it so there is nothing to except.
    local matrix_summary='' record
    local label tier mode back_end select_filter exclude_filter synopsis
    for record in "${INVOCATION_MATRIX[@]}"; do
        IFS='|' read -r label tier mode back_end select_filter exclude_filter synopsis <<< "$record"
        matrix_summary="${matrix_summary}$(printf '  %s  %-3s CEC_TEST_AIDL_MODE=%-13s expects %-6s  %s' \
            "$label" "$tier" "$mode" "$back_end" "$synopsis")
"
    done

    cat <<USAGE
Usage: run_coverage.sh [OPTIONS]

gcov/lcov coverage runner and line-coverage gate for the HDMI-CEC middleware L1 suite.
Reproduces the "Generate coverage" step of .github/workflows/L1-tests.yml, adds the
branch data that step discards, and adds the numeric threshold it has never had.

OPTIONS
  -b, --build              Build the middleware, its L1 suite and the L2 tier with
                           coverage instrumentation first.  OFF by default so an existing
                           measurement is never silently discarded.  autoreconf and
                           configure rewrite six git-tracked Makefiles: this snapshots
                           their working-tree content beforehand and RESTORES THEM ITSELF
                           on every exit path, including a failure or a Ctrl-C.  No
                           'git checkout' is run or suggested (see --restore).
  -r, --run                Zero this build tree's gcov counters ONCE, then run every
                           invocation of the matrix -- one process per selection outcome,
                           each with its own results and log artifact -- and capture ONCE
                           after all of them have passed.  OFF by default.
                           Zeroing once is what makes the figures an account of THIS run:
                           gcov counters accumulate, so a stale .gcda would let the gate
                           pass on evidence the current tests did not produce.  Zeroing
                           once rather than per invocation is what makes them an account
                           of the WHOLE run: the back-end selection resolves once per
                           process, so accumulation across the invocations is the only way
                           both arms of that branch are ever covered.
                           An invocation that cannot run on this host -- no binder driver,
                           for the AIDL-selected ones -- is reported as DEFERRED and the
                           verdict becomes advisory.  It is never counted as passed.
  -t, --threshold N        Line-coverage bar, an integer percentage.  Default ${COVERAGE_MIN}.
                           Lower it only for a deliberate diagnostic run; 80 is the
                           required bar.
  -o, --output-dir DIR     Write artifacts to DIR.  Left unset -- the default -- the root
                           is MINTED per run with
                           mktemp -d "\${TMPDIR:-/tmp}/hdmicec-l1-coverage.XXXXXXXX",
                           which is outside the git tree because this suite's .gitignore
                           does not cover coverage.info, filtered_coverage.info or
                           coverage/ and is out of scope for editing, and unpredictable so
                           it cannot be pre-created by another account.  The chosen path is
                           printed as the "artifacts:" line.
                           A DIR you name is held to the same custody as a minted one and
                           to two checks a minted name cannot need: it is collapsed
                           lexically first (so '..' cannot land the artifacts outside the
                           tree it appears to name) and refused if it is near-root or
                           inside a system tree -- /etc, /usr, /var and the rest; /tmp,
                           /var/tmp, /run/user, /opt, /home, /root and anywhere in your own
                           checkout are all accepted.  It is then brought to mode 0700, and
                           every file written under it is 0600.
      --per-file-gate      In addition to the aggregate gate, fail when ANY non-exempt file
                           in the filtered trace is below the bar.  ON by default, because
                           Directive 4 states the bar per target; the flag exists so a
                           caller can state it explicitly and override
                           COVERAGE_PER_FILE_GATE=0 from the environment.
      --no-per-file-gate   Gate on the aggregate only, for a diagnostic run that wants the
                           per-file table without its verdict.  NOT a setting an acceptance
                           run uses.  Below-bar files are enumerated either way.
      --no-html            Skip the genhtml report (the trace and the tables are still
                           produced).
      --restore            Report on the six git-tracked Makefiles that autoreconf and
                           configure rewrite, restore them from THIS invocation's snapshot
                           if it has one, and exit.  Since --build restores from its own
                           snapshot before it exits, the expected outcome here is "nothing
                           to restore".  With the six dirty and no snapshot it REFUSES and
                           exits non-zero: the only remaining mechanism would be
                           'git checkout' over those paths, which restores what the index
                           holds rather than what was there and would destroy an
                           uncommitted edit to ccec/src/Makefile -- hand-written source,
                           not generated output.  This script runs no git command that
                           writes, in any mode.
  -h, --help               Print this help and exit.
      --selftest           Audit every internal function and variable reference in this
                           script, re-parse it, and verify the measurement tooling -- then
                           exit.  Runs no suite, zeroes no counters, captures nothing and
                           creates no artifact directory.  The same audit runs at the start
                           of every real invocation.

ENVIRONMENT (command-line flags win)
  COVERAGE_MIN=N               same as --threshold
  COVERAGE_OUTPUT_DIR=DIR      same as --output-dir
  COVERAGE_PER_FILE_GATE=1     same as --per-file-gate (the default)
  COVERAGE_PER_FILE_GATE=0     same as --no-per-file-gate
  COVERAGE_GATE_EXEMPT_FILES   newline-separated trace paths whose per-file VOTE is
                               suppressed.  Empty by default and meant to stay that way;
                               the only admissible reason for an entry, and what an entry
                               does and does not do, are stated at its definition in this
                               script.  Exempt files are still traced, still counted in
                               the aggregate, and still printed with their figures.
  GTEST_PREFIX=DIR             prefix providing libgtest/libgmock and their headers,
                               used by --build and put on LD_LIBRARY_PATH by --run.
                               Default: ${GTEST_PREFIX}
  GTEST_EXTRA_ARGS="..."       extra arguments appended to each invocation's command line.
                               --gtest_shuffle is a DIAGNOSTIC: this suite has
                               pre-existing order-fragile tests that pass in the default
                               order, so a shuffled run is expected to fail and must
                               never be treated as a gate.
                               A --gtest_filter here OVERRIDES the invocation's own filter
                               (GoogleTest lets the last one win) and therefore FAILS the
                               invocation on its case count, with both numbers named. That
                               is deliberate: a re-filtered invocation is a different
                               invocation and this script will not report one as the other.

  AIDL/BINDER STAGING PREFIXES -- passed straight through to configure by --build, which
  names no path of its own.  configure.ac declares all four with AC_ARG_VAR and derives the
  include roots, the library directories, the four -l edges and the C++17 flag from them, so
  these are the only inputs and there is one place that decides.  Unset is passed as unset,
  never as empty, because 'BINDER_SDK_DIR=' asserts there is no SDK while omitting it lets
  configure look where it knows to look.
  HALIF_PREFIX=DIR             root of the staged rdk-halif-aidl tree, supplying the
                               generated AIDL stub headers, common/current/halcompat.h and
                               the stub libraries.  Unset: configure falls back to the
                               sibling rdk-halif-aidl checkout beside this submodule.
                               Currently: ${HALIF_PREFIX:-<unset>}
  HALIF_LIB_DIR=DIR            stub library directory, for a split staging layout in which
                               it is not HALIF_PREFIX/lib/halif.
                               Currently: ${HALIF_LIB_DIR:-<unset>}
  BINDER_SDK_DIR=DIR           root of the staged Binder SDK, holding lib/binder and
                               include/binder_sdk.
                               Currently: ${BINDER_SDK_DIR:-<unset>}
  BINDER_SDK_INCLUDE_DIR=DIR   Binder header prefix, for a layout whose headers are not
                               under BINDER_SDK_DIR.
                               Currently: ${BINDER_SDK_INCLUDE_DIR:-<unset>}

  TEST HARNESS VARIABLES -- set by this script per invocation, listed so their contract is
  discoverable from --help rather than only from the sources.  Do not set them yourself: a
  value in the environment would be overridden per invocation anyway.
  CEC_TEST_AIDL_MODE           absent | compatible | incompatible | remote.  What the
                               harness makes the service lookup find, and the only thing
                               that distinguishes one invocation from the next.  Read by
                               tests/L1Tests/test_main.cpp and tests/L2Tests/test_main.cpp
                               and by NO production source.  Each harness hard-fails on a
                               value it does not implement rather than downgrading to
                               'absent', so a typo cannot silently become a legacy run.
  CEC_FAKE_AIDL_HOST_PATH      where the L2 harness finds the out-of-process fake service
                               host.  Set for both L2 invocations; only 'remote' reads it.
  CEC_FAKE_HOST_READY_FD       NOT SET BY THIS SCRIPT, and it must not be.  It names an
                               INHERITED descriptor -- the write end of a pipe whose read
                               end the waiting parent holds -- and the parent is the L2
                               harness, which creates the pipe, forks, sets this in the
                               child's environment and blocks on the read end under a
                               bounded timeout.  A value exported from here would name a
                               descriptor this script never opened; the host treats an
                               unusable descriptor as a hard failure precisely so a botched
                               handoff is reported rather than answered on stdout.
  BINDER_DRIVER_NODE           the driver node whose presence decides whether the
                               AIDL-selected invocations can run at all.  Default
                               /dev/binder.  Absent or unusable, those invocations are
                               reported DEFERRED and never attempted -- registering a
                               service name opens the driver, and on the pinned binder stack
                               that aborts the process when the node is missing.
                               Currently: ${BINDER_DRIVER_NODE}

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
  L1 runner       ${HDMICEC_ROOT}/${TEST_BINARY_REL}
  L2 runner       ${HDMICEC_ROOT}/${L2_TEST_BINARY_REL}
  fake host       ${HDMICEC_ROOT}/${FAKE_HOST_BINARY_REL}   (launched by the L2 harness, never by this script)
  binder node     ${BINDER_DRIVER_NODE}$( binder_transport_present && printf '   (present and usable: every invocation can run)' || printf '   (ABSENT or unusable: the AIDL-selected invocations will be DEFERRED)' )

THE INVOCATION MATRIX
${matrix_summary}
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
            --selftest)          selftest; exit 0 ;;
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
    # plugin runners': digits, or digits.digits.  Keep the three in step -- a bar that is a
    # working diagnostic value for one runner and a hard error in another cannot be wired
    # through one pipeline.  Fractions are accepted because lcov's --fail-under-lines takes
    # one, so refusing them would buy nothing.
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
    # 80 is this submodule's acceptance bar.  Any other value is a diagnostic, and saying so is
    # not enough on its own: a warning still lets the run print the same PASS line and return
    # the same 0 a real acceptance run returns, so `COVERAGE_MIN=0` used to buy an acceptance
    # success with no coverage requirement behind it.  The weakening is therefore recorded as an
    # ADVISORY REASON, which forces the final verdict to ADVISORY and the exit status to
    # $EXIT_ADVISORY - a status a caller can branch on and cannot mistake for a pass.
    # 80, 80.0 and 80.00 are the same bar; 80.5 is not.
    if [ "$min_int" -ne 80 ] || [ -n "${min_frac//0/}" ]; then
        warn "the line bar is ${COVERAGE_MIN}%, not the required 80%.  This is a DIAGNOSTIC run:"
        warn "    its verdict is NOT the acceptance verdict for this submodule."
        note_advisory "the line bar was set to ${COVERAGE_MIN}%, not the 80% Directive 4 requires,
       so no verdict this run produces is an acceptance verdict for this submodule."
    fi

    case "$COVERAGE_PER_FILE_GATE" in
        0) note_advisory "per-file gating was turned off (--no-per-file-gate /
       COVERAGE_PER_FILE_GATE=0).  Directive 4 states the bar PER TARGET, so a run without the
       per-file half cannot produce an acceptance verdict however good the aggregate is." ;;
        1) ;;
        *)   die "COVERAGE_PER_FILE_GATE must be 0 or 1, got '$COVERAGE_PER_FILE_GATE'" ;;
    esac

    # An exemption list suppresses a per-file vote, which is exactly the shape of weakening the
    # aggregate cannot reveal: one file at 0% inside a large denominator moves the total by
    # almost nothing.  Naming a file here is admissible for a diagnostic run and never for an
    # acceptance one, so its presence is recorded rather than only printed later.
    if [ -n "$COVERAGE_GATE_EXEMPT_FILES" ]; then
        note_advisory "COVERAGE_GATE_EXEMPT_FILES is set, so at least one target's per-file vote
       was suppressed.  Directive 4 admits no exemption, so this run's verdict is diagnostic:
       $(printf '%s' "$COVERAGE_GATE_EXEMPT_FILES" | tr '\n' ' ')"
    fi

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

# ------------------------------------------------------------------------------------
# THE TWO TEST TIERS AND THE FAKE SERVICE HOST.
#
# Each is the libtool WRAPPER, never .libs/<name>: the wrapper is what sets up the
# uninstalled shared-library search path for libRCEC and libRCECOSHal, and for the L2 tier
# also for the AIDL stub and Binder libraries libRCEC now links.
#
# TEST_BINARY_REL keeps its name and its meaning -- the L1 runner -- because
# require_instrumented_tree and do_build both treat it as THE binary whose absence means
# "this tree was never built", and that judgement is still the L1 runner's to make: the L1
# suite is the one that always exists, and a tree with an L1 runner but no L2 runner is a
# partial build rather than an unbuilt one.  The L2 pair is named separately and checked
# separately, in the invocations that use it.
#
# FAKE_HOST_BINARY_REL is NOT a test and is never run by this script.  It is the
# out-of-process fake service the L2 harness launches for invocation E; tests/L2Tests's own
# Makefile.am keeps it out of TESTS for the same reason (listing a process that runs until
# signalled would hang `make check` rather than fail it).  All this script does with it is
# build it and tell the harness where it is -- see CEC_FAKE_AIDL_HOST_PATH below.
# ------------------------------------------------------------------------------------
readonly TEST_BINARY_REL='tests/L1Tests/run_L1Tests'
readonly L2_TEST_BINARY_REL='tests/L2Tests/run_L2Tests'
readonly FAKE_HOST_BINARY_REL='tests/L2Tests/fake_hdmi_cec_aidl_host'

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

    # THE L2 TIER IS REPORTED HERE, NOT REQUIRED HERE, and the difference is deliberate.  An
    # invocation that needs a binary it does not have fails in run_one_invocation, naming that
    # binary and that invocation -- which is a better diagnostic than a blanket prerequisite
    # failure, and it leaves the L1-only invocations runnable on a tree where the L2 tier was
    # not configured.  Saying so up front is still worth doing: a caller who expected five
    # invocations and is about to get three should learn it before the counters are zeroed.
    if [ ! -x "$HDMICEC_ROOT/$L2_TEST_BINARY_REL" ] || [ ! -x "$HDMICEC_ROOT/$FAKE_HOST_BINARY_REL" ]; then
        warn "the L2 integration tier is not built:"
        [ -x "$HDMICEC_ROOT/$L2_TEST_BINARY_REL" ]   || warn "    missing: $L2_TEST_BINARY_REL"
        [ -x "$HDMICEC_ROOT/$FAKE_HOST_BINARY_REL" ] || warn "    missing: $FAKE_HOST_BINARY_REL"
        warn "  Invocations D and E need both and will fail naming them.  '$SCRIPT_PATH --build'"
        warn "  builds them; they come from tests/L2Tests, which configure.ac configures through"
        warn "  AC_CONFIG_FILES and tests/Makefile.am recurses into under --enable-l1tests."
    fi

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
        #
        # ACCUMULATION IS NOW BOTH THE MECHANISM AND THE HAZARD, which is why this warning
        # says less than it looks as though it should.  Under `--run` the counters are
        # zeroed once and then accumulated DELIBERATELY across the matrix, because the
        # back-end selection resolves once per process and that is the only way both arms
        # of the selection branch are ever covered.  Without `--run` the same accumulation
        # is uncontrolled: these counters may hold one complete matrix, a partial one, or
        # several runs of different builds, and nothing here can tell which.  So the
        # figures are still produced and still real, and the verdict is still advisory --
        # what cannot be established is WHICH executions they describe.
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

    # ------------------------------------------------------------------------------------
    # THE AIDL/BINDER STAGING PREFIXES, HANDED TO configure AS PRECIOUS VARIABLES.
    #
    # configure.ac declares all four with AC_ARG_VAR, so `./configure NAME=value` is the
    # documented way to supply them, and it DERIVES everything else from them itself: the four
    # include roots, the -L directories, the four -l edges and the C++17 dialect flag are
    # AC_SUBSTed outputs computed inside configure.  So this passes the prefixes and nothing
    # else -- no -I, no -L, no -l is spelled here.
    #
    # WHY NOT PUT THE INCLUDE ROOTS IN CPPFLAGS ABOVE, which would look more direct.  Because
    # then two places would decide where halcompat.h lives, and configure.ac exists precisely
    # so that ONE place does: the same two prefixes feed the Autotools build and the
    # hand-written ccec/src/Makefile, and the whole point of the single-prefix contract is that
    # the two build systems cannot end up configured differently.  A root spelled here as well
    # would be a second source of truth and a silent way for them to diverge.
    #
    # AN UNSET PREFIX IS PASSED AS UNSET, NOT AS AN EMPTY STRING, and the difference is real:
    # `configure BINDER_SDK_DIR=` asserts there is no Binder SDK and fails configure's
    # availability check, while omitting it lets configure look where it knows to look -- for
    # HALIF_PREFIX, the sibling rdk-halif-aidl checkout beside this submodule, which is the
    # ordinary in-workspace arrangement.  Hence the append-only array rather than four
    # unconditional assignments.
    # ------------------------------------------------------------------------------------
    local -a configure_prefix_args=()
    [ -z "$HALIF_PREFIX" ]            || configure_prefix_args+=("HALIF_PREFIX=$HALIF_PREFIX")
    [ -z "$HALIF_LIB_DIR" ]           || configure_prefix_args+=("HALIF_LIB_DIR=$HALIF_LIB_DIR")
    [ -z "$BINDER_SDK_DIR" ]          || configure_prefix_args+=("BINDER_SDK_DIR=$BINDER_SDK_DIR")
    [ -z "$BINDER_SDK_INCLUDE_DIR" ]  || configure_prefix_args+=("BINDER_SDK_INCLUDE_DIR=$BINDER_SDK_INCLUDE_DIR")

    if [ "${#configure_prefix_args[@]}" -eq 0 ]; then
        log "  no AIDL/Binder staging prefix was supplied; configure will look for the sibling"
        log "  rdk-halif-aidl checkout and for the Binder SDK where it knows to look.  If it"
        log "  cannot find them it fails at configure time with the roots it tried, which is the"
        log "  intended outcome: the alternative is a libRCEC carrying only one back-end."
    else
        log "  AIDL/Binder staging prefixes passed to configure:"
        local prefix_arg
        for prefix_arg in "${configure_prefix_args[@]}"; do
            log "    $prefix_arg"
        done
    fi

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
    # THE SNAPSHOT AND THE LOCK GO HERE: ahead of the build chain and OUTSIDE the subshell.
    #
    # OUTSIDE, because a variable assigned inside a subshell is lost the moment it exits, and
    # MAKEFILE_SNAPSHOT_DIR has to survive into the exit trap that does the restoring.  That
    # rules out the literally-adjacent placement -- a snapshot step wedged between the
    # `ln -sf` and the `autoreconf` lines inside the chain -- and it is the only reason it is
    # not spelled that way.
    #
    # AHEAD OF THE CHAIN rather than immediately adjacent to autoreconf costs nothing, and
    # that is a statement about the two steps in between rather than a hope: the stub-header
    # step creates stubs/ and one symlink under it and touches none of the six.  autoreconf is
    # still the first command in this function that can rewrite any of them, so the snapshot
    # is still of pre-build content -- which is the property that matters, not the line number.
    acquire_build_lock
    snapshot_regenerated_makefiles

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
        ./configure --enable-l1tests "${configure_prefix_args[@]}" &&
        echo '###STEP:make all' &&
        make -j"$nproc_count" all &&
        echo '###STEP:make -C tests/L1Tests all' &&
        make -C tests/L1Tests all &&
        echo '###STEP:make -C tests/L2Tests all' &&
        make -C tests/L2Tests all
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
       Note that PKG_CHECK_MODULES ignores -I and -L: only pkg-config's path decides.
       If configure failed on the AIDL/Binder headers or libraries, it prints the roots it
       tried; supply the staging prefixes rather than adding -I or -L flags of your own,
       because configure derives every root from them and a flag added here would leave the
       hand-written ccec/src/Makefile configured differently. This run passed:
           ${configure_prefix_args[*]:-<none: configure looked for the sibling rdk-halif-aidl checkout>}
       If 'make -C tests/L2Tests all' failed, note that it builds TWO programs -- the L2
       runner and the out-of-process fake service host -- and that invocations D and E have
       no binary without both."
    }

    [ -x "$HDMICEC_ROOT/$TEST_BINARY_REL" ] || die "the build reported success but produced no
       executable at $HDMICEC_ROOT/$TEST_BINARY_REL. Full log: $build_log"
    log "build complete: $HDMICEC_ROOT/$TEST_BINARY_REL (log: $build_log)"
    announce_regenerated_makefiles
}

# ------------------------------------------------------------------------------------
# The build rewrote six git-tracked Makefiles.  THIS RUN PUTS THEM BACK ITSELF, from the
# snapshot it took before autoreconf, and this function's job is now to say so rather than
# to hand the caller a command.
#
# WHAT THIS FUNCTION USED TO DO, AND WHY IT NO LONGER DOES IT.  It built a
# `git -C <hdmicec> checkout -- <the six>` string and printed it as one of two offered
# routes.  Both routes were destructive and the printed one was the more dangerous of the
# two, because a paste is unreviewed: it restores whatever the index holds and destroys any
# uncommitted edit to ccec/src/Makefile, which is hand-written source rather than generated
# output.  Fixing only the action and leaving the advice in place would have left the footgun
# loaded and merely moved the trigger, so the advice went with it.  Nothing here prints a
# command for anyone to run.
# ------------------------------------------------------------------------------------
announce_regenerated_makefiles() {
    rule
    warn "MANDATORY BUILD HYGIENE: autoreconf and configure rewrite six GIT-TRACKED files."
    warn "Five are local toolchain output.  ccec/src/Makefile is NOT: it is a hand-written"
    warn "  RDK build file that configure clobbers anyway, so restoring it puts source back."
    warn "None of the six may be committed in its generated form."
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
    if [ -n "$MAKEFILE_SNAPSHOT_DIR" ]; then
        warn "THIS RUN WILL RESTORE THEM before it exits, from the snapshot it took of their"
        warn "  working-tree content immediately before autoreconf:"
        warn "    $MAKEFILE_SNAPSHOT_DIR"
        warn "  That happens on EVERY exit path -- success, build failure, gate failure and"
        warn "  Ctrl-C -- because the restore hangs off this script's exit trap.  You do not"
        warn "  need to run anything, and there is nothing to paste."
        warn "DO NOT revert these paths with 'git checkout'.  It restores what the INDEX holds,"
        warn "  not what was there, so it would silently destroy an uncommitted edit to any of"
        warn "  the six.  This script no longer offers that route in any form."
    else
        # Reachable only if do_build is ever called without its snapshot step, which would be
        # a defect in this script rather than a caller error -- so it says exactly that.
        warn "NO SNAPSHOT WAS TAKEN FOR THIS RUN, which should not be possible: do_build takes"
        warn "  one before autoreconf.  The six cannot be restored automatically, and this"
        warn "  script will not reach for 'git checkout' to cover for its own defect.  Compare"
        warn "  them against your own copy and report this."
    fi
}

# ------------------------------------------------------------------------------------
# `--restore` as a standalone action.
#
# WHAT IT IS FOR NOW.  Since `--build` restores from its own snapshot on every exit path,
# the normal case is that there is nothing left to do -- and confirming that is worth doing,
# because "the tree is clean" is the thing a caller actually wants to know after a build.
#
# WHAT IT REFUSES TO DO, AND WHY THE REFUSAL IS THE FEATURE.  With the six dirty and no
# snapshot from THIS invocation, the only mechanism left is the `git checkout` this script
# has removed everywhere else.  Reaching for it here would mean the one code path a caller
# invokes precisely when something has gone wrong is also the one that can destroy their
# work: an uncommitted edit to ccec/src/Makefile -- hand-written source, not output -- would
# be gone with no prompt and no copy.  So it reports what is dirty, says plainly why it will
# not act, and exits non-zero.  A non-zero status is the honest answer: the caller asked for
# a restore and did not get one.
# ------------------------------------------------------------------------------------
do_restore() {
    rule
    log "checking the tracked Makefiles in $HDMICEC_ROOT"

    # A snapshot from this same invocation is the only thing that can be restored from, and
    # the only way to have one is to have run --build here -- which parse_args forbids
    # combining with --restore.  So this arm exists for the internal caller (the exit trap)
    # rather than for the flag, and it is kept for exactly one reason: if the two are ever
    # allowed to combine, this function must restore rather than refuse.
    if [ -n "$MAKEFILE_SNAPSHOT_DIR" ]; then
        restore_regenerated_makefiles_from_snapshot
        return 0
    fi

    if ! command -v git >/dev/null 2>&1 \
       || ! git -C "$HDMICEC_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
        die "no snapshot exists for this invocation, and git is not usable here either, so
       there is no way to tell whether the six tracked Makefiles even need restoring.
       Nothing was changed.
           the six: ${REGENERATED_MAKEFILES[*]}
       Run '$SCRIPT_PATH --build ...' and it will snapshot them before autoreconf and
       restore them itself when it exits."
    fi

    local before
    before="$(git -C "$HDMICEC_ROOT" status --porcelain -- "${REGENERATED_MAKEFILES[@]}" 2>/dev/null || true)"
    if [ -z "$before" ]; then
        log "none of the six differs from the index; nothing to restore."
        log "  (--build restores them from its own snapshot before it exits, so this is the"
        log "  expected state after a build rather than a sign that nothing ran.)"
        return 0
    fi

    printf '%s\n' "$before" | sed 's/^/[run_coverage]   dirty: /' >&2
    die "the paths above differ from the index, and THIS INVOCATION HAS NO SNAPSHOT to
       restore them from.  Nothing was changed, deliberately.
       WHY THIS IS A REFUSAL AND NOT A FALLBACK.  The only other mechanism available is
       'git -C $HDMICEC_ROOT checkout -- <the six>', and this script does not run it in
       any mode.  That command restores whatever the INDEX holds rather than what was
       there, so on ccec/src/Makefile -- a hand-written RDK build file with its own object
       list and link command, which configure clobbers because it is in AC_CONFIG_FILES --
       it would silently destroy an uncommitted edit.  Losing someone's source is not an
       acceptable way to tidy up generated content.
       WHAT TO DO INSTEAD.  A snapshot only exists inside the invocation that took it, so
       let the build take one and put them back for you:
           $SCRIPT_PATH --build --run
       That snapshots all six before autoreconf and restores them from its exit trap on
       every path, including a failure or a Ctrl-C.  If you already know these six hold
       nothing you want, revert them however you normally revert a file -- that decision
       is yours to take deliberately, and it is not one a measurement tool should take for
       you."
}

# ====================================================================================
# THE INVOCATION MATRIX
# ====================================================================================
#
# WHY THERE IS A MATRIX AT ALL, rather than one run.  The middleware now compiles BOTH HAL
# back-ends into libRCEC and chooses between them ONCE PER PROCESS, inside LibCCEC::init,
# which is the first thing either harness does that forces Driver::getInstance().  By the
# time any TEST_F body runs the choice is made and cannot be changed, so ONE BINARY RUN
# YIELDS EXACTLY ONE SELECTION OUTCOME and every outcome that has to be covered needs its
# own process.  A single run cannot exercise both arms of the selection branch no matter
# what it is filtered to.
#
# THE FIVE INVOCATIONS, and what each is the only way to establish:
#
#   A  run_L1Tests  CEC_TEST_AIDL_MODE=absent        expects the LEGACY back-end
#      No service is registered and no host is launched.  This is the regression arm: the
#      whole pre-existing suite plus every case in the new contract suite that does not
#      require the AIDL back-end.  It is also the arm that demonstrates the
#      fallback-not-abort requirement on the one platform that naturally exhibits it -- a
#      host with no binder driver at all, where an unguarded service lookup would abort the
#      process rather than return an error.
#
#   B  run_L1Tests  CEC_TEST_AIDL_MODE=compatible    expects the AIDL back-end
#      The harness registers a compatible fake IN THIS PROCESS before init.  This is the arm
#      that proves the ADAPTER translates correctly -- array marshalling, status mapping,
#      the frame-length guard, the state guards.
#
#   C  run_L1Tests  CEC_TEST_AIDL_MODE=incompatible  expects the LEGACY back-end
#      The harness registers a fake that reports an interface hash of "-1".  This is the arm
#      that proves "present but not usable falls back", and it CANNOT be replaced by a unit
#      test: that is a FACTORY-level behaviour, and the selection helper that acts on the
#      answer (resolveBackEnd in ccec/src/Driver.cpp) sits in an anonymous namespace, so it
#      has internal linkage and no test can call it.  A unit test of the predicate proves the
#      predicate; only a process that STARTS with an incompatible service registered proves
#      the factory acts on it.  It is also only feasible IN-PROCESS: a remote Bn* service
#      cannot report bad metadata at all, because its generated onTransact answers those
#      transactions from compiled-in constants.
#
#   D  run_L2Tests  CEC_TEST_AIDL_MODE=absent        expects the LEGACY back-end
#      The end-to-end round trip -- HAL event to Bus reader to Connection to FrameListener --
#      driven through the legacy in-process mock.
#
#   E  run_L2Tests  CEC_TEST_AIDL_MODE=remote        expects the AIDL back-end
#      The L2 harness launches the out-of-process fake service host, waits for its readiness
#      token, and only then initializes.  The middleware holds a real Bp* proxy, transactions
#      cross the binder driver, and the listener callback arrives on a binder threadpool
#      thread.
#
# B AND E ARE BOTH REQUIRED AND NEITHER SUBSTITUTES FOR THE OTHER, which is worth stating
# because five invocations look like more than are needed.  An in-process registration IS NOT
# IPC: libbinder resolves a name registered in the calling process to the local BBinder, so
# interface_cast hands back that very object -- no proxy is created, no transaction crosses
# the driver, and the client threadpool is never involved.  B therefore proves translation
# and cannot prove transport; E proves transport and, because a remote service cannot report
# bad metadata, cannot reach the compatibility-rejection arms B and C reach.
#
# ------------------------------------------------------------------------------------
# THE FILTERS ARE MEASURED, NOT ESTIMATED -- and the counts are not written down at all.
#
# --gtest_filter selects by SUITE name, and several source files declare more than one suite,
# so a filter derived from file names would be wrong in a way nothing would catch.  The two
# suite groups below were read out of the built binary with --gtest_list_tests, and the case
# counts in the comments are what that listing reported on the host this was written on.
#
# THOSE COUNTS ARE DOCUMENTATION.  The gate does not use them: run_one_invocation asks the
# binary itself how many cases its own filter selects, every time.  That is deliberate, and it
# is what the two test files ask for in as many words -- adding a case must not require
# editing an expected number here, because a stale number is a gate that fails on a correct
# build and, worse, one that a caller then learns to override.
# ------------------------------------------------------------------------------------

# BACK-END-NEUTRAL: ten suites, 308 cases as measured, containing no reference to either HAL
# back-end -- no mock, no EXPECT_CALL, no Driver::getInstance.  They run under EVERY
# invocation, unchanged and unmodified, and that is the backward-compatibility evidence: the
# same cases, the same assertions, passing with either back-end resolved.
readonly NEUTRAL_SUITES='CECFrameTest.*:MessageDecoderTest.*:MessageDecoderTrackingTest.*:MessageEncoderTest.*:OpCodeTest.*:OperandsTest.*:UtilTest.*:ConditionVariableTest.*:MutexTest.*:ThreadTest.*'

# LEGACY-BOUND: eight suites, 175 cases as measured, whose EXPECT_CALLs name HdmiCec*
# functions a binder back-end never calls.  Under AIDL selection they would fail on unmet
# expectations rather than on behaviour, which is a false negative and not evidence of
# anything, so they run on invocation A only.  Their behavioural content is re-asserted
# back-end-agnostically by the contract suite, which is what keeps nothing dropped.
# Two of them also hold the ill-typed static_cast<DriverImpl &> route through the legacy
# receive callback, so restricting them to A makes that route structurally unreachable under
# AIDL selection rather than merely unused.
readonly LEGACY_BOUND_SUITES='BusTest.*:ConnectionTest.*:IntegrationFlowTest.*:DriverTest.*:DriverImplAsyncTest.*:HdmiCecDriverMockTest.*:LibCCECTest.*:LibCCECUninitializedTest.*'

# The contract suite's own fixtures, partitioned by which back-end each one requires.  Read
# from the FIXTURE MANIFEST in tests/L1Tests/ccec/test_DriverAidl.cpp, which is the authority
# for its own case set, and cross-checked against the built binary.
#   back-end independent  Compatibility 11, Preflight 5, LocalInstance 9   -- run under A, B and C
#   legacy back-end only  Selection 4, LegacyArm 4                         -- run under A only
#   AIDL back-end only    Session 19, Transmit 12                          -- run under B only
readonly CONTRACT_ANY_BACKEND_SUITES='DriverAidlCompatibilityTest.*:DriverAidlPreflightTest.*:DriverAidlLocalInstanceTest.*'
readonly CONTRACT_LEGACY_ONLY_SUITES='DriverAidlSelectionTest.*:DriverAidlLegacyArmTest.*'
readonly CONTRACT_AIDL_ONLY_SUITES='DriverAidlSessionTest.*:DriverAidlTransmitTest.*'

# The L2 tier's own filter.  All three of its fixtures share the DualPath prefix precisely so
# that one glob selects the tier, and the registered total is IDENTICAL for D and E by
# design: the two arm-specific fixtures SKIP rather than FAIL when the resolved back-end is
# not theirs, so every case is registered and reported under both and only the pass/skip
# split differs.  That is what lets one expected count serve both invocations.
readonly L2_SUITES='DualPath*'

# ------------------------------------------------------------------------------------
# THE MATRIX, one record per invocation, '|'-separated:
#
#   1 letter            the invocation's name, and the suffix on every artifact it writes
#   2 tier              L1 or L2 -- which runner, which directory, which fixture pattern
#   3 mode              the CEC_TEST_AIDL_MODE value handed to the harness
#   4 back-end          the back-end name that MUST appear in the selected-path log line
#   5 select filter     --gtest_filter for the run; empty means "no filter"
#   6 exclude filter    the COMPLEMENT, spelled out as suite globs rather than derived
#   7 synopsis          one line, printed when the invocation starts
#
# WHY FIELD 6 EXISTS RATHER THAN BEING COMPUTED.  The check it feeds is
# `selected + excluded == registered`, measured all three times from the binary, and that is
# the generalisation of the guard this script has always had: it used to compare the executed
# count against the UNFILTERED inventory and raise an advisory on any gap, which was exactly
# right when nothing was filtered and would now fire on every filtered invocation by design.
# Deriving the complement arithmetically (registered - selected) would prove nothing at all,
# because both sides would come from the same subtraction.  Spelling it out as suite globs and
# MEASURING it makes the reconciliation real, and it buys something the old check could not:
# a suite added to the binary and classified into neither group breaks the arithmetic and
# fails the invocation, instead of silently going unrun on four invocations out of five.
#
# INVOCATION A CARRIES A NEGATIVE FILTER, AND IT IS NOT OPTIONAL.  The two AIDL-only fixtures
# assert in SetUp that the AIDL back-end is the resolved one, and they FAIL rather than SKIP
# when it is not -- deliberately, because a skipped arm is indistinguishable from a passing
# one in an aggregate count, which is the swallowed-failure shape the whole suite is built to
# avoid.  So A excludes them by name.  Everything else in the binary runs.
# ------------------------------------------------------------------------------------
readonly INVOCATION_MATRIX=(
    "A|L1|absent|legacy|-${CONTRACT_AIDL_ONLY_SUITES}|${CONTRACT_AIDL_ONLY_SUITES}|no service registered: the regression arm, and the fallback-not-abort demonstration"
    "B|L1|compatible|AIDL|DriverAidl*:${NEUTRAL_SUITES}-${CONTRACT_LEGACY_ONLY_SUITES}|${LEGACY_BOUND_SUITES}:${CONTRACT_LEGACY_ONLY_SUITES}|in-process compatible fake: the adapter-translation arm"
    "C|L1|incompatible|legacy|${CONTRACT_ANY_BACKEND_SUITES}:${NEUTRAL_SUITES}|${LEGACY_BOUND_SUITES}:${CONTRACT_LEGACY_ONLY_SUITES}:${CONTRACT_AIDL_ONLY_SUITES}|in-process fake reporting hash \"-1\": present but not usable falls back"
    "D|L2|absent|legacy|${L2_SUITES}||legacy round trip through the in-process mock, end to end"
    "E|L2|remote|AIDL|${L2_SUITES}||out-of-process fake over real binder IPC, end to end"
)

# ------------------------------------------------------------------------------------
# WHICH INVOCATIONS THIS HOST CAN ACTUALLY RUN.
#
# Compiling and linking the AIDL back-end needs only headers and libraries and is fully
# verifiable anywhere.  EXECUTING an AIDL-selected case needs three more things: a kernel with
# binder support, a binder protocol version matching the one libbinder was built for, and a
# running service manager.  Where the driver node is absent, invocations B, C and E cannot run
# AT ALL -- and not merely because the lookup would fail.  Registering a name requires
# defaultServiceManager(), which opens the driver, and on the pinned binder stack that call
# ABORTS the process when the node is missing.  So an in-process fake is as unreachable as a
# remote one, and an attempt would take the whole runner down with it.
#
# The consequence for this script is a hard rule: an invocation that cannot run is REPORTED AS
# DEFERRED AND SKIPPED.  It is never attempted, never counted as passed, and never allowed to
# stand in for evidence it did not produce.  The final report says which invocations ran and
# which did not, and a run that skipped any of them cannot produce an acceptance verdict.
#
# The probe is the driver node itself, checked exactly as the middleware's own bounded
# preflight checks it first: does the node exist and can it be opened.  This script does NOT
# reimplement the rest of that predicate -- the protocol-version equality check and the bounded
# wait for binder handle 0 are production code in the middleware and are not duplicated here.
# What this probe answers is narrower and is all it claims: "is there any point attempting an
# AIDL-selected invocation on this host".
# ------------------------------------------------------------------------------------
readonly BINDER_DRIVER_NODE="${BINDER_DRIVER_NODE:-/dev/binder}"

binder_transport_present() {
    [ -e "$BINDER_DRIVER_NODE" ] || return 1
    # Readable-and-writable rather than merely present: the node can exist with permissions
    # that make it useless, and a run that got past this check only to abort inside libbinder
    # would report a crash where it should have reported a deferral.
    [ -r "$BINDER_DRIVER_NODE" ] && [ -w "$BINDER_DRIVER_NODE" ]
}

# ------------------------------------------------------------------------------------
# THE RUNTIME LIBRARY PATH FOR THE BINDER CLOSURE.
#
# libRCEC now links four libraries directly -- the two generated AIDL stub libraries, libbinder
# and libutils -- and libbinder itself pulls in liblog, libbase, libcutils and libcutils_sockets
# beneath it.  Those four are NOT direct edges of anything in the middleware and are deliberately
# not named as such, but the loader still has to find every one of them or the runner does not
# start.  So this covers the DIRECTORIES rather than enumerating the libraries: whatever the
# staging layout puts in them is what the closure needs.
#
# WHY THIS IS NOT A NO-OP EVEN WHERE ldconfig ALREADY RESOLVES THEM.  On a host whose loader
# cache has been taught about the staged libraries this adds nothing, and that is fine -- a
# duplicated directory on the search path costs a stat.  On a host where they are staged and NOT
# registered, which is the ordinary case for a build tree, it is the difference between a suite
# that runs and one that dies with "cannot open shared object file" before its first test.
# Handling only the first case would make this script work on the machine it was written on.
#
# Each directory is added only if it exists, so an unset prefix contributes nothing rather than
# an empty path element -- an empty element in LD_LIBRARY_PATH means the CURRENT DIRECTORY,
# which is a genuine hazard and not merely untidy.
# ------------------------------------------------------------------------------------
binder_runtime_library_path() { # $1 = the path built so far -> the extended path on stdout
    local path="${1:-}" candidate
    local -a candidates=()

    [ -z "$HALIF_LIB_DIR" ]  || candidates+=("$HALIF_LIB_DIR")
    [ -z "$HALIF_PREFIX" ]   || candidates+=("$HALIF_PREFIX/lib/halif")
    [ -z "$BINDER_SDK_DIR" ] || candidates+=("$BINDER_SDK_DIR/lib/binder" "$BINDER_SDK_DIR/lib")

    for candidate in "${candidates[@]:-}"; do
        [ -n "$candidate" ] || continue
        [ -d "$candidate" ] || continue
        # Already present?  Do not add it twice; a search path that grows on every call is how
        # a long-running loop ends up with an environment too large to exec.
        case ":$path:" in
            *":$candidate:"*) continue ;;
        esac
        path="$candidate${path:+:$path}"
    done
    printf '%s\n' "$path"
}

# Does this invocation need the binder transport in order to run at all?
#
# B and C need it even though their fake is in-process, for the reason given above: the
# registration itself opens the driver.  E needs it for the transport.  A and D need it not at
# all and must not be made to look as though they do -- A on a driverless host is exactly the
# fallback-not-abort evidence, so it is the one place the absence is a FEATURE of the run.
invocation_needs_binder() { # $1 = invocation letter
    case "$1" in
        B|C|E) return 0 ;;
        *)     return 1 ;;
    esac
}

# ------------------------------------------------------------------------------------
# Optional suite execution.  The order -- zero once, run the matrix, capture once -- is the
# whole point, and the first of those three words changed for a measured reason recorded at
# clause 4 in the header: gcov counters ACCUMULATE, and because the back-end selection
# resolves once per process, accumulation ACROSS the invocations is the only way both arms of
# the selection branch are ever covered by anything.  Zeroing per invocation would leave the
# capture describing only the last one; never zeroing would let an earlier run's evidence into
# the figures.  Zeroing once, before the first invocation, and capturing once, after the last
# one has passed, is the only arrangement that has neither failure mode.
#
# Verifying after each invocation is unchanged and is still what proves a suite ran rather
# than exited early.
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
# names the oldest, largest fixtures, so adding or renaming a test file does not require
# editing this list, while a run that exercised none of them is not this suite.
#
# ONE PATTERN PER TIER, because the two runners have DISJOINT fixture sets.  A single pattern
# covering both would accept an L1 run that produced only L2 fixture names and vice versa,
# which is precisely the "whatever ran was not this suite" condition it exists to catch.  Each
# invocation names which pattern applies, from its tier.
#
# THE L1 PATTERN GAINED THE CONTRACT SUITE'S BACK-END-INDEPENDENT FIXTURES, and that is not a
# weakening even though it widens what counts as "this suite".  The reason it is safe is that
# the pattern is no longer the guard against a PARTIAL run: the per-invocation count
# reconciliation below is, and it is far stronger -- it compares the executed count against
# what the invocation's own filter selects and requires selected + excluded to equal the whole
# registered inventory.  A run narrowed to one fixture now fails on the count with a specific
# number, where before it could only be caught by not matching a name.  Widening the name test
# while a real count test stands behind it costs nothing; widening it INSTEAD of one would have
# cost everything.
readonly EXPECTED_SUITE_PATTERN='"(classname|name)"[[:space:]]*:[[:space:]]*"(CECFrameTest|ConnectionTest|BusTest|DriverTest|LibCCECTest|OpCodeTest|MessageDecoderTest|MessageEncoderTest|OperandsTest|DriverAidlCompatibilityTest|DriverAidlPreflightTest|DriverAidlLocalInstanceTest)'

# The L2 tier's three fixtures.  All of them, not a subset: there are only three and they are
# the whole tier, so naming them all is the stable choice here rather than the fragile one.
readonly L2_EXPECTED_SUITE_PATTERN='"(classname|name)"[[:space:]]*:[[:space:]]*"(DualPathSelectionTest|DualPathLegacyFlowTest|DualPathAidlFlowTest)'

# Number of test cases a runner has REGISTERED, read from the binary itself.
#
# --gtest_list_tests enumerates without executing, so this is cheap.  A test line is indented by
# exactly two spaces; a fixture line is not indented, and GoogleTest's trailing "# GetParam() ="
# annotations are stripped before counting.
#
# THE FILTER PARAMETER IS THE WHOLE GENERALISATION, and it needs stating because the old version
# of this function REFUSED to take one.  Its comment said so: a filtered listing would agree with
# a filtered run and prove nothing.  That was correct when there was one unfiltered run to check,
# and it is still correct as far as it goes -- which is why the caller asks THREE questions rather
# than one, and never just the filtered one:
#
#   registered_test_count <...>            the whole inventory, no filter        -> registered
#   registered_test_count <...> "$select"  what this invocation intends to run   -> selected
#   registered_test_count <...> "$exclude" what it intends NOT to run            -> excluded
#
# `executed == selected` alone would indeed prove nothing, for exactly the reason the old comment
# gave.  `selected + excluded == registered`, with all three read independently from the binary, is
# what makes it an assertion: it is only satisfiable if the invocation's own account of what it
# runs and what it skips adds up to everything that exists.
#
# Prints the count, or nothing when the listing could not be obtained - in which case the caller
# treats the inventory as unknown rather than as satisfied.
registered_test_count() { # $1=directory  $2=binary name  $3=LD_LIBRARY_PATH  $4=filter (optional)
    local dir="$1" binary="$2" ld_path="$3" filter="${4:-}" listing
    local -a filter_args=()
    [ -z "$filter" ] || filter_args=("--gtest_filter=$filter")
    listing="$(cd "$dir" \
        && LD_LIBRARY_PATH="$ld_path" "$TIMEOUT_BIN" --foreground 120 \
            "./$binary" --gtest_list_tests "${filter_args[@]}" 2>/dev/null)" || return 0
    [ -n "$listing" ] || return 0
    printf '%s\n' "$listing" \
        | sed -n 's/^  \([A-Za-z_][A-Za-z0-9_]*\).*/\1/p' \
        | grep -c . || true
}

# ------------------------------------------------------------------------------------
# Verify one invocation's results file.
#
# EVERY GUARD THE SINGLE-RUN VERSION HAD IS STILL HERE, unchanged in intent and in most cases
# unchanged in text: the file must exist (it was deleted immediately beforehand), "tests" must
# be positive, executed = declared - disabled must be positive, failures and errors must both
# be zero even when the exit status was zero, and the results must name one of this tier's own
# fixtures.  None of them was weakened to accommodate a filtered run; the parameters below are
# what let them apply per invocation instead of once.
#
# WHAT CHANGED IS THE INVENTORY COMPARISON, and only its mechanism.  It used to compare the
# executed count against the UNFILTERED registered inventory and raise an advisory on any gap,
# which is exactly right when the run is unfiltered and fires on every filtered invocation by
# design.  It is now a per-invocation HARD CHECK against the count the invocation's own filter
# selects, backed by the selected + excluded == registered reconciliation described at
# registered_test_count.  That is stricter than the old advisory in two ways worth naming: a
# mismatch FAILS rather than merely noting, and a suite classified into neither the select nor
# the exclude group breaks the reconciliation instead of quietly going unrun.
# ------------------------------------------------------------------------------------
verify_results() { # $1=results JSON  $2=LD_LIBRARY_PATH  $3=label  $4=fixture pattern  $5=expected executed  $6=registered  $7=excluded
    local results="$1" ld_path="${2:-}" label="$3" fixture_pattern="$4"
    local expected="$5" registered="$6" excluded="$7" count

    [ -f "$results" ] || die "invocation $label exited 0 but wrote no results file at
       $results
       The file was deleted immediately before the run, so its absence means the binary
       produced no results at all.  A coverage figure cannot be attributed to a run that
       left no evidence it executed anything."

    # THE DECLARED COUNT AND THE EXECUTED COUNT ARE NOT THE SAME NUMBER.
    #
    # The GoogleTest JSON header carries the run's totals, and its "tests" field counts every case
    # in the selection INCLUDING the DISABLED_ ones, which are listed as entries and never
    # executed.  Both numbers are needed here and they are used for different things:
    #   * "declared" is what is compared against --gtest_list_tests below, because that listing
    #     also enumerates DISABLED_ cases -- comparing an executed count against an inclusive
    #     listing would report a phantom gap for every disabled case in the suite;
    #   * "executed" is what is REPORTED, because it is the number a reader quotes as evidence,
    #     and a suite with disabled cases does not execute as many as it declares.
    # sed rather than a JSON parser, because this script adds no dependency beyond the POSIX tools
    # it already needs; each field is taken from its first occurrence, which is the top-level
    # header preceding the "testsuites" array.
    local declared disabled failures errors
    declared="$(sed -n 's/^[[:space:]]*"tests"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$results" | head -1)"
    disabled="$(sed -n 's/^[[:space:]]*"disabled"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$results" | head -1)"
    failures="$(sed -n 's/^[[:space:]]*"failures"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$results" | head -1)"
    errors="$(sed -n 's/^[[:space:]]*"errors"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$results" | head -1)"
    : "${disabled:=0}" "${failures:=0}" "${errors:=0}"
    count="$declared"
    if [ -z "$count" ] || [ "$count" -le 0 ]; then
        die "invocation $label exited 0 but $results reports no tests (\"tests\": ${count:-absent}).
       An empty run cannot substantiate a coverage figure: every line would be reported
       unhit and the gate would fail for the wrong reason, or -- worse, with a partial
       filter -- a subset's figure would be reported as the suite's.
       This invocation's own --gtest_filter selected $expected case(s), so a zero here means
       either that filter matched nothing in this binary or something overrode it."
    fi

    local executed=$((declared - disabled))
    if [ "$executed" -le 0 ]; then
        die "invocation $label exited 0 and $results declares $declared case(s), but $disabled of them are
       DISABLED_ and so none actually executed.  A coverage figure cannot be attributed to a run in
       which nothing ran."
    fi
    # The results file is this run's evidence and a zero exit status is a different claim, so a
    # suite that recorded a failure or an error while still exiting 0 is treated as a failing suite.
    if [ "$failures" -ne 0 ] || [ "$errors" -ne 0 ]; then
        die "invocation $label exited 0 but $results records $failures failure(s) and $errors error(s).
       The results file is the evidence and it contradicts the exit status, so no coverage is
       reported for this run."
    fi

    grep -Eq "$fixture_pattern" "$results" || die "invocation $label exited 0 and $results
       reports $count test case(s), but not one of them belongs to a known fixture of this
       tier.  Whatever ran was not this suite, while the capture would credit this
       submodule's objects.  Check that the runner in that directory is the binary built
       from this tree and that GTEST_EXTRA_ARGS is not restricting the run to unrelated
       cases."

    # ------------------------------------------------------------------------------------
    # CHECK 2 OF THE THREE PER-INVOCATION CHECKS: the executed count is the count this
    # invocation's own filter selects.
    #
    # Everything above establishes that SOMETHING from this tier ran; none of it establishes
    # that ALL OF WHAT THIS INVOCATION WAS SUPPOSED TO RUN ran.
    # `GTEST_EXTRA_ARGS=--gtest_filter=-SomeFailingSuite.*` leaves a positive count, keeps
    # recognisable fixture names in the JSON, and exits 0 -- so the suite reads as green while
    # the cases that would have failed were never executed, and the coverage captured
    # afterwards is credited to a run that skipped them.
    #
    # The expected number is READ FROM THE BINARY with this invocation's declared filter, never
    # written down here, so adding a test case requires no edit to this script.  A caller who
    # overrides the filter through GTEST_EXTRA_ARGS therefore fails HERE, loudly, with both
    # numbers named -- which is the intended outcome: GTEST_EXTRA_ARGS may add diagnostic flags,
    # but re-filtering an invocation makes it a different invocation and this script will not
    # report one as the other.
    # ------------------------------------------------------------------------------------
    if [ -z "$registered" ] || [ "$registered" -le 0 ] || [ -z "$expected" ] || [ "$expected" -le 0 ]; then
        die "invocation $label ran, but its expected case count could not be read from the binary
       (--gtest_list_tests produced nothing usable: registered='${registered:-}',
       selected='${expected:-}').  Without it there is no way to tell a complete invocation
       from a partial one, and a coverage figure credited to an unverifiable execution is
       exactly what this script exists not to produce."
    fi

    # Both sides include DISABLED_ cases -- the listing enumerates them and the JSON's "tests"
    # counts them -- so this compares SELECTION against SELECTION and a disabled case cannot by
    # itself open a gap.
    if [ "$count" -ne "$expected" ]; then
        # The override is named ONLY when it is actually set.  An unconditional "filter :" line
        # printed an empty field on every other cause of a mismatch, which reads as though the
        # filter were the culprit when it is not -- and the overwhelmingly likeliest cause of
        # this failure is a GTEST_EXTRA_ARGS filter layered on top of the matrix's own, so when
        # it IS set that fact belongs in the message rather than in the reader's head.
        local override_note=''
        if [ -n "${GTEST_EXTRA_ARGS:-}" ]; then
            override_note="
           GTEST_EXTRA_ARGS      : set to [$GTEST_EXTRA_ARGS], which narrows the matrix's filter"
        fi
        die "invocation $label executed a different set from the one it declared.
           declared by the filter : $expected case(s)
           actually selected      : $count case(s)$override_note
       A run that exits 0 because the cases that would have failed were filtered out is not a
       green run, and coverage captured from it would be credited to an execution that did not
       happen.  Both numbers above came from this same binary moments apart, so the difference
       is real."
    fi

    # ------------------------------------------------------------------------------------
    # THE RECONCILIATION.  selected + excluded == registered, all three measured.
    #
    # This is what the old unfiltered comparison became rather than what replaced it.  Its
    # purpose was to notice cases that were never selected; under a matrix that is by design,
    # so the question changed from "were any cases skipped" to "are the skipped cases exactly
    # the ones this invocation SAID it skips".  A suite added to the binary and classified into
    # neither group fails here -- which the old check could not have caught either, since a new
    # unclassified suite would simply have widened the gap it already tolerated as an advisory.
    # ------------------------------------------------------------------------------------
    local accounted=$((expected + excluded))
    if [ "$accounted" -ne "$registered" ]; then
        die "invocation $label does not account for the whole registered inventory.
           selected by its filter : $expected
           excluded by its filter : $excluded
           sum                    : $accounted
           registered in binary   : $registered
       $(( registered - accounted )) case(s) are in neither group, so they run under no
       invocation and nothing would ever report them.  This almost always means a new test
       suite was added without being classified in INVOCATION_MATRIX: put it in the select
       filter of the invocations that can run it and in the exclude filter of the rest.
       Do NOT widen a select filter with a wildcard to make this arithmetic close -- that
       would run the new suite under an invocation whose back-end it may not support, which
       is the failure this classification exists to prevent."
    fi

    if [ "$disabled" -gt 0 ]; then
        log "invocation $label EXECUTED $executed case(s) per $results (of $declared declared;
       $disabled DISABLED_ and therefore not run) -- matching the $expected its filter selects,
       with $excluded excluded and $registered registered"
    else
        log "invocation $label EXECUTED all $executed declared case(s) -- matching the $expected its"
        log "  filter selects, with $excluded excluded and $registered registered in the binary"
    fi
}

# ------------------------------------------------------------------------------------
# CHECK 3 OF THE THREE: the selected-path line, in THIS invocation's own log.
#
# WHY THIS CHECK IS NOT OPTIONAL AND CANNOT BE INFERRED.  An invocation that was supposed to
# resolve the AIDL back-end and quietly resolved the legacy one instead is GREEN in every
# other respect: the back-end-neutral cases pass either way, the arm-specific fixtures skip
# rather than fail by design, the count reconciles, the exit status is zero.  Nothing in the
# results file records WHICH back-end ran.  So a mis-wired invocation E -- a host launched too
# late, a readiness token never written, a service name already taken -- would report a clean
# AIDL result having tested the legacy path from start to finish.  This grep is the only thing
# standing between that and an acceptance verdict.
#
# THE STRING IS NOT INVENTED HERE.  It is emitted once per process at LOG_INFO by the selection
# helper in ccec/src/Driver.cpp, from a single format constant with the back-end name as its one
# substitution, and both test files transcribe it verbatim for the same reason this does.  It
# reaches the log through CCEC_LOG, which prefixes a timestamp and the CEC log prefix, so the
# match is on the TRAILING SUBSTRING rather than on a whole line.
#
# EXACTLY ONE HIT IS REQUIRED, not at least one.  The two "service is not usable" lines that
# precede it on a legacy fallback are deliberately worded so that they do not match, which is
# what makes "one hit" meaningful: more than one would mean the selection resolved twice, and
# the whole design rests on it resolving once per process.
# ------------------------------------------------------------------------------------
readonly SELECTED_BACK_END_LOG_PREFIX='Driver::getInstance : HDMI CEC HAL back-end selected : '

assert_selected_back_end() { # $1=log file  $2=expected back-end name  $3=invocation label
    local log_file="$1" expected_back_end="$2" label="$3" hits other

    [ -f "$log_file" ] || die "invocation $label left no log at $log_file, so the back-end it
       selected cannot be established."

    hits="$(grep -c -F -- "${SELECTED_BACK_END_LOG_PREFIX}${expected_back_end}" "$log_file" || true)"
    if [ "${hits:-0}" -eq 1 ]; then
        log "invocation $label resolved the $expected_back_end back-end (selected-path line found, once)"
        return 0
    fi

    # Name what WAS selected when something was, because "expected AIDL" is a much less useful
    # diagnostic than "expected AIDL, got legacy".
    other="$(grep -o -- "${SELECTED_BACK_END_LOG_PREFIX}[A-Za-z]*" "$log_file" 2>/dev/null | sort -u | sed 's/^/         /' || true)"
    if [ "${hits:-0}" -eq 0 ]; then
        die "invocation $label did not resolve the $expected_back_end back-end.
       The selected-path line naming it is absent from
           $log_file
       Selected-path line(s) actually present:
${other:-         (none at all)}
       This invocation is therefore NOT evidence about the back-end it was configured for, and
       it is treated as a failure rather than as a pass, because every other signal it produces
       -- exit status, case count, fixture names -- is identical whichever back-end resolved.
       For invocation E the usual causes are a fake service host that never became ready, one
       launched after LibCCEC::init rather than before it, or a stale registration already
       holding the production service name.  For B and C, a harness that could not register its
       in-process fake."
    fi
    die "invocation $label emitted the $expected_back_end selected-path line $hits times, and it
       must appear EXACTLY ONCE: the selection resolves once per process and is held for the
       process lifetime, so a second line means it resolved twice.  Log: $log_file"
}

do_run() {
    # A run has to be BOUNDED, so `timeout` is a hard requirement of this path rather than an
    # optional nicety: the L1 suite drives real pthread mutexes, condition variables and threads,
    # and without a bound a deadlocked case hangs here for ever with no output, no exit status and
    # no gate.  Checked here, before a single counter is zeroed, so the tree is left untouched.
    [ -n "$TIMEOUT_BIN" ] || die "timeout was not found on PATH, and the suite will not be run
       without it, because the run would then be unbounded.  timeout ships with coreutils:
           sudo apt-get install -y coreutils
       Then re-run, or invoke this script without --run against counters produced elsewhere."
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
    # gate could pass on evidence the current tests did not produce.  A warning here would be
    # exactly the shape of "reported but not prevented", so this is FATAL, matching the sink
    # runner: refuse to measure rather than measure the wrong thing.
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

    log "counters zeroed ONCE for the whole matrix; the invocations accumulate into them"
    run_invocation_matrix

    local fresh
    fresh="$(gcda_count)"
    [ "$fresh" -gt 0 ] || die "every invocation passed but no *.gcda counters exist under
       $HDMICEC_ROOT.  Either the binaries are not the instrumented ones or the counters went
       somewhere unexpected.  Refusing to report a coverage figure that was not measured."
    log "the matrix produced $fresh .gcda counter file(s), accumulated across every invocation"
}

# ------------------------------------------------------------------------------------
# Run ONE invocation of the matrix and hold it to all three checks.
#
# ARTIFACTS ARE PER-INVOCATION AND A LATER ONE CANNOT OVERWRITE AN EARLIER ONE.  This used to
# be a single hard-coded run_L1Tests.log and a single rdkL1TestResults.json, which under a
# matrix would mean the last invocation's evidence standing in for all five: an empty or failed
# run would be erased by whichever green one came after it, and the artifact bundle would say
# nothing about how the result was reached.  The invocation LETTER goes in the name -- and a
# letter, not a timestamp, because clause 5 of the governing contract requires fixed artifact
# names with no timestamp in any of them, so that the same tree produces the same file set and
# a reader knows what to look for without listing the directory.
#
# ORDERING IS LOAD-BEARING AND IT IS THE HARNESS THAT OWNS IT.  On B and C the in-process fake
# must be registered, and on E the host must be launched and signalled ready, BEFORE
# LibCCEC::init -- because init is what forces the selection, and a service that appears after
# it leaves the already-resolved choice on legacy and produces a green run that proves nothing.
# Both harnesses do that themselves, in their global environment SetUp, before the init call.
# What this script contributes is the mode and the host's path, handed over as environment
# before the process starts, and then the assertion that the intended back-end actually
# resolved -- which is the only check that can catch a mis-ordering after the fact.
# ------------------------------------------------------------------------------------
run_one_invocation() { # $1=letter $2=tier $3=mode $4=back-end $5=select $6=exclude $7=synopsis
    local label="$1" tier="$2" mode="$3" back_end="$4"
    local select_filter="$5" exclude_filter="$6" synopsis="$7"
    local binary_rel binary_name binary_dir fixture_pattern

    case "$tier" in
        L1) binary_rel="$TEST_BINARY_REL";    fixture_pattern="$EXPECTED_SUITE_PATTERN" ;;
        L2) binary_rel="$L2_TEST_BINARY_REL"; fixture_pattern="$L2_EXPECTED_SUITE_PATTERN" ;;
        *)  die "internal error: invocation $label names an unknown tier '$tier'" ;;
    esac
    binary_name="$(basename -- "$binary_rel")"
    binary_dir="$HDMICEC_ROOT/$(dirname -- "$binary_rel")"

    [ -x "$HDMICEC_ROOT/$binary_rel" ] || die "invocation $label needs $binary_rel and it is
       not an executable in this tree:
           $HDMICEC_ROOT/$binary_rel
       Build it with:  $SCRIPT_PATH --build
       The L2 tier is configured by tests/L2Tests/Makefile in configure.ac's AC_CONFIG_FILES
       and recursed into from tests/Makefile.am's SUBDIRS, both under --enable-l1tests."

    local run_log="$OUTPUT_DIR/run_invocation_${label}.log"
    local results_json="$OUTPUT_DIR/rdkTestResults_invocation_${label}.json"
    local ld_path="${LD_LIBRARY_PATH:-}"

    # Deleted BEFORE the binary starts, so a results file that exists afterwards can only
    # have been written by this invocation.  Without this, a binary that produced nothing would
    # be judged against whatever an earlier invocation left at the same path -- and with
    # per-invocation names, against its own earlier attempt.
    rm -f -- "$results_json"
    if [ -d "$GTEST_PREFIX/lib" ]; then
        ld_path="$GTEST_PREFIX/lib${ld_path:+:$ld_path}"
    fi
    ld_path="$(binder_runtime_library_path "$ld_path")"

    # THE EXPECTED COUNTS ARE READ FROM THE BINARY, NOW, with this invocation's own filters.
    # Three separate listings, so the reconciliation in verify_results compares independently
    # obtained numbers rather than two views of one subtraction.
    local registered selected excluded
    registered="$(registered_test_count "$binary_dir" "$binary_name" "$ld_path" '')"
    selected="$(registered_test_count "$binary_dir" "$binary_name" "$ld_path" "$select_filter")"
    if [ -n "$exclude_filter" ]; then
        excluded="$(registered_test_count "$binary_dir" "$binary_name" "$ld_path" "$exclude_filter")"
    else
        # No exclude filter means the invocation claims to run everything, and the
        # reconciliation then reduces to selected == registered, which is exactly right.
        excluded=0
    fi
    : "${registered:=0}" "${selected:=0}" "${excluded:=0}"

    rule
    log "INVOCATION $label -- $synopsis"
    log "  runner            : $binary_rel"
    log "  CEC_TEST_AIDL_MODE: $mode"
    log "  expected back-end : $back_end"
    log "  filter            : ${select_filter:-<none: the whole registered inventory>}"
    log "  expected cases    : $selected selected, $excluded excluded, $registered registered"
    log "  results JSON      : $results_json"
    log "  log               : $run_log"
    log "  time limit        : ${SUITE_TIMEOUT}s (SUITE_TIMEOUT)"
    if [ -n "$GTEST_EXTRA_ARGS" ]; then
        log "  extra gtest args  : $GTEST_EXTRA_ARGS"
        # ANY caller-supplied gtest argument makes this run diagnostic, not acceptance.
        #
        # A filter is the obvious case - `--gtest_filter=-KnownFailingSuite.*` leaves a
        # recognisable, positive, exit-0 run with the failures excised - but it is not the only
        # one: --gtest_shuffle changes an order this suite is known to be fragile under,
        # --gtest_repeat changes what the counters accumulate, and --gtest_also_run_disabled_tests
        # changes which cases the figures came from.  Rather than enumerate a permitted subset
        # and be wrong about the next one, every custom argument is recorded, so the verdict is
        # ADVISORY and the exit status is $EXIT_ADVISORY.
        #
        # A FILTER PASSED THIS WAY NOW FAILS THE INVOCATION rather than being reported as a
        # partial one, and that is a deliberate tightening.  GTEST_EXTRA_ARGS is appended after
        # this invocation's own --gtest_filter, and GoogleTest lets the last one win, so an
        # override silently turns invocation B into some other selection entirely.  The expected
        # count in verify_results comes from the invocation's DECLARED filter, so the two
        # disagree and it dies with both numbers named.  Diagnostic flags remain useful here;
        # re-filtering does not, because a re-filtered invocation is a different invocation and
        # this script will not report one as the other.
        note_advisory "GTEST_EXTRA_ARGS was set to '$GTEST_EXTRA_ARGS', so the suite was NOT run
       the way an acceptance run runs it.  Custom arguments can change which cases execute, in
       what order and how often, so this run's figures are diagnostic."
    fi

    # --gtest_print_time=1 and the JSON output mirror the workflow's own run step.
    # GTEST_EXTRA_ARGS is intentionally word-split: it is a user-supplied argument list.
    #
    # BOUNDED, because an unbounded run is not a run that can fail.  A test that deadlocks --
    # this suite drives real pthread mutexes, condition variables and threads, and a mock that
    # never signals is one edit away -- would otherwise hang here for ever: no output, no exit,
    # no gate, and in CI a job killed by the runner's own limit with no diagnosis attached.
    # `timeout --foreground` is kept, but NOT for the reason it usually is: the suite no longer
    # runs in the terminal's foreground group at all (see the cancellation block next to
    # `trap on_exit`).  It is kept because --foreground makes `timeout` refrain from putting its
    # child in a process group of its OWN, which is what keeps `timeout` and the test binary
    # inside the one group the signal handler signals.  A Ctrl-C reaches the suite through that
    # handler now, not through the terminal.  --kill-after is added where the local timeout
    # preserves exit 124 with it (see the probe above), to escalate to SIGKILL if the suite
    # ignores SIGTERM.  Exit 124 is timeout's own signal that the limit fired, and it is reported
    # as its own outcome below rather than folded into "the suite failed", because the two need
    # different fixes.
    #
    # CANCELLABLE: the suite is launched in the BACKGROUND and in its OWN PROCESS GROUP, and this
    # shell waits on it, so an external INT/TERM/HUP reaches the handler WHILE the suite runs
    # rather than after it (see the block next to `trap on_exit`).  `timeout --foreground` is kept
    # for exactly that reason: it deliberately does not put its child in a new process group, so
    # `timeout` and the test binary both stay in the one group the handler signals.
    # THE HARNESS ENVIRONMENT, and the one variable this script deliberately does NOT set.
    #
    # CEC_TEST_AIDL_MODE is what distinguishes one invocation from the next.  It is read by
    # tests/L1Tests/test_main.cpp and tests/L2Tests/test_main.cpp and by nothing else -- no
    # production source reads it, and none may.  Each harness hard-fails on a value it does not
    # implement rather than quietly downgrading to `absent`, which is why passing `remote` to
    # the L1 runner or `compatible` to the L2 runner is a loud error rather than a silent
    # legacy run.  The matrix never does either.
    #
    # CEC_FAKE_AIDL_HOST_PATH tells the L2 harness where the out-of-process fake service host
    # is.  It is set for BOTH L2 invocations even though only E launches anything: D reads it
    # not at all, so setting it costs nothing and keeps one code path.
    #
    # CEC_FAKE_HOST_READY_FD IS NOT SET HERE, AND MUST NOT BE.  It names the number of an
    # INHERITED DESCRIPTOR -- the write end of a pipe whose read end the waiting parent holds --
    # and the parent is the L2 harness, not this script.  The harness creates the pipe, forks,
    # sets that variable in the CHILD's environment, and blocks on the read end under a bounded
    # timeout.  A value exported from here would name a descriptor this script never opened, and
    # the host treats an unusable descriptor as a hard failure precisely so that a botched
    # handoff is reported rather than silently answered on standard output.  Setting it would
    # therefore break the very handoff it looks like it is helping with.
    #
    # THE READINESS WAIT AND THE REAPING ARE THE HARNESS'S TOO, and both are bounded there: it
    # matches the fixed token verbatim, fails the run rather than proceeding when the token does
    # not arrive, and terminates and reaps the host in its teardown.  This script's contribution
    # to that contract is SUITE_TIMEOUT as an outer bound and the selected-path assertion as the
    # after-the-fact check -- a host that never became ready leaves the harness failing, and a
    # host that became ready too late leaves a legacy selection, which the assertion catches.
    #
    # NOTHING HERE STARTS A BINDER THREADPOOL.  The client-side pool is DriverAidlImpl::open()'s
    # to start, during init; the host starts its own service-side pool.  A third one from the
    # harness or from here would duplicate an ownership the production back-end already holds.
    local rc=0
    set -m
    (
        cd "$binary_dir"
        CEC_TEST_AIDL_MODE="$mode" \
        CEC_FAKE_AIDL_HOST_PATH="$HDMICEC_ROOT/$FAKE_HOST_BINARY_REL" \
        LD_LIBRARY_PATH="$ld_path" \
        exec "$TIMEOUT_BIN" --foreground "${TIMEOUT_KILL_AFTER[@]}" "$SUITE_TIMEOUT" \
            "./$binary_name" --gtest_print_time=1 "--gtest_output=json:$results_json" \
                ${select_filter:+"--gtest_filter=$select_filter"} \
                ${GTEST_EXTRA_ARGS:+$GTEST_EXTRA_ARGS}
    ) >"$run_log" 2>&1 &
    SUITE_PGID=$!
    set +m
    # `|| rc=$?` rather than a set +e / set -e pair: toggling errexit inside a function that may
    # itself have been invoked in a `||` or `if !` context re-arms it where the caller had
    # deliberately suppressed it.  A trapped signal interrupts the wait either way, which is the
    # whole point of waiting rather than running the suite in the foreground.
    #
    # THE PROCESS GROUP IS ALSO WHAT REAPS THE FAKE SERVICE HOST, and no new machinery was added
    # for it.  The harness forks the host from inside this group, so a cancelled or timed-out
    # invocation has its host signalled along with the runner by stop_suite_group, which
    # addresses the group BY ID.  That matters: a pattern-based kill would match anything merely
    # mentioning the binary's name, up to and including the harness that started this script.
    wait "$SUITE_PGID" || rc=$?
    SUITE_PGID=''

    # The tail is the part a reader needs; the whole log is on disk either way.
    "$AWK_BIN" '/^\[==========\]|^\[  PASSED  \]|^\[  FAILED  \]|^\[  SKIPPED \]|tests? from .* ran/' "$run_log" \
        | sed 's/^/[run_coverage]   /' || true

    # ------------------------------------------------------------------------------------
    # CHECK 1 OF THE THREE: the exit status.
    # ------------------------------------------------------------------------------------
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
        printf '%s\n' "--- last 30 lines ---" >&2
        tail -n 30 -- "$run_log" | sed 's/^/[run_coverage]   /' >&2 || true
        die "invocation $label did not finish within ${SUITE_TIMEOUT}s and was terminated (exit $rc).
       This is a HANG, not a test failure: the last test named in the log above is where it
       stopped.  Coverage is not captured, because a suite that was killed part-way through
       produced partial counters.  Investigate that test, or raise the bound deliberately with
       SUITE_TIMEOUT=<seconds> if the suite has legitimately grown.  Full log: $run_log
       On invocation E specifically, a hang can also mean the fake service host is waiting on a
       service manager that is not running: reaching one blocks in one-second retries until
       binder handle 0 resolves, and this bound is what turns that into a reported failure."
    fi
    if [ "$rc" -ne 0 ]; then
        warn "invocation $label exited $rc. Full log: $run_log"
        printf '%s\n' "--- last 30 lines ---" >&2
        tail -n 30 -- "$run_log" | sed 's/^/[run_coverage]   /' >&2 || true
        die "invocation $label FAILED, so coverage was not measured and no later invocation was run.
       A red suite makes every coverage figure meaningless: fix the tests first.
       Coverage is deliberately NOT captured for a failing run, so that no report can
       ever be produced from one, and the artifacts of this and every earlier invocation
       are left in place under $OUTPUT_DIR for diagnosis."
    fi
    log "invocation $label exited 0"

    # ------------------------------------------------------------------------------------
    # CHECKS 2 AND 3.  Both are FATAL, and both run before the next invocation starts.
    # ------------------------------------------------------------------------------------
    verify_results "$results_json" "$ld_path" "$label" "$fixture_pattern" \
                   "$selected" "$registered" "$excluded"
    assert_selected_back_end "$run_log" "$back_end" "$label"

    log "invocation $label PASSED all three checks (exit status, case count, selected back-end)"
}

# ------------------------------------------------------------------------------------
# Run the whole matrix, in order, stopping at the first invocation that fails.
#
# WHY IT STOPS RATHER THAN COLLECTING FAILURES.  The counters accumulate into one set, and a
# capture taken after a failed invocation would describe a mixture of complete and incomplete
# executions with no way to tell which lines came from which.  There is no useful report to be
# had from a partial matrix, so the run ends where the evidence stops being attributable -- and
# every artifact written so far survives, because they are per-invocation and nothing overwrites
# them.
#
# AN INVOCATION THAT CANNOT RUN IS DEFERRED, NOT SKIPPED QUIETLY AND NEVER PASSED.  On a host
# with no binder driver, B, C and E cannot execute at all -- an attempt would abort inside
# libbinder rather than fail a test.  Those are recorded as deferred, listed in the summary, and
# turned into an advisory reason, so the run cannot produce an acceptance verdict while claiming
# five invocations' worth of evidence from two.
# ------------------------------------------------------------------------------------
INVOCATIONS_RUN=''
INVOCATIONS_DEFERRED=''

run_invocation_matrix() {
    local binder_available=0
    if binder_transport_present; then
        binder_available=1
    fi

    rule
    log "the invocation matrix: ${#INVOCATION_MATRIX[@]} invocation(s), one process each,"
    log "  all against this one build so the gcov counters accumulate into a single capture"
    if [ "$binder_available" -eq 1 ]; then
        log "  binder transport: $BINDER_DRIVER_NODE is present and usable, so every invocation runs"
    else
        log "  binder transport: $BINDER_DRIVER_NODE is absent or unusable on this host"
        log "  => the AIDL-selected invocations are DEFERRED, not attempted and not passed."
        log "     Executing one needs a kernel with binder support, a matching binder protocol"
        log "     version and a running service manager.  Compiling and linking the back-end"
        log "     needs none of those and has already been verified by the build."
    fi

    local record label tier mode back_end select_filter exclude_filter synopsis
    for record in "${INVOCATION_MATRIX[@]}"; do
        IFS='|' read -r label tier mode back_end select_filter exclude_filter synopsis <<< "$record"

        if [ "$binder_available" -eq 0 ] && invocation_needs_binder "$label"; then
            rule
            warn "INVOCATION $label DEFERRED -- $synopsis"
            warn "  It needs the binder transport and $BINDER_DRIVER_NODE is not usable here."
            warn "  Not attempted: on the pinned binder stack, registering a service name calls"
            warn "  defaultServiceManager(), which opens the driver and ABORTS the process when"
            warn "  the node is missing -- so an attempt would take the runner down with it and"
            warn "  destroy the counters the invocations that CAN run have accumulated."
            INVOCATIONS_DEFERRED="${INVOCATIONS_DEFERRED}${INVOCATIONS_DEFERRED:+, }$label"
            continue
        fi

        run_one_invocation "$label" "$tier" "$mode" "$back_end" \
                           "$select_filter" "$exclude_filter" "$synopsis"
        INVOCATIONS_RUN="${INVOCATIONS_RUN}${INVOCATIONS_RUN:+, }$label"
    done

    rule
    log "invocations run      : ${INVOCATIONS_RUN:-none}"
    log "invocations deferred : ${INVOCATIONS_DEFERRED:-none}"
    [ -n "$INVOCATIONS_RUN" ] || die "no invocation ran at all, so there is nothing to measure.
       Every one of them was deferred for want of a binder transport, which cannot be right:
       invocations A and D need none.  Check that both runners exist and are executable."

    if [ -n "$INVOCATIONS_DEFERRED" ]; then
        note_advisory "invocation(s) $INVOCATIONS_DEFERRED were DEFERRED, not run: this host has no
       usable binder transport at $BINDER_DRIVER_NODE.  The figures below therefore describe
       only invocation(s) $INVOCATIONS_RUN, so no arm of the selection branch that needs the
       AIDL back-end was exercised and the branch gate cannot be satisfied from this run.
       Re-run the whole matrix on a binder-capable host for an acceptance verdict; nothing here
       may be reported as though those invocations had passed."
    fi
}


# ------------------------------------------------------------------------------------
# Artifact paths.  Fixed names, matching CI's, inside a directory that defaults outside
# the git tree.  Every destination is checked before it is written: a symlink or a
# wrong-typed object at one of these paths would otherwise let a run scribble somewhere
# nobody intended.
# ------------------------------------------------------------------------------------
RAW_TRACE=''
FILTERED_TRACE=''
LINE_GATE_TRACE=''
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
        # TMPDIR is caller-controlled too, so it gets the same treatment as --output-dir:
        # collapsed first, then checked for where it actually lands.  Without this, a TMPDIR
        # of /tmp/x/../../../etc would put the minted root under /etc by exactly the route
        # the named branch refuses -- the guard has to cover both ways in or it covers
        # neither.
        parent="$(canonicalise_path_lexically "$parent")"
        assert_artifact_location_plausible "$parent/hdmicec-l1-coverage" "TMPDIR"
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
        # Then COLLAPSE it, and only then decide whether the place it names is acceptable.
        # Both steps precede create_safe_dir deliberately: that function CREATES what is
        # missing, so anything it is handed has already been created by the time a later
        # check could object.  The collapsed form is printed when it differs from what the
        # caller typed, because a path that quietly means somewhere else is the whole
        # defect being closed here and a reader must be able to see it happen.
        local named_output_dir="$OUTPUT_DIR"
        OUTPUT_DIR="$(canonicalise_path_lexically "$OUTPUT_DIR")"
        if [ "$OUTPUT_DIR" != "$named_output_dir" ]; then
            log "the output directory you named collapses to: $OUTPUT_DIR"
            log "  as given: $named_output_dir"
        fi
        assert_artifact_location_plausible "$OUTPUT_DIR" "--output-dir / COVERAGE_OUTPUT_DIR"
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
    # Applied to BOTH branches, after ownership has been established: the minted branch is
    # already 0700 so this is a no-op there, and a caller-supplied directory is brought to
    # the same posture instead of being trusted as found.  See the function's own comment.
    restrict_artifact_dir_to_owner "$OUTPUT_DIR"

    RAW_TRACE="$OUTPUT_DIR/coverage.info"
    FILTERED_TRACE="$OUTPUT_DIR/filtered_coverage.info"
    # The line gate's own input: filtered_coverage.info with the dependency headers removed.
    # A THIRD name rather than an overwrite, so all three stages of the trace survive side by
    # side and a reader can see exactly what each step took out.
    LINE_GATE_TRACE="$OUTPUT_DIR/line_gate_coverage.info"
    HTML_DIR="$OUTPUT_DIR/coverage"
    PER_FILE_TSV="$OUTPUT_DIR/per_file_coverage.tsv"
    UNCOVERED_TXT="$OUTPUT_DIR/uncovered_lines.txt"
    readonly RAW_TRACE FILTERED_TRACE LINE_GATE_TRACE HTML_DIR PER_FILE_TSV UNCOVERED_TXT

    assert_safe_artifact_path "$RAW_TRACE" file
    assert_safe_artifact_path "$FILTERED_TRACE" file
    assert_safe_artifact_path "$LINE_GATE_TRACE" file
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
# Step 2b -- ONE TRACE, TWO CONSUMERS.  Produce the line gate's input by removing the
# dependency headers, and leave the branch gate reading the UNFILTERED capture.
#
# THE SPLIT, AND WHY IT IS NOT AN INCONSISTENCY.  Two gates now read two different traces, and
# that is deliberate because they are asking two different questions:
#
#   THE BRANCH GATE READS THE UNFILTERED CAPTURE ($RAW_TRACE).  Some of the branches SC6 asks
#   for by name live inside halcompat.h's isCompatible<>() -- the empty-hash arm, the "-1" arm,
#   the "notfrozen" arm, the era and major comparisons -- and some of the arcs it needs are in
#   header-only code.  Filtering first would delete exactly the evidence the gate exists to
#   check, and a gate that cannot see its own subject is worse than no gate: it would pass
#   silently and for ever.
#
#   THE LINE GATE READS THIS DERIVATIVE.  Its threshold is 80% over an aggregate, and an
#   aggregate is only meaningful against a stable denominator.  Admitting six binder SDK
#   headers and four generated stub headers would change the file set, change the denominator,
#   make the figure incomparable with the recorded baseline, and -- because per-file gating is
#   on -- start failing the run over how well this suite covers a third-party header it neither
#   owns nor tests.  So the dependency headers come out, and only they.
#
# WHY NOT SIMPLY GATE BOTH ON THE SAME TRACE.  Both single-trace answers are worse.  Gating
# lines on the unfiltered capture imports the denominator problem above.  Gating branches on
# the filtered copy deletes halcompat.h's arcs.  Two consumers of one capture, each reading the
# form that answers its own question, is the only arrangement in which both gates mean
# something -- and the capture is still taken ONCE, so the two are views of the same evidence
# rather than two measurements that could disagree.
# ------------------------------------------------------------------------------------
filter_dependency_headers() {
    rule
    log "producing the line gate's trace: removing dependency headers from the filtered trace"

    # The fixed globs, plus whatever the caller's staging prefixes tell us about this host.  A
    # Yocto-style layout can put the binder headers somewhere that no fixed glob describes, and
    # the prefixes are the only thing that knows where.  Derived here rather than at definition
    # time because they depend on values parse_args may still have been changing.
    local -a excludes=("${DEPENDENCY_EXCLUDES[@]}")
    local root
    for root in "$BINDER_SDK_INCLUDE_DIR" "$BINDER_SDK_DIR"; do
        [ -n "$root" ] || continue
        excludes+=("$root/*")
    done

    local g
    for g in "${excludes[@]}"; do
        log "    exclude: $g"
    done
    log "    RETAINED: halcompat.h -- a consumed HALIF header, and the branches SC6 names live in it"

    assert_output_dir_still_safe "$LINE_GATE_TRACE"
    rm -f -- "$LINE_GATE_TRACE"
    lcov_run -r "$FILTERED_TRACE" \
        "${excludes[@]}" \
        -o "$LINE_GATE_TRACE" \
        "${LCOV_CONFIG_ARGS[@]}" \
        "${LCOV_RC_ARGS[@]}" \
        --ignore-errors "$LCOV_FILTER_IGNORE" \
        >"$OUTPUT_DIR/filter_dependencies.log" 2>&1 \
        || die "removing the dependency headers failed. Full log: $OUTPUT_DIR/filter_dependencies.log
       Tail:
$(tail -n 20 -- "$OUTPUT_DIR/filter_dependencies.log" 2>/dev/null | sed 's/^/         /')"

    [ -s "$LINE_GATE_TRACE" ] || die "removing the dependency headers produced an empty trace
       ($LINE_GATE_TRACE).  One of the globs above must be matching this submodule's own source;
       compare them against the file list in $FILTERED_TRACE before changing anything else."

    # ------------------------------------------------------------------------------------
    # THE TWO FILES THAT MUST SURVIVE, ASSERTED RATHER THAN ASSUMED.
    #
    # A glob that accidentally described this migration's own source would leave a gate passing
    # over code nobody measured -- the exact failure mode a filter can produce while looking
    # like it is working.  The globs above do not name them, but "the globs do not name them" is
    # a claim about the globs and this is a check on the result.
    # ------------------------------------------------------------------------------------
    local protected_file
    for protected_file in "${PROTECTED_TRACE_FILES[@]}"; do
        grep -q "^SF:.*/${protected_file}\$" "$LINE_GATE_TRACE" || die "$protected_file is
       ABSENT from the line gate's trace, and it must never be.  It is production source this
       migration is answerable for, so a gate computed without it would pass over unmeasured
       code.  Either one of the exclusion globs above matches it -- check them against
           grep '^SF:' $FILTERED_TRACE
       -- or it was never compiled into the objects the capture walked."
    done

    # THE FILE SET IS ENUMERATED, NOT SUMMARISED.  What came out and what stayed are both
    # printed, because a policy about which dependency headers belong in a denominator is only
    # auditable if a reader can see the decision applied to real paths.
    local before after removed
    before="$(grep -c '^SF:' "$FILTERED_TRACE" || true)"
    after="$(grep -c '^SF:' "$LINE_GATE_TRACE" || true)"
    removed=$((before - after))
    log "line gate trace: $LINE_GATE_TRACE ($after file record(s); $removed dependency header(s) removed)"

    if [ "$removed" -gt 0 ]; then
        log "  removed:"
        # comm needs sorted input; LC_ALL=C so the ordering is the same on every host.
        LC_ALL=C comm -23 \
            <(grep '^SF:' "$FILTERED_TRACE"   | sed 's/^SF://' | LC_ALL=C sort) \
            <(grep '^SF:' "$LINE_GATE_TRACE" | sed 's/^SF://' | LC_ALL=C sort) \
            | sed 's/^/[run_coverage]     /'
    fi

    # Any surviving record from outside the submodule is a NEWLY APPEARING dependency header
    # that this policy has not classified.  It is reported rather than filtered, because
    # silently widening a filter is how a denominator drifts: the reader decides whether it is
    # consumed HALIF source that belongs in the gate, like halcompat.h, or toolchain code that
    # does not.
    local unclassified
    unclassified="$(grep '^SF:' "$LINE_GATE_TRACE" | sed 's/^SF://' \
                    | grep -v "^$HDMICEC_ROOT/" | grep -v '/halcompat\.h$' || true)"
    if [ -n "$unclassified" ]; then
        warn "the line gate's trace still holds file record(s) from outside this submodule that"
        warn "  this policy has not classified:"
        printf '%s\n' "$unclassified" | sed 's/^/[run_coverage]     /' >&2
        warn "  They are IN the aggregate denominator and IN the per-file gate as things stand."
        warn "  Decide deliberately: a consumed HALIF header belongs there, as halcompat.h does;"
        warn "  third-party toolchain code does not and belongs in DEPENDENCY_EXCLUDES.  Nothing"
        warn "  is filtered automatically here, because a filter that widens itself is how a"
        warn "  coverage denominator drifts without anyone deciding that it should."
        note_advisory "the line gate's trace holds unclassified dependency header record(s) from
       outside this submodule, so its aggregate denominator is not the one the recorded baseline
       was measured over.  They are listed in this run's output; classify them in
       DEPENDENCY_EXCLUDES or accept them deliberately."
    fi

    log "  retained inside this submodule: $(grep '^SF:' "$LINE_GATE_TRACE" | sed 's/^SF://' | grep -c "^$HDMICEC_ROOT/" || true) file record(s)"
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
        "$LINE_GATE_TRACE" \
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
    lcov_run --summary "$LINE_GATE_TRACE" \
        "${LCOV_CONFIG_ARGS[@]}" \
        "${LCOV_RC_ARGS[@]}" \
        --ignore-errors "$LCOV_SUMMARY_IGNORE" 2>&1 \
        | sed 's/^/[run_coverage]   /' \
        || die "lcov --summary failed for $LINE_GATE_TRACE"
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
# ------------------------------------------------------------------------------------
# PROVENANCE.  Who produced these artifacts, from which tree, with which script.
#
# Coverage numbers are only evidence if they can be tied to a revision. Numbers with no
# revision are indistinguishable from numbers produced by a different checkout, a different
# runner body, or a different toolchain - and once separated from their tree they cannot be
# re-attached, because nothing in an lcov trace records where it came from.
#
# So attribution is written three ways, deliberately redundantly:
#   * provenance.txt, the full manifest, beside the traces;
#   * the superproject's short SHA appended to the genhtml title, which puts it on EVERY page
#     of the HTML report rather than in one file that can be separated from it;
#   * a header block in the per-file TSV and the uncovered-lines list, so those two remain
#     self-describing when quoted on their own - which is how they are usually read.
#
# The runner's OWN sha256 is included because a trace can outlive the script that made it. If
# the recorded hash does not match the script now on disk, the artifact was produced by a
# different runner and its acceptance decision does not transfer.
# ------------------------------------------------------------------------------------
git_sha_of() { # $1 = repository path;  prints "<sha> (<branch>)<dirty marker>" or "unavailable"
    local repo="$1" sha branch dirty=''
    command -v git >/dev/null 2>&1 || { printf 'unavailable (no git)\n'; return 0; }
    git -C "$repo" rev-parse --git-dir >/dev/null 2>&1 || { printf 'unavailable (not a repository)\n'; return 0; }
    sha="$(git -C "$repo" rev-parse HEAD 2>/dev/null)" || sha=''
    [ -n "$sha" ] || { printf 'unavailable (no HEAD)\n'; return 0; }
    branch="$(git -C "$repo" rev-parse --abbrev-ref HEAD 2>/dev/null)" || branch='?'
    # --porcelain over tracked paths only: untracked build residue is not a content difference
    # and must not be reported as one, or every instrumented tree would read as dirty.
    if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
        dirty='  [DIRTY: tracked files modified]'
    fi
    printf '%s (%s)%s\n' "$sha" "$branch" "$dirty"
}

sha256_of() { # $1 = file;  prints the hex digest, or a reason
    local f="$1"
    [ -f "$f" ] || { printf 'absent\n'; return 0; }
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -- "$f" 2>/dev/null | "$AWK_BIN" '{print $1; exit}'
    else
        printf 'unavailable (no sha256sum)\n'
    fi
}

write_provenance() {
    PROVENANCE_TXT="$OUTPUT_DIR/provenance.txt"
    assert_output_dir_still_safe "$PROVENANCE_TXT"

    local super short
    super="$WS"
    short="$(git -C "$super" rev-parse --short=12 HEAD 2>/dev/null)" || short=''
    if [ -n "$short" ]; then
        GENHTML_TITLE="$GENHTML_TITLE @ $short"
    else
        GENHTML_TITLE="$GENHTML_TITLE @ revision-unavailable"
    fi

    {
        printf 'HDMI-CEC middleware L1 coverage -- ARTIFACT PROVENANCE\n'
        printf '=====================================================\n\n'
        printf 'Generated (UTC)      : %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || printf 'unavailable')"
        printf 'Host                 : %s\n' "$(uname -n 2>/dev/null || printf 'unavailable')"
        printf 'Clone index          : %s\n' "${CLONE_INDEX:-unset}"
        printf 'Workspace root       : %s\n' "$WS"
        printf 'Submodule root       : %s\n' "$HDMICEC_ROOT"
        printf 'Output directory     : %s\n' "$OUTPUT_DIR"
        printf '\nREVISIONS\n'
        printf '  superproject       : %s\n' "$(git_sha_of "$WS")"
        local sub
        for sub in hdmicec entservices-hdmicecsource entservices-hdmicecsink entservices-testframework \
                   entservices-apis entservices-helpers Thunder ThunderTools; do
            if [ -d "$WS/$sub" ]; then
                printf '  %-18s : %s\n' "$sub" "$(git_sha_of "$WS/$sub")"
            fi
        done
        printf '\nRUNNER AND CONFIGURATION\n'
        printf '  runner path        : %s\n' "$SCRIPT_PATH"
        printf '  runner sha256      : %s\n' "$(sha256_of "$SCRIPT_PATH")"
        printf '  runner bytes       : %s\n' "$(wc -c <"$SCRIPT_PATH" 2>/dev/null | tr -d ' ' || printf 'unavailable')"
        printf '  lcov config        : %s\n' "$SCRIPT_DIR/.lcovrc_l1"
        printf '  lcov config sha256 : %s\n' "$(sha256_of "$SCRIPT_DIR/.lcovrc_l1")"
        printf '  line bar           : %s%%  (per-file gating: %s)\n' \
            "$COVERAGE_MIN" "$( [ "$COVERAGE_PER_FILE_GATE" -eq 1 ] && printf on || printf off )"
        printf '  branch data        : forced on (--rc branch_coverage=1)\n'
        printf '\nTOOLCHAIN\n'
        printf '  lcov               : %s\n' "$(lcov_run --version 2>/dev/null | head -n1 || printf 'unavailable')"
        printf '  gcov               : %s\n' "$(gcov --version 2>/dev/null | head -n1 || printf 'unavailable')"
        printf '  compiler           : %s\n' "$("${CXX:-g++}" --version 2>/dev/null | head -n1 || printf 'unavailable')"
        printf '\nHOW TO CHECK THIS ARTIFACT STILL APPLIES\n'
        printf '  1. Compare the superproject revision above with `git rev-parse HEAD`.\n'
        printf '  2. Compare the runner sha256 above with `sha256sum %s`.\n' "$SCRIPT_PATH"
        printf '  If either differs, these numbers were produced from a different tree or a\n'
        printf '  different script, and the acceptance decision they carry does not transfer.\n'
    } >"$PROVENANCE_TXT" || die "could not write $PROVENANCE_TXT"

    chmod 600 -- "$PROVENANCE_TXT" 2>/dev/null || true
    log "provenance: $PROVENANCE_TXT"
    log "  superproject : $(git_sha_of "$WS")"
    log "  runner sha256: $(sha256_of "$SCRIPT_PATH")"
}

write_per_file_tsv() {
    local tmp_tsv="$OUTPUT_DIR/.per_file_coverage.tsv.tmp"
    assert_output_dir_still_safe "$PER_FILE_TSV"
    rm -f -- "$tmp_tsv"

    {
        printf '# Per-file coverage for the HDMI-CEC middleware L1 suite, derived from\n'
        printf '# %s\n' "$LINE_GATE_TRACE"
        printf '# PROVENANCE  superproject %s\n' "$(git_sha_of "$WS")"
        printf '# PROVENANCE  hdmicec      %s\n' "$(git_sha_of "$HDMICEC_ROOT")"
        printf '# PROVENANCE  runner       %s  sha256 %s\n' "$SCRIPT_PATH" "$(sha256_of "$SCRIPT_PATH")"
        printf '# PROVENANCE  generated    %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || printf unavailable)"
        printf '# Full manifest: %s\n' "${PROVENANCE_TXT:-provenance.txt}"
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
        ' "$LINE_GATE_TRACE" \
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
    } >"$tmp_tsv" || { rm -f -- "$tmp_tsv"; die "per-file parsing of $LINE_GATE_TRACE failed"; }

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
        printf '# %s\n' "$LINE_GATE_TRACE"
        printf '# Paths are relative to %s. A line is listed when its DA: record shows a hit\n' "$HDMICEC_ROOT"
        printf '# count of zero. Files with full line coverage are omitted.\n'
        printf '# Line coverage bar in force for this run: %s%%\n' "$COVERAGE_MIN"
        printf '# PROVENANCE  superproject %s\n' "$(git_sha_of "$WS")"
        printf '# PROVENANCE  runner       %s  sha256 %s\n' "$SCRIPT_PATH" "$(sha256_of "$SCRIPT_PATH")"
        printf '# Full manifest: %s\n' "${PROVENANCE_TXT:-provenance.txt}"
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
        ' "$LINE_GATE_TRACE" \
        | LC_ALL=C sort -t "$(printf '\t')" -k1,1 \
        | "$AWK_BIN" -F'\t' '{ printf "%s\t%s\t%s\t%s\n    uncovered lines: %s\n", $1, $2, $3, $4, $5 }'
    } >"$tmp" || { rm -f -- "$tmp"; die "uncovered-line enumeration failed for $LINE_GATE_TRACE"; }
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
# ====================================================================================
# THE BRANCH-COVERAGE MANIFEST AND ITS GATE
# ====================================================================================
#
# WHAT PROBLEM THIS SOLVES.  The requirement is full branch coverage of the SELECTION and the
# ARRAY-ADAPTATION code specifically, and a whole-file percentage cannot establish that:
# ccec/src/DriverAidlImpl.cpp contains many branches that are neither, so a high file figure can
# sit happily on top of an uncovered selection arm.  The gate therefore has to name individual
# arms -- which means mapping a semantic arm onto something a trace actually contains.
#
# WHY A MAPPING IS UNAVOIDABLE.  LCOV records a branch as
#     BRDA:<line>,<block>,<branch>,<taken>
# and those are COORDINATES, not names.  Nothing in the trace says "this is the
# service-not-found arm".  So the mapping from semantic arm to coordinate has to be produced
# from a real trace and written down, and it is written down HERE, with the source line quoted
# beside each entry so that a later reader can re-derive it rather than take it on trust.
#
# WHY IT LIVES IN THIS SCRIPT RATHER THAN IN A FILE OF ITS OWN.  A sibling manifest file would
# be a twenty-fifth path in a change whose diff check requires exactly twenty-four in-submodule
# paths and nothing more, so it would fail that check.  The manifest is data plus gate logic,
# both of which a shell script can hold perfectly well, so it holds them.
#
# IDENTIFICATION IS BY BLOCK AND BRANCH INDEX, NEVER BY LINE ALONE.  A short-circuited
# condition contributes several arcs on ONE line -- `if (a && b)` is at least two -- so a
# line-only key would match an arm the entry did not mean and would pass or fail on the wrong
# arc.  Every entry therefore carries all three of line, block and branch.
#
# THE GATE FAILS ON TWO CONDITIONS, AND THE SECOND IS THE ONE THAT EARNS ITS KEEP:
#   (a) a mapped record whose `taken` count is ZERO -- the arm exists and nothing drove it;
#   (b) a mapped record that is ABSENT FROM THE TRACE ALTOGETHER -- which is what catches a
#       refactor that silently deleted an arm.  Without (b), deleting the branch would make the
#       gate greener rather than redder, and a gate that rewards deletion is worse than none.
#
# `-` IS RECORDED AS UNEXERCISABLE, NOT AS A FAILURE.  gcov emits `-` for an arc it considers
# impossible to drive.  That is information about the compiler's control-flow graph, not about
# the test set, and treating it as a failure would produce a gate nobody can ever satisfy.
#
# THIS MANIFEST IS REVIEWED WHENEVER A MAPPED FILE CHANGES.  Coordinates are a function of the
# source: inserting a line above a mapped arm moves it, and adding a condition to a mapped `if`
# renumbers its branches.  A stale coordinate shows up as condition (b) -- absent from the
# trace -- which is the loud failure rather than the quiet one, but it is still a review that
# belongs with the source change and not with a puzzled reading of this gate's output.
#
# ------------------------------------------------------------------------------------
# THE COORDINATES ARE NOT POPULATED IN THIS COMMIT, AND THAT IS STATED RATHER THAN FAKED.
#
# A coordinate is only real if it came out of a real unfiltered trace produced by a full
# five-invocation run.  Three of those five invocations cannot execute on a host without a
# binder driver, so the arms belonging to them have no trace to be read out of here, and the
# arms that DO have one would be a half-populated manifest presenting itself as a whole one.
# Writing plausible-looking numbers would be the exact failure this whole script is built
# against: a figure that looks measured and is not.
#
# So the manifest below ships the ARM LIST -- which is knowledge, and is the part that took the
# analysis -- with its coordinate fields empty, and the gate reports honestly that it could not
# check them.  An unpopulated gate NEVER reports success and never contributes a pass: it
# records an advisory reason, which is what stops the run producing an acceptance verdict.
# Populating it is a one-time step on the binder-capable job: run the full matrix, read
# coverage.info, and fill in the line, block and branch fields against the quoted source lines.
# ------------------------------------------------------------------------------------
#
# RECORD FORMAT, '|'-separated:
#   1 id         short stable name for the arm, used in the gate's output
#   2 file       path suffix as it appears after SF: in the trace
#   3 line       gcov line number     -- EMPTY until populated on a binder-capable host
#   4 block      gcov block index     -- EMPTY until populated
#   5 branch     gcov branch index    -- EMPTY until populated
#   6 reacher    the invocation or unit test that drives this arm.  An arm with no named
#                reacher is a DEFECT IN THE TEST SET, not a target to lower -- so every entry
#                below has one, and a future entry without one is the finding rather than the
#                gate's problem.
#   7 status     required | unreachable
#   8 source     the source line, quoted, so the coordinate can be re-derived
# ====================================================================================
readonly BRANCH_MANIFEST=(
    # ---- GROUP 1: the selection helper's three arms and their log lines.
    # Reached by invocations A, B and C -- one arm each, which is the whole reason the matrix
    # has three L1 invocations rather than one.
    "selection.aidl-selected|ccec/src/Driver.cpp||||invocation B|required|if (aidlBackEnd.isServiceAvailable()) {"
    "selection.legacy-fallback|ccec/src/Driver.cpp||||invocations A and C|required|if (aidlBackEnd.isServiceAvailable()) {"
    "selection.preflight-ok-no-service|ccec/src/Driver.cpp||||invocation C|required|if (DriverAidlImpl::isBinderPreflightOk()) {"
    "selection.preflight-unavailable|ccec/src/Driver.cpp||||invocation A|required|if (DriverAidlImpl::isBinderPreflightOk()) {"

    # ---- GROUP 2: halcompat's compatibility predicate, arm by arm.
    # The template form is what the selection calls; the reject arms are only reachable
    # IN-PROCESS, because a remote Bn* service answers metadata transactions from compiled-in
    # constants and so cannot report a bad hash or a bad version at all.
    "compat.reject-null|rdk-halif-aidl/common/current/halcompat.h||||unit test, DriverAidlCompatibilityTest|required|if (service == nullptr) {"
    "compat.reject-empty-hash|rdk-halif-aidl/common/current/halcompat.h||||unit test, DriverAidlCompatibilityTest|required|if (hash.empty() || hash == \"-1\") {"
    "compat.reject-minus-one-hash|rdk-halif-aidl/common/current/halcompat.h||||invocation C at factory level; unit test otherwise|required|if (hash.empty() || hash == \"-1\") {"
    "compat.reject-notfrozen|rdk-halif-aidl/common/current/halcompat.h||||unit test, DriverAidlCompatibilityTest|required|if (hash == \"notfrozen\") {"
    "compat.accept-exact|rdk-halif-aidl/common/current/halcompat.h||||invocation B; unit test|required|return detail::isCompatible(I::VERSION, service->getInterfaceVersion());"
    "compat.accept-newer-same-major|rdk-halif-aidl/common/current/halcompat.h||||unit test, DriverAidlCompatibilityTest|required|&& serverVersion >= clientVersion);"
    "compat.reject-cross-major|rdk-halif-aidl/common/current/halcompat.h||||unit test, DriverAidlCompatibilityTest|required|&& major(serverVersion) == major(clientVersion)"
    "compat.reject-cross-era|rdk-halif-aidl/common/current/halcompat.h||||unit test, DriverAidlCompatibilityTest|required|: (era(serverVersion) == era(clientVersion)"

    # ---- GROUP 2a: TWO ARMS THAT ARE UNREACHABLE BY CONSTRUCTION.  Recorded with their
    # proofs so that nobody spends time hunting them, and NOT gated -- an unreachable arm
    # cannot be covered and a gate that demanded it could never be satisfied.
    #
    # PROOF FOR BOTH.  IHdmiCec::VERSION is 1000.  Under the encoding
    # era*100000 + major*1000 + minor*10 + bugfix that decodes to era 0, major 1.
    #
    # (1) OLDER-SAME-MAJOR IS AN EMPTY SET for this client.  A server value with era 0 and
    #     major 1 satisfies v < 100000 and (v/1000) % 100 == 1, which for era 0 means
    #     v/1000 == 1, i.e. v in [1000, 1999] -- and EVERY member of that range is >= 1000.
    #     So "same era, same major, server older than client" has no members at all.
    #     A server reporting 999 does NOT belong here: major(999) == 0, so it is rejected by
    #     the CROSS-MAJOR condition, and labelling such a case "older same-major" would
    #     mis-attribute which condition decided it.
    # (2) THE ERA >= 1 TERNARY ARM is unreachable for this client, because
    #     (era(server) >= 1 && era(client) >= 1) has era(client) == 0 as a conjunct and is
    #     therefore false for every possible server value.
    #
    # WORTH KNOWING WHILE READING halcompat.h's OWN PROOFS: its
    # static_assert(!isCompatible(3000, 2000), "era0 older rejected") is MISLABELLED.
    # major(3000) == 3 and major(2000) == 2, so that case is decided by the major inequality
    # and merely re-covers cross-major.  A genuine older-same-major call needs equal era AND
    # equal major with a lower server -- detail::isCompatible(3020, 3000), for instance.
    "compat.older-same-major|rdk-halif-aidl/common/current/halcompat.h||||UNREACHABLE: era-0/major-1 servers occupy [1000,1999], all >= VERSION 1000|unreachable|&& serverVersion >= clientVersion);"
    "compat.era1-ordering|rdk-halif-aidl/common/current/halcompat.h||||UNREACHABLE: era(client) == 0 falsifies the conjunction for every server|unreachable|return (era(serverVersion) >= 1 && era(clientVersion) >= 1)"

    # ---- GROUP 3: the bounded binder preflight.
    # The POSITIVE arm needs a driver, so invocation B owns it; every negative arm is driven by
    # a unit test calling the predicate with a driver path of the test's choosing, which is what
    # makes them testable without rendering the runner's own driver unusable.
    "preflight.positive|ccec/src/DriverAidlImpl.cpp||||invocation B|required|the preflight's all-checks-passed arm"
    "preflight.node-absent|ccec/src/DriverAidlImpl.cpp||||unit test, DriverAidlPreflightTest|required|the driver-node existence check"
    "preflight.node-unopenable|ccec/src/DriverAidlImpl.cpp||||unit test, DriverAidlPreflightTest|required|the driver-node open check"
    "preflight.protocol-mismatch|ccec/src/DriverAidlImpl.cpp||||unit test, DriverAidlPreflightTest|required|the protocol-version equality check"
    "preflight.handle-zero-timeout|ccec/src/DriverAidlImpl.cpp||||unit test, DriverAidlPreflightTest|required|the bounded wait for binder handle 0"

    # ---- GROUP 4: the one-element address arrays, add and remove, all three outcomes each.
    "addlogical.ok-true|ccec/src/DriverAidlImpl.cpp||||invocation B|required|addLogicalAddresses(one, &ok) succeeding"
    "addlogical.ok-false|ccec/src/DriverAidlImpl.cpp||||invocation B|required|ok == false -> AddressNotAvailableException"
    "addlogical.status-not-ok|ccec/src/DriverAidlImpl.cpp||||invocation B|required|a non-ok binder Status -> IOException"
    "removelogical.ok-true|ccec/src/DriverAidlImpl.cpp||||invocation B|required|removeLogicalAddresses(one, &ok) succeeding"
    "removelogical.ok-false|ccec/src/DriverAidlImpl.cpp||||invocation B|required|ok == false -> logged at LOG_EXP and ignored"
    "removelogical.status-not-ok|ccec/src/DriverAidlImpl.cpp||||invocation B|required|a non-ok binder Status -> logged and ignored"

    # ---- GROUP 5: getLogicalAddresses, every cardinality plus the failure.
    "getlogical.empty|ccec/src/DriverAidlImpl.cpp||||invocation B|required|an empty vector -> return 0"
    "getlogical.exactly-one|ccec/src/DriverAidlImpl.cpp||||invocation B|required|exactly one entry -> return vec[0]"
    "getlogical.more-than-one|ccec/src/DriverAidlImpl.cpp||||invocation B|required|vec.size() > 1 -> log the count, return vec[0]"
    "getlogical.status-not-ok|ccec/src/DriverAidlImpl.cpp||||invocation B|required|a non-ok binder Status -> return 0"

    # ---- GROUP 6: the frame-length guard, both sides.  The over-length side is difference 1
    # of the authorized observable differences, so both arms are evidence rather than trivia.
    "framelen.within-contract|ccec/src/DriverAidlImpl.cpp||||invocation B|required|a frame at or below the 16-byte sendMessage contract"
    "framelen.over-contract|ccec/src/DriverAidlImpl.cpp||||invocation B|required|an over-length frame -> IOException, never truncated"
)

# Set by apply_branch_gate and folded into apply_gate's own failure count, so the script keeps
# ONE final verdict and one exit status rather than two gates racing to exit first.
BRANCH_GATE_FAILURES=0

# ------------------------------------------------------------------------------------
# Print the BRDA records for one file suffix out of a trace, as "line block branch taken".
#
# Reads the trace once per file rather than once per manifest entry: several entries share a
# file, and re-walking a trace for each of thirty-odd arms would turn a gate into a cost.
# ------------------------------------------------------------------------------------
branch_records_for_file() { # $1=trace  $2=SF path suffix
    "$AWK_BIN" -v want="$2" '
        /^SF:/ {
            path = substr($0, 4)
            # SUFFIX match anchored on the END of the path, with the manifest supplying enough
            # leading directory components to be unambiguous.  Both halves matter and both were
            # checked against a real trace: end-anchoring is what stops "ccec/src/Driver.cpp"
            # matching "ccec/src/DriverAidlImpl.cpp" or "ccec/src/DriverImpl.cpp", and the
            # directory components are what stop it matching "tests/L1Tests/ccec/test_Driver.cpp".
            # A bare basename would satisfy neither.
            infile = (index(path, want) > 0 && index(path, want) + length(want) - 1 == length(path))
            next
        }
        /^end_of_record/ { infile = 0; next }
        infile && /^BRDA:/ {
            rec = substr($0, 6)
            n = split(rec, f, ",")
            if (n >= 4) print f[1], f[2], f[3], f[4]
        }
    ' "$1"
}

# ------------------------------------------------------------------------------------
# THE BRANCH GATE.  Reads the UNFILTERED trace, per ONE TRACE, TWO CONSUMERS in the header:
# some of the arms above live in halcompat.h, which the line gate's derivative keeps but which
# any narrower filtering would remove.
#
# IT RUNS AFTER THE SUITE RESULTS AND AFTER THE LINE GATE'S INPUT IS PREPARED, and it records
# its failures rather than exiting on them, so that apply_gate remains the single place a
# verdict is issued.  Coverage is subordinate to functional passes: by the time this runs, every
# invocation has already been run and held to its three checks, and a red matrix never reaches
# here at all.
# ------------------------------------------------------------------------------------
apply_branch_gate() {
    rule
    log "branch-arm gate: checking the manifest's named arms against the UNFILTERED trace"
    log "  trace: $RAW_TRACE"

    [ -s "$RAW_TRACE" ] || die "the branch gate has no trace to read at $RAW_TRACE."

    local record id file line block branch reacher status source
    local required=0 unreachable=0 unpopulated=0 covered=0 zero_taken=0 absent=0 unexercisable=0
    local -a zero_list=() absent_list=()

    # One pass per distinct file, cached in a variable keyed by nothing more elaborate than the
    # file we last read -- the manifest is grouped by file, so this is a single read per group.
    local cached_file='' cached_records=''

    for record in "${BRANCH_MANIFEST[@]}"; do
        IFS='|' read -r id file line block branch reacher status source <<< "$record"

        if [ "$status" = 'unreachable' ]; then
            unreachable=$((unreachable + 1))
            log "  UNREACHABLE  $id"
            log "               $reacher"
            continue
        fi
        required=$((required + 1))

        if [ -z "$line" ] || [ -z "$block" ] || [ -z "$branch" ]; then
            unpopulated=$((unpopulated + 1))
            continue
        fi

        if [ "$file" != "$cached_file" ]; then
            cached_records="$(branch_records_for_file "$RAW_TRACE" "$file")"
            cached_file="$file"
        fi

        local taken
        taken="$(printf '%s\n' "$cached_records" \
                 | "$AWK_BIN" -v l="$line" -v b="$block" -v br="$branch" \
                       '$1 == l && $2 == b && $3 == br { print $4; found = 1; exit }
                        END { if (!found) print "ABSENT" }')"

        case "$taken" in
            ABSENT)
                absent=$((absent + 1))
                # The quoted source line is carried into the report, not just held in the
                # manifest: a reader told an arm has moved needs the line to look for, and
                # making them go and read the manifest to find it is a step this can save.
                absent_list+=("$id  ($file:$line block $block branch $branch)")
                absent_list+=("    manifest source: $source")
                ;;
            -)
                # gcov's own judgement that the arc cannot be driven.  Recorded, not failed.
                unexercisable=$((unexercisable + 1))
                log "  UNEXERCISABLE  $id -- gcov reports '-' for this arc"
                ;;
            0)
                zero_taken=$((zero_taken + 1))
                zero_list+=("$id  ($file:$line block $block branch $branch)")
                zero_list+=("    should be reached by: $reacher")
                zero_list+=("    manifest source:     $source")
                ;;
            *)
                covered=$((covered + 1))
                ;;
        esac
    done

    log "  manifest: $required required arm(s), $unreachable recorded unreachable by construction"

    # ------------------------------------------------------------------------------------
    # THE UNPOPULATED CASE.  It is neither a pass nor a failure of the TEST SET: it is this
    # gate reporting that it could not do its job, which is the only honest thing to say when
    # the coordinates it needs have never been read out of a real trace.
    # ------------------------------------------------------------------------------------
    if [ "$unpopulated" -gt 0 ]; then
        warn "  $unpopulated of $required required arm(s) have NO COORDINATES in the manifest, so"
        warn "  they were NOT checked.  This is not a pass and is not counted as one."
        warn "  The coordinates come from a real unfiltered trace after a FULL five-invocation"
        warn "  run, which a host with no binder driver cannot produce -- so they are shipped"
        warn "  empty rather than guessed.  Populate them on the binder-capable job: run the"
        warn "  whole matrix, then read $RAW_TRACE and fill in the line, block and branch"
        warn "  fields of BRANCH_MANIFEST against the source line quoted in each entry."
        note_advisory "the branch-arm gate did not run: $unpopulated of $required required arm(s) in
       BRANCH_MANIFEST have no line/block/branch coordinates yet, so full branch coverage of the
       selection and array-adaptation code is NOT established by this run.  The arm list and the
       gate are in place; the coordinates are populated from a full five-invocation trace on a
       binder-capable host."
    fi

    if [ "$unexercisable" -gt 0 ]; then
        log "  $unexercisable mapped arc(s) reported '-' by gcov and are recorded as unexercisable"
        log "    rather than failed: that is the compiler's view of its own control-flow graph,"
        log "    not a statement about the tests."
    fi

    # ------------------------------------------------------------------------------------
    # FAILURE CONDITION (b): a mapped arm that is not in the trace at all.
    # Listed FIRST because it usually means the source moved, which changes what the reader
    # should do next -- review the manifest against the source, rather than write a test.
    # ------------------------------------------------------------------------------------
    if [ "$absent" -gt 0 ]; then
        warn "  $absent mapped arm(s) are ABSENT FROM THE TRACE ENTIRELY:"
        printf '%s\n' "${absent_list[@]}" | sed 's/^/[run_coverage]      /' >&2
        warn "  An arm that is not in the trace has either been DELETED from the source or has"
        warn "  MOVED, and both matter: a deleted branch would otherwise make this gate greener"
        warn "  rather than redder, which is why absence fails instead of being ignored."
        warn "  Check the source lines quoted in BRANCH_MANIFEST for each id above, then either"
        warn "  re-derive the coordinates or remove the entry with a reason."
        BRANCH_GATE_FAILURES=$((BRANCH_GATE_FAILURES + absent))
    fi

    # ------------------------------------------------------------------------------------
    # FAILURE CONDITION (a): a mapped arm present and never driven.
    # ------------------------------------------------------------------------------------
    if [ "$zero_taken" -gt 0 ]; then
        warn "  $zero_taken mapped arm(s) were NEVER TAKEN:"
        printf '%s\n' "${zero_list[@]}" | sed 's/^/[run_coverage]      /' >&2
        warn "  Each names the invocation or test that is supposed to drive it.  Where that"
        warn "  invocation was DEFERRED on this host, the arm is unmeasured for that reason and"
        warn "  the deferral is the finding.  Where it RAN, the arm is a genuine gap and the"
        warn "  answer is a test -- never a smaller manifest."
        BRANCH_GATE_FAILURES=$((BRANCH_GATE_FAILURES + zero_taken))
    fi

    if [ "$BRANCH_GATE_FAILURES" -eq 0 ] && [ "$unpopulated" -eq 0 ]; then
        log "  every one of the $required required arm(s) is present and taken at least once"
    fi
    log "  checked: $covered taken, $zero_taken never taken, $absent absent, $unpopulated unchecked"
    return 0
}

apply_gate() {
    local failures=0 rc=0

    rule
    log "applying the >= ${COVERAGE_MIN}% line-coverage gate to the aggregate (lcov --fail-under-lines)"
    lcov_run --summary "$LINE_GATE_TRACE" \
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
           $LCOV_BIN --summary '$LINE_GATE_TRACE' --fail-under-lines $COVERAGE_MIN --rc branch_coverage=1
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
        # A MISSING TEST AND A DEFERRED INVOCATION ARE DIFFERENT DIAGNOSES, and telling
        # someone to write tests that already exist wastes exactly the reader whose time this
        # script is meant to save.
        #
        # THE VERDICT DOES NOT CHANGE.  Below the bar still fails, and a deferral is never a
        # licence to pass: the code really is unmeasured, whatever the reason.  What changes is
        # only the ADVICE, because on a host that could not run the AIDL-selected invocations
        # the AIDL back-end's source is unmeasured BY CONSTRUCTION -- the cases that cover it
        # are written, are in the binary, and were skipped for want of a binder driver.  Adding
        # tests would not move those figures by a line.
        if [ -n "$INVOCATIONS_DEFERRED" ]; then
            warn "READ THE DEFERRALS BEFORE READING THESE FIGURES.  Invocation(s)"
            warn "  $INVOCATIONS_DEFERRED did not run on this host, so any target reached only by"
            warn "  them is unmeasured HERE for that reason and not for want of a test.  The AIDL"
            warn "  back-end and the selection helper are covered by cases that exist, are"
            warn "  compiled into the binaries and were skipped -- so these numbers are a"
            warn "  property of this HOST, not of the test set."
            warn "  This still FAILS, and deliberately: unmeasured is unmeasured whatever the"
            warn "  cause, and a run that excused itself here would be the one place a real gap"
            warn "  could hide. Re-run the whole matrix on a binder-capable host before treating"
            warn "  any figure above as this suite's coverage."
            warn "  Do NOT respond to this by lowering the bar, by exempting a file, or by"
            warn "  adding an exclusion glob. Nothing about a deferred invocation is fixed by"
            warn "  changing what the gate looks at."
        else
            warn "Close each gap by ADDING TESTS. Never by adding an exclusion glob, never by"
            warn "editing production source, and never by lowering the bar: the threshold is a"
            warn "gate, not a filter. Where a line genuinely cannot be reached from a test-only"
            warn "change, record it with its reason in the traceability report instead."
        fi
        if [ "$COVERAGE_PER_FILE_GATE" -eq 1 ]; then
            warn "per-file gating is IN FORCE (the default), so the above fails this run."
            failures=$((failures + 1))
        else
            warn "per-file gating has been turned OFF for this run (--no-per-file-gate or"
            warn "  COVERAGE_PER_FILE_GATE=0), so the above is reported but does not fail it."
            warn "  That is a DIAGNOSTIC setting: Directive 4 states the bar per target, so an"
            warn "  acceptance run leaves the per-file half on. Re-run without the override."
        fi
    elif [ -z "$BELOW_BAR_FILES" ]; then
        log "every target in the filtered trace meets the ${COVERAGE_MIN}% bar"
    else
        log "every gated target meets the ${COVERAGE_MIN}% bar; the exempt listing above is the"
        log "  complete set of files below it, and each carries its reason in this script."
    fi

    # THE BRANCH-ARM GATE'S VERDICT IS FOLDED IN HERE RATHER THAN ISSUED SEPARATELY, so that
    # this function stays the one place a verdict is decided and the script keeps one exit
    # status a caller can branch on.  It is counted AFTER the line gate, which is the required
    # ordering: coverage is subordinate to functional passes, and within coverage the line bar
    # is the acceptance criterion while the named-arm check is the evidence that the negative
    # and corner-case tests actually landed.
    if [ "$BRANCH_GATE_FAILURES" -ne 0 ]; then
        warn "the branch-arm gate reported $BRANCH_GATE_FAILURES failing arm(s) (listed above)."
        failures=$((failures + 1))
    fi

    rule
    if [ "$failures" -ne 0 ]; then
        local artifacts="$LINE_GATE_TRACE
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
#   [zero ONCE + run the whole invocation matrix + verify EACH invocation] -> capture ONCE
#   -> filter -> report HTML -> summarise -> per-file table -> gate.
#
# THE SUITE RESULTS COME BEFORE ANY COVERAGE NUMBER, and that ordering is a requirement
# rather than an accident of layout: a coverage figure never substitutes for a passing
# suite.  Every invocation is run and held to its three checks BEFORE a single counter is
# captured, and the first one that fails ends the run with nothing captured at all -- so
# there is no arrangement of these steps in which a coverage report can be produced from a
# red or partial matrix.
# The private HOME is created before the FIRST lcov invocation of the run, which is the
# version banner, not the capture: lcov reads a home configuration on every invocation and one
# it cannot parse breaks all of them.
# --restore is a standalone action and short-circuits everything else, so it can never be
# mistaken for part of a measurement run.
# ------------------------------------------------------------------------------------
main() {
    # THE REFERENCE AUDIT RUNS FIRST, before parsing, before validation and before any
    # filesystem write.  A script that cannot resolve one of its own function or variable
    # names must say so and stop, not discover it mid-run after the banner has printed.
    reference_audit

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

    # Provenance is written BEFORE the report, because it also augments the genhtml title -
    # the mechanism that puts the revision on every page rather than in one detachable file.
    write_provenance

    capture_coverage
    filter_coverage
    filter_dependency_headers
    generate_html
    summarise_coverage
    per_file_report
    # The branch-arm gate runs BEFORE apply_gate and records rather than exits, so that
    # apply_gate remains the single place a verdict is issued.  By this point every invocation
    # has already run and passed its three checks -- a red matrix never reaches either gate.
    apply_branch_gate
    apply_gate
}

main "$@"
