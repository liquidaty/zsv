#!/bin/bash -eu
# OSS-Fuzz build script. Builds the libFuzzer harness for zsv_parse_bytes
# and packages a seed corpus from the in-tree PoC payloads.
#
# Variables provided by OSS-Fuzz: $SRC, $WORK, $OUT, $CC, $CFLAGS,
# $LIB_FUZZING_ENGINE, $SANITIZER.

cd "$SRC/zsv"

# include/zsv.h is .gitignored — generated from include/zsv.h.in by
# `make -C src build`. We don't want to run the full library build (drags
# in jq / ncurses); use the same sed substitution the Makefile uses (the
# no-ZSV_EXTRAS branch, matching our build flags).
sed 's/__ZSV_EXTRAS__DEFINE__//' < include/zsv.h.in > include/zsv.h

# Compile the parser + harness as a single TU, no external dependencies.
# The parser's unity translation unit (src/zsv.c) #includes everything it
# needs, so we don't need to run ./configure or build libzsv.a first.
$CC $CFLAGS \
    -DZSV_VERSION=\"oss-fuzz\" \
    -DHAVE_MEMMEM \
    -DHAVE___BUILTIN_EXPECT \
    -DZSV_HAVE_NEON=0 \
    -fsigned-char \
    -I include -I src -I app/external/sqlite3 \
    -c src/zsv.c -o "$WORK/zsv.o"

$CC $CFLAGS $LIB_FUZZING_ENGINE \
    -I include \
    app/test/sec/libfuzzer.c "$WORK/zsv.o" \
    -o "$OUT/zsv_parse_bytes_fuzzer"

# Seed corpus from the PoC payloads checked into the tree.
zip -j "$OUT/zsv_parse_bytes_fuzzer_seed_corpus.zip" \
    app/test/sec/poc/*.bin
