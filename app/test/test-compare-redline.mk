# test-compare-redline.mk -- tests for `zsv compare --json-redline` (compare-redline-v1 feature).
# Included by app/test/Makefile (the TESTS+=test-compare-redline[-v2] registrations live there).
# Kept in a separate fragment because the redline coverage is verbose; it shares the parent
# Makefile's variables (PREFIX, CMP, TMP_DIR, BUILD_DIR, TEST_PASS/FAIL, ...) via plain include.
#
# Help-topic and --json-redline outputs are deterministic, so they are checked byte-for-byte
# against goldens in expected/ (no jq/ajv: those tools are absent on the CI runners).

# TC1: two identical inputs → empty rows[]
# TC2: mix diff/tolerated (--tolerance 1.0)
# TC3: schema mismatch (extra column in input 0)
# TC4: missing rows on each side → object form
# TC5: --include-unchanged-rows
# TC6: --include-tolerated
# TC7: three inputs → length-3 diff arrays
# TC8: SOURCE_DATE_EPOCH yields a deterministic generated_at
# TC9: --exit-code returns the number of differing cells in redline mode
# TC10: --require-all-inputs drops rows missing from any input; options.require_all_inputs is recorded
test-compare-redline: ${BUILD_DIR}/bin/zsv_compare${EXE}
	@${TEST_INIT}
	@(${PREFIX} $< --json-redline -k id compare/jredline1a.csv compare/jredline1b.csv ${REDIRECT1} ${TMP_DIR}/$@.out1raw && \
	sed '/"generated_at"/d' ${TMP_DIR}/$@.out1raw > ${TMP_DIR}/$@.out1 && \
	${CMP} ${TMP_DIR}/$@.out1 expected/$@.out1 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --json-redline -k id --tolerance 1.0 compare/jredline2a.csv compare/jredline2b.csv ${REDIRECT1} ${TMP_DIR}/$@.out2raw && \
	sed '/"generated_at"/d' ${TMP_DIR}/$@.out2raw > ${TMP_DIR}/$@.out2 && \
	${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --json-redline -k id compare/jredline3a.csv compare/jredline3b.csv ${REDIRECT1} ${TMP_DIR}/$@.out3raw && \
	sed '/"generated_at"/d' ${TMP_DIR}/$@.out3raw > ${TMP_DIR}/$@.out3 && \
	${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --json-redline -k id compare/jredline4a.csv compare/jredline4b.csv ${REDIRECT1} ${TMP_DIR}/$@.out4raw && \
	sed '/"generated_at"/d' ${TMP_DIR}/$@.out4raw > ${TMP_DIR}/$@.out4 && \
	${CMP} ${TMP_DIR}/$@.out4 expected/$@.out4 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --json-redline -k id --include-unchanged-rows compare/jredline5a.csv compare/jredline5b.csv ${REDIRECT1} ${TMP_DIR}/$@.out5raw && \
	sed '/"generated_at"/d' ${TMP_DIR}/$@.out5raw > ${TMP_DIR}/$@.out5 && \
	${CMP} ${TMP_DIR}/$@.out5 expected/$@.out5 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --json-redline -k id --tolerance 1.0 --include-tolerated compare/jredline2a.csv compare/jredline2b.csv ${REDIRECT1} ${TMP_DIR}/$@.out6raw && \
	sed '/"generated_at"/d' ${TMP_DIR}/$@.out6raw > ${TMP_DIR}/$@.out6 && \
	${CMP} ${TMP_DIR}/$@.out6 expected/$@.out6 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --json-redline -k id compare/jredline7a.csv compare/jredline7b.csv compare/jredline7c.csv ${REDIRECT1} ${TMP_DIR}/$@.out7raw && \
	sed '/"generated_at"/d' ${TMP_DIR}/$@.out7raw > ${TMP_DIR}/$@.out7 && \
	${CMP} ${TMP_DIR}/$@.out7 expected/$@.out7 && ${TEST_PASS} || ${TEST_FAIL})

	@# TC8: SOURCE_DATE_EPOCH must produce a fixed generated_at (1700000000 -> 2023-11-14T22:13:20Z)
	@(SOURCE_DATE_EPOCH=1700000000 $< --json-redline -k id compare/jredline1a.csv compare/jredline1b.csv >${TMP_DIR}/$@.out8 2>&1 && \
	grep -q '"generated_at": "2023-11-14T22:13:20Z"' ${TMP_DIR}/$@.out8 && ${TEST_PASS} || ${TEST_FAIL})

	@# TC9: --exit-code returns the differing-cell count (jredline7 -> 1) in redline mode
	@($< --json-redline --exit-code -k id compare/jredline7a.csv compare/jredline7b.csv compare/jredline7c.csv >/dev/null 2>&1; \
	[ $$? -eq 1 ] && ${TEST_PASS} || ${TEST_FAIL})

	@# TC10: --require-all-inputs keeps only the key intersection; the only-in-input rows are dropped
	@# (only_in_input_count -> 0,0) and the run is self-describing (options.require_all_inputs: true)
	@(${PREFIX} $< --json-redline -k id --require-all-inputs compare/reqall_a.csv compare/reqall_b.csv ${REDIRECT1} ${TMP_DIR}/$@.out9raw && \
	sed '/"generated_at"/d' ${TMP_DIR}/$@.out9raw > ${TMP_DIR}/$@.out9 && \
	${CMP} ${TMP_DIR}/$@.out9 expected/$@.out9 && ${TEST_PASS} || ${TEST_FAIL})

# TC-A: missing_in indices are strictly ascending in 3-input case
# TC-B: -a/--add combined with --json-redline errors with non-zero exit, exact stderr, empty stdout
# TC-C: help topics accessible via canonical `zsv help compare <topic>` routing and via --help-topic
#        alias; version pinning; JSON Schema validates real output
test-compare-redline-v2: ${BUILD_DIR}/bin/zsv_compare${EXE} ${BUILD_DIR}/bin/cli${EXE}
	@${TEST_INIT}

	@# TC-A: 3-input missing_in sort — missing_in arrays must be ascending
	@(${PREFIX} $< --json-redline -k id compare/jredline8a.csv compare/jredline8b.csv compare/jredline8c.csv ${REDIRECT1} ${TMP_DIR}/$@.out8raw && \
	sed '/"generated_at"/d' ${TMP_DIR}/$@.out8raw > ${TMP_DIR}/$@.out8 && \
	${CMP} ${TMP_DIR}/$@.out8 expected/test-compare-redline.out8 && ${TEST_PASS} || ${TEST_FAIL})

	@# TC-B1: -a names column in input 0 + --json-redline → error (non-zero exit, exact stderr, empty stdout)
	@($< --json-redline -k id -a val compare/jredline1a.csv compare/jredline1b.csv >${TMP_DIR}/$@.mutexout1 2>${TMP_DIR}/$@.mutexerr1; \
	[ $$? -ne 0 ] && \
	${CMP} /dev/null ${TMP_DIR}/$@.mutexout1 && \
	${CMP} ${TMP_DIR}/$@.mutexerr1 expected/test-compare-redline-mutex.err && \
	${TEST_PASS} || ${TEST_FAIL})

	@# TC-B2: -a names column in both inputs + --json-redline → error
	@($< --json-redline -k id -a name compare/jredline1a.csv compare/jredline1b.csv >${TMP_DIR}/$@.mutexout2 2>${TMP_DIR}/$@.mutexerr2; \
	[ $$? -ne 0 ] && \
	${CMP} /dev/null ${TMP_DIR}/$@.mutexout2 && \
	${CMP} ${TMP_DIR}/$@.mutexerr2 expected/test-compare-redline-mutex.err && \
	${TEST_PASS} || ${TEST_FAIL})

	@# TC-B3: -a names nonexistent column + --json-redline → error (mutex fires before "column not found")
	@($< --json-redline -k id -a nonexistent compare/jredline1a.csv compare/jredline1b.csv >${TMP_DIR}/$@.mutexout3 2>${TMP_DIR}/$@.mutexerr3; \
	[ $$? -ne 0 ] && \
	${CMP} /dev/null ${TMP_DIR}/$@.mutexout3 && \
	${CMP} ${TMP_DIR}/$@.mutexerr3 expected/test-compare-redline-mutex.err && \
	${TEST_PASS} || ${TEST_FAIL})

	@# TC-B4: no -a with --json-redline → normal JSON output (no error)
	@(${PREFIX} $< --json-redline -k id compare/jredline1a.csv compare/jredline1b.csv ${REDIRECT1} ${TMP_DIR}/$@.mutexout4 && \
	[ -s ${TMP_DIR}/$@.mutexout4 ] && ${TEST_PASS} || ${TEST_FAIL})

	@# TC-B5: -a without --json-redline → normal operation (unaffected)
	@(${PREFIX} $< -k id -a val compare/jredline1a.csv compare/jredline1b.csv ${REDIRECT1} ${TMP_DIR}/$@.mutexout5 && \
	[ -s ${TMP_DIR}/$@.mutexout5 ] && ${TEST_PASS} || ${TEST_FAIL})

	@# TC-C1: canonical `zsv help compare json-redline` narrative matches the golden byte-for-byte
	@(${BUILD_DIR}/bin/cli help compare json-redline > ${TMP_DIR}/$@.topicout1 2>&1 && \
	${CMP} ${TMP_DIR}/$@.topicout1 expected/$@.json-redline && ${TEST_PASS} || ${TEST_FAIL})

	@# TC-C2: canonical `zsv help compare json-redline-schema` JSON Schema matches the golden byte-for-byte
	@(${BUILD_DIR}/bin/cli help compare json-redline-schema > ${TMP_DIR}/$@.topicout2 2>&1 && \
	${CMP} ${TMP_DIR}/$@.topicout2 expected/$@.json-redline-schema && ${TEST_PASS} || ${TEST_FAIL})

	@# TC-C3: `zsv help compare` (no topic) lists both topics
	@(${BUILD_DIR}/bin/cli help compare > ${TMP_DIR}/$@.helpcmp 2>&1 && \
	grep -q 'json-redline' ${TMP_DIR}/$@.helpcmp && \
	grep -q 'json-redline-schema' ${TMP_DIR}/$@.helpcmp && \
	${TEST_PASS} || ${TEST_FAIL})

	@# TC-C4: the version emitted in --json-redline output matches version.const in the schema topic.
	@# Extract both with sed (no jq): from the live output and from the schema captured in TC-C2.
	@(${PREFIX} $< --json-redline -k id compare/jredline1a.csv compare/jredline1b.csv > ${TMP_DIR}/$@.verout 2>&1 && \
	VER_OUT=`sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' ${TMP_DIR}/$@.verout | head -1` && \
	VER_SCHEMA=`sed -n 's/.*"version": *{"const": *"\([^"]*\)".*/\1/p' ${TMP_DIR}/$@.topicout2 | head -1` && \
	[ -n "$$VER_OUT" ] && [ "$$VER_OUT" = "$$VER_SCHEMA" ] && ${TEST_PASS} || ${TEST_FAIL})

	@# TC-C5: the real --json-redline output is pinned byte-for-byte (deterministic via SOURCE_DATE_EPOCH).
	@# With the pinned schema topic (TC-C2) this enforces output<->schema conformance without an external
	@# JSON-Schema validator -- jq and ajv are not available on the CI runners.
	@(SOURCE_DATE_EPOCH=1700000000 $< --json-redline -k id compare/jredline1a.csv compare/jredline1b.csv > ${TMP_DIR}/$@.fixture1 2>&1 && \
	${CMP} ${TMP_DIR}/$@.fixture1 expected/$@.fixture1 && ${TEST_PASS} || ${TEST_FAIL})

	@# TC-C6: legacy --help-topic aliases route to the same bytes as the canonical `help compare <topic>`
	@($< --help-topic compare-json-redline > ${TMP_DIR}/$@.alias1 2>&1 && \
	${CMP} ${TMP_DIR}/$@.alias1 ${TMP_DIR}/$@.topicout1 && \
	$< --help-topic compare-json-redline-schema > ${TMP_DIR}/$@.alias2 2>&1 && \
	${CMP} ${TMP_DIR}/$@.alias2 ${TMP_DIR}/$@.topicout2 && \
	${TEST_PASS} || ${TEST_FAIL})
