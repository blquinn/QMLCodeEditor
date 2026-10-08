# Benchmark history

Recorded runs of the tracked benchmark suites (`benchmarks/tracked.json`), one directory per run:
`<date>-<shorthash>[-N]/<suite>.json`. Each file is the normal harness JSON plus `meta.git.{commit,branch,dirty}`,
`meta.args` and an optional `meta.note`. Runs are only compared with runs from the same CPU and build type
(`meta.cpu`, `meta.optimized`), because absolute timings don't carry across machines.

This is tracking only: nothing here fails a build. PERF-05 will turn the comparison into a gate.

## Recording

```sh
cmake --preset release
cmake --build --preset release --target bench_record     # about 2.5 minutes; prints the comparison with the previous run
python3 tools/benchtrack.py record --build-dir build/release --note "after CORE-NN"   # same, with a note
```

Commit first so the stored hash is the code that was measured (the tool warns when the tree is dirty), use the
release preset, and keep the machine otherwise idle. Record after changes that touch rendering, the rope, the display
map or highlighting, and commit the new directory.

The frame suites run with `--quick` on the software backend, so they track CPU-side cost only. Real-display runs
(`bench_scroll` without `--quick`) stay manual and go to `benchmarks/results/`.

## Reading it

```sh
python3 tools/benchtrack.py list                      # recorded runs
python3 tools/benchtrack.py compare                   # latest vs the previous run on this machine
python3 tools/benchtrack.py compare A B [--all]       # two history dirs or result files
python3 tools/benchtrack.py history 'insert' --suite bench_core   # a case's trend across runs
```

A case is marked `slower`/`faster` when it moved more than its tolerance (20% for times, 30% for the frame suites,
10% for bytes) and, for times, by more than 2 us. Counts are never compared.
