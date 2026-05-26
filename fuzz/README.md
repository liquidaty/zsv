# `libzsv` fuzzing

This directory contains the fuzzing support for `libzsv`.

## AFL/AFL++ fuzzing

The AFL++ workflow uses a local build of `zsvfuzz`, a filtered/minimized seed
corpus, and AFL++ itself.

- Build and run a single AFL++ instance: `make run`
- Run AFL++ in parallel: `make run-parallel JOBS=N`
- Build an ASan/UBSan test binary: `make test-build`
- Run the test binary against the raw corpus: `make test-run`

For a complete list of AFL++-related commands, use: `make help`

## `libFuzzer`-based fuzzing (Dockerized)

A separate dockerized setup is provided for libFuzzer-based fuzzing.

- Start the dockerized fuzzer: `make run-dockerized-fuzzer JOBS=N`
- Stop the containers: `make stop-dockerized-fuzzer`
- View logs: `make show-dockerized-fuzzer-logs`
- Tail logs: `make watch-dockerized-fuzzer-logs`
- List crashes: `make list-dockerized-fuzzer-crashes`

For the full command list, use: `make help-dockerized-fuzzer`

## Requirements

- AFL++: `afl-clang-fast`, `afl-fuzz`, `afl-cmin`, `afl-tmin`
- Clang with ASan/UBSan: `make test-build` | `make test-run`
- Docker + Docker Compose + `yq`: dockerized `libFuzzer` workflow

## Notes

- The AFL++ corpus is generated automatically from `../data/test` when needed.
- The dockerized setup clones <https://github.com/matteoalba/fuzz-zsv> repo as a
  helper environment.
