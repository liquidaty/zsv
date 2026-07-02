### jsonwriter: small, fast and permissively-license JSON writer written in C


#### Why do we need this?

This library was designed to meet the following requirements:

* small
* fast
* memory-efficient
* supports full range of UTF8
* written in C, portable, and permissively licensed
* thread safe
* supports custom write functions and write targets (e.g. write to network or pipe
or to custom buffer using some custom write function)

The last item was the most difficult to find, but was indispensible for versatile
reusability (for example, maybe we will not know, until dynamically at runtime, whether
we want to write our JSON to stdout, stderr, another file, a network pipe, or just
feed that back into some other process in a chain of data processing.

#### How to use

##### create a handle
Write either to a file, or provide your own `fwrite`-like function and target:

```
jsonwriter_handle jsonwriter_new_file(FILE *f);
jsonwriter_handle jsonwriter_new(
    size_t (*write)(const void *, size_t, size_t, void *),
    void *write_arg
);
```

e.g. `jsonwriter_handle h = jsonwriter_new(fwrite, stdout)`

##### (optional) override the maximum nesting depth
By default a writer allows up to `JSONWRITER_MAX_NESTING` (256) levels of nested
arrays/objects. To raise or lower this per writer, call — before opening any
container (i.e. at depth 0):

```
enum jsonwriter_status jsonwriter_set_max_nesting(jsonwriter_handle h, unsigned int max_nesting);
```

Returns `jsonwriter_status_ok`, or `jsonwriter_status_invalid_value` (`max_nesting`
is 0 or too large), `jsonwriter_status_misconfiguration` (a container is already
open), or `jsonwriter_status_out_of_memory`. The compile-time default itself can
be changed with `-DJSONWRITER_MAX_NESTING=N`.

##### write your JSON
For example:
```
jsonwriter_start_object(h);
jsonwriter_object_key(h, "hello");
jsonwriter_str(h, "there!");
jsonwriter_end(h); // or alternatively, for more strict use, `jsonwriter_end_object(h)`
```

or, in a more concise form:
```
jsonwriter_start_object(h);
jsonwriter_object_str(h, "hello", "there!"); // similar macros can be used for other types
jsonwriter_end(h);

```

##### clean up

`jsonwriter_delete(h)`

### Testing and fuzzing

After `./configure`, the Makefile exposes:

```
make test               # build and run the unit test suite
make test ASAN=1        # same, under AddressSanitizer + UBSan (leaks too, on Linux)
make test-leaks         # run the suite under a leak checker (macOS leaks / valgrind)
make strict             # strict-warnings (-Werror) syntax check; per compiler via CC=
make fuzz-standalone    # portable fuzz replay driver (any compiler); replays argv files / stdin
make fuzz               # libFuzzer target (needs an LLVM clang; FUZZ_CC= to override)
```

`tests/test.c` exercises the full public API (objects/arrays, every value type,
escaping, compact/pretty output, end-mismatch and nesting-limit error paths, the
variant handler, `jsonwriter_unknown` and `jsonwriter_write_raw`) by writing into
an in-memory sink and asserting exact output.

`tests/fuzz.c` drives the writer with an opcode-program and runs an escaping
*oracle*: it serializes arbitrary input bytes as a JSON string and asserts the
result is always a well-formed JSON string token (no input can break out of its
quotes). Seed inputs live in `tests/corpus/`.

Every push and pull request runs all of the above across gcc/clang on Linux and
macOS, a mingw64 cross-compile, the sanitizer and strict lanes, and a short
libFuzzer session (see `.github/workflows/ci.yml`).

#### Fuzzing on macOS (including Apple Silicon)

Apple clang ships without the libFuzzer runtime, so `make fuzz` can't run on a
Mac directly. The `df-*` targets run the same libFuzzer harness inside a Linux
container (built natively for the host arch, so it runs at full speed on Apple
Silicon):

```
make df-run [FUZZ_SECONDS=N] [JOBS=N]   # build the image and fuzz
make df-list-crashes                    # list any crash/leak/oom inputs found
make df-repro CRASH_FILE=<file>         # replay one finding in the container
make df-clean                           # remove the image and findings
make df-help                            # full command list
```

Findings (the growing corpus and any crash inputs) persist on the host under
`tests/fuzz-findings/`. This uses a Docker bind mount, so the repo must live
under a path Docker Desktop shares (your home dir / anything under `/Users` by
default). Requires Docker; no other host tooling.

### Enjoy!
