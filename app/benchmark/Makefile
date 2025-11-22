# Makefile for use with GNU make

CONFIGFILE ?= ../../config.mk
$(info # Using config file ${CONFIGFILE})
include ${CONFIGFILE}

PULL=

COUNT=count
SELECT=select
ifeq ($(PULL),1)
  COUNT=count-pull
  SELECT=select-pull
endif

CC ?= cc

WIN=
ifeq ($(WIN),)
  WIN=0
  ifneq ($(findstring w64,$(CC)),) # e.g. mingw64
    WIN=1
  endif
endif
ifeq ($(DEBUG),1)
  REL=dbg
else
  REL=rel
endif

ifeq ($(WIN),0)
  BUILD_SUBDIR=$(shell uname)/${REL}
else
  BUILD_SUBDIR=win/${REL}
endif

CCBN=$(shell basename ${CC})
ZSVBIN=../../build/${BUILD_SUBDIR}/${CCBN}/bin/zsv_

MAKE_BIN=$(notdir ${MAKE})

TSV_UTILS=~/install/tsv-utils/bin

HAVE_DUCKDB=$(shell command -v duckdb >/dev/null && echo 1 || echo 0)
HAVE_XSV=$(shell command -v xsv >/dev/null && echo 1 || echo 0)
HAVE_C2TSV=$(shell command -v c2tsv >/dev/null && echo 1 || echo 0)
HAVE_TSVUTILS=$(shell [ -f $(TSV_UTILS)/tsv-select ] && echo 1 || echo 0)
HAVE_POLARS=$(shell [ -f venv/bin/activate ] && echo 1 || echo 0)

help:
	@echo "To run:"
	@echo "    ${MAKE_BIN} all [PULL=1] [TSV_UTILS=~/install/tsv-utils/bin]"
	@echo "    ${MAKE_BIN} CLI"
	@echo "    ${MAKE_BIN} clean"

CLI: ZSVBIN="zsv "

CLI:
	${MAKE} all ZSVBIN=${ZSVBIN} -n | sed 's/[.]*\/.*\/bin\/zsv_/zsv /g' | sh

all: count select tsv

worldcitiespop_mil.csv:
	curl -LO 'https://burntsushi.net/stuff/worldcitiespop_mil.csv'

worldcitiespop.csv:
	curl -LO 'https://burntsushi.net/stuff/worldcitiespop.csv'

worldcitiespop_big.csv: worldcitiespop.csv
	cat worldcitiespop.csv worldcitiespop.csv worldcitiespop.csv > $@.tmp
	mv $@.tmp $@

worldcitiespop_big.tsv worldcitiespop_mil.tsv: %.tsv : %.csv
	${ZSVBIN}2tsv $< > $@.tmp
	mv $@.tmp $@

count: count-worldcitiespop_mil count-worldcitiespop_big

count-worldcitiespop_mil count-worldcitiespop_big: count-% : %.csv %.tsv
	@echo "-----------------------------------------------"
	@echo "  Benchmarking count: $*"
	@echo "-----------------------------------------------"
	@echo "${ZSVBIN}"${COUNT}

	@printf "zsv                  : "
	@(time ${ZSVBIN}${COUNT} < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(time ${ZSVBIN}${COUNT} < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(time ${ZSVBIN}${COUNT} < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "zsv --parallel       : "
	@(time ${ZSVBIN}${COUNT} --parallel $< > /dev/null) 2>&1 | xargs
	@printf "zsv --parallel       : "
	@(time ${ZSVBIN}${COUNT} --parallel $< > /dev/null) 2>&1 | xargs
	@printf "zsv --parallel       : "
	@(time ${ZSVBIN}${COUNT} --parallel $< > /dev/null) 2>&1 | xargs
	@echo ""

ifneq ($(HAVE_XSV),1)
	@echo "xsv not found: skipping (run \'brew install xsv\' to install)"
else
	@printf "xsv                  : "
	@(time xsv count < $< > /dev/null) 2>&1 | xargs
	@printf "xsv                  : "
	@(time xsv count < $< > /dev/null) 2>&1 | xargs
	@printf "xsv                  : "
	@(time xsv count < $< > /dev/null) 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_POLARS),1)
	@echo "polars not found: skipping (run \`make polars-setup\` to set up)"
else
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (time python3 -c 'import polars as pl; print(pl.scan_csv("$<").select(pl.len()).collect().item())' > /dev/null) 2>&1 | xargs
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (time python3 -c 'import polars as pl; print(pl.scan_csv("$<").select(pl.len()).collect().item())' > /dev/null) 2>&1 | xargs
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (time python3 -c 'import polars as pl; print(pl.scan_csv("$<").select(pl.len()).collect().item())' > /dev/null) 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_DUCKDB),1)
	@echo "duckdb not found: skipping"
else
	@printf "duckdb               : "
	@(time duckdb -c "SELECT count(*) FROM '$<'" > /dev/null) 2>&1 | xargs
	@printf "duckdb               : "
	@(time duckdb -c "SELECT count(*) FROM '$<'" > /dev/null) 2>&1 | xargs
	@printf "duckdb               : "
	@(time duckdb -c "SELECT count(*) FROM '$<'" > /dev/null) 2>&1 | xargs
	@echo ""
endif

	@printf "tsv-utils            : "
	@(time ~/install/tsv-utils/bin/number-lines -d, < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils            : "
	@(time ~/install/tsv-utils/bin/number-lines -d, < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils            : "
	@(time ~/install/tsv-utils/bin/number-lines -d, < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "tsv-utils (tsv input): "
	@(time ~/install/tsv-utils/bin/number-lines < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (tsv input): "
	@(time ~/install/tsv-utils/bin/number-lines < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (tsv input): "
	@(time ~/install/tsv-utils/bin/number-lines < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@echo ""

tsv: worldcitiespop_mil.csv
	@echo "${ZSVBIN}"2tsv

	@printf "zsv                  : "
	@(time ${ZSVBIN}2tsv < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(time ${ZSVBIN}2tsv < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(time ${ZSVBIN}2tsv < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "tsv-utils (csv2tsv)  : "
	@(time ~/install/tsv-utils/bin/csv2tsv < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (csv2tsv)  : "
	@(time ~/install/tsv-utils/bin/csv2tsv < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (csv2tsv)  : "
	@(time ~/install/tsv-utils/bin/csv2tsv < $< > /dev/null) 2>&1 | xargs
	@echo ""

ifneq ($(HAVE_C2TSV),1)
	@echo "c2tsv not found-- skipping"
else
	@printf "c-blake/nio/c2tsv    : " # from https://github.com/c-blake/nio
	@(time c2tsv < $< > /dev/null)  2>&1 | xargs
	@printf "c-blake/nio/c2tsv    : "
	@(time c2tsv < $< > /dev/null)  2>&1 | xargs
	@printf "c-blake/nio/c2tsv    : "
	@(time c2tsv < $< > /dev/null)  2>&1 | xargs
endif
	@echo ""

select: select-worldcitiespop_mil select-worldcitiespop_big

select-worldcitiespop_mil select-worldcitiespop_big: select-% : %.csv %.tsv
	@echo "-----------------------------------------------"
	@echo "  Benchmarking select: $*"
	@echo "-----------------------------------------------"

	@printf "zsv                  : "
	@(time ${ZSVBIN}${SELECT} -W -n -- 2 1 3-7 < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(time ${ZSVBIN}${SELECT} -W -n -- 2 1 3-7 < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(time ${ZSVBIN}${SELECT} -W -n -- 2 1 3-7 < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "zsv --parallel       : "
	@(time ${ZSVBIN}${SELECT} --parallel -W -n $< -- 2 1 3-7 > /dev/null) 2>&1 | xargs
	@printf "zsv --parallel       : "
	@(time ${ZSVBIN}${SELECT} --parallel -W -n $< -- 2 1 3-7 > /dev/null) 2>&1 | xargs
	@printf "zsv --parallel       : "
	@(time ${ZSVBIN}${SELECT} --parallel -W -n $< -- 2 1 3-7 > /dev/null) 2>&1 | xargs
	@echo ""

ifneq ($(HAVE_XSV),1)
	@echo "xsv not found: skipping (run \'brew install xsv\' to install)"
else
	@printf "xsv                  : "
	@(time xsv select 2,1,3-7 < $< > /dev/null) 2>&1 | xargs
	@printf "xsv                  : "
	@(time xsv select 2,1,3-7 < $< > /dev/null) 2>&1 | xargs
	@printf "xsv                  : "
	@(time xsv select 2,1,3-7 < $< > /dev/null) 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_POLARS),1)
	@echo "polars not found: skipping (run \`make polars-setup\` to set up)"
else
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (time python3 -c 'import polars as pl; pl.scan_csv("$<", infer_schema=False).select(pl.nth(1, 0, 2, 3, 4, 5, 6)).sink_csv("/dev/null")') 2>&1 | xargs
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (time python3 -c 'import polars as pl; pl.scan_csv("$<", infer_schema=False).select(pl.nth(1, 0, 2, 3, 4, 5, 6)).sink_csv("/dev/null")') 2>&1 | xargs
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (time python3 -c 'import polars as pl; pl.scan_csv("$<", infer_schema=False).select(pl.nth(1, 0, 2, 3, 4, 5, 6)).sink_csv("/dev/null")') 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_DUCKDB),1)
	@echo "duckdb not found: skipping"
else
	@printf "duckdb               : "
	@(time duckdb -c "COPY (from read_csv('$<', all_varchar=true) select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null'") 2>&1 | xargs
	@printf "duckdb               : "
	@(time duckdb -c "COPY (from read_csv('$<', all_varchar=true) select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null'") 2>&1 | xargs
	@printf "duckdb               : "
	@(time duckdb -c "COPY (from read_csv('$<', all_varchar=true) select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null'") 2>&1 | xargs
	@echo ""

	@printf "duckdb (NO_QUOTE)    : "
	@(time duckdb -c "COPY (from read_csv('$<', all_varchar=true, quote='') select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null' (quote '')") 2>&1 | xargs
	@printf "duckdb (NO_QUOTE)    : "
	@(time duckdb -c "COPY (from read_csv('$<', all_varchar=true, quote='') select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null' (quote '')") 2>&1 | xargs
	@printf "duckdb (NO_QUOTE)    : "
	@(time duckdb -c "COPY (from read_csv('$<', all_varchar=true, quote='') select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null' (quote '')") 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_TSVUTILS),1)
	@echo "tsv-utils not found: skipping"
else
	@printf "tsv-utils            : "
	@(time ${TSV_UTILS}/tsv-select -d, -f 1-7 < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils            : "
	@(time ${TSV_UTILS}/tsv-select -d, -f 1-7 < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils            : "
	@(time ${TSV_UTILS}/tsv-select -d, -f 1-7 < $< > /dev/null) 2>&1 | xargs
endif
	@echo ""

	@printf "tsv-utils (tsv input): "
	@(time ${TSV_UTILS}/tsv-select -f 1-7 < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (tsv input): "
	@(time ${TSV_UTILS}/tsv-select -f 1-7 < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (tsv input): "
	@(time ${TSV_UTILS}/tsv-select -f 1-7 < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "csvcut               : "
	@(time csvcut -c 2,1,3-7 < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "miller (mlr)         : "
	@(time mlr --csv cut -o -f City,Country,AccentCity,Region,Population,Latitude,Longitude $< > /dev/null) 2>&1 | xargs
	@printf "miller (mlr)         : "
	@(time mlr --csv cut -o -f City,Country,AccentCity,Region,Population,Latitude,Longitude $< > /dev/null) 2>&1 | xargs
	@echo ""

clean:
	rm -f worldcitiespop_big.csv worldcitiespop_big.tsv worldcitiespop_mil.csv worldcitiespop_mil.tsv worldcitiespop.csv

.PHONY: help all count select select-% clean polars-setup


##### python / polars

ifeq ($(OS),Windows_NT)
VENV_ACTIVATE=venv/Scripts/activate
else
VENV_ACTIVATE=venv/bin/activate
endif

polars-setup: venv/setup-done

venv/setup-done:
	test -d venv || virtualenv venv
	. ${VENV_ACTIVATE}; pip install polars
	touch $@

