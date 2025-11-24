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

HAVE_TIME_ALL=$(shell command -v /usr/bin/time >/dev/null && echo 1 || echo 0)

ifeq ($(TIME_ALL),)
  ifeq ($(HAVE_TIME_ALL),1)
    TIME_ALL=1
  endif
endif
ifeq ($(TIME_ALL),1)
  ALL_SUFFIX=_all
  ifeq ($(HAVE_TIME_ALL),1)
    TIMER=/usr/bin/time -l
  else
    $(error /usr/bin/time not found)
  endif
else
  TIMER=time
  ALL_SUFFIX=
endif
AWK_SCRIPT=./benchmark${ALL_SUFFIX}.awk

help:
	@echo "========================================================================"
	@echo "Running benchmarks"
	@echo "------------------------------------------------------------------------"
	@echo "Generating graphs requires gnuplot, gawk and zsv"
	@echo "    ${MAKE_BIN} graph  : run benchmarks and generate graphs"
	@echo "    ${MAKE_BIN} reset  : reset saved e.g. results e.g. csv"
	@echo "------------------------------------------------------------------------"
	@echo "If /usr/bin/time is available, it will be used with -l to get full metrics"
	@echo "To only run timing metrics, set TIME_ALL=0 e.g. \`${MAKE_BIN} graph TIME_ALL=0\`"
	@echo ""
	@echo "========================================================================"
	@echo "Other targets:"
	@echo "------------------------------------------------------------------------"
	@echo "    ${MAKE_BIN} clean  : remove all data, intermediate and result files"
	@echo "    ${MAKE_BIN} all [PULL=1] [TSV_UTILS=~/install/tsv-utils/bin]"
	@echo "    ${MAKE_BIN} csv    : same as all but saves to benchmark.csv"
	@echo "    ${MAKE_BIN} CLI    : use CLI instead of standalone command binaries"
	@echo ""
	@echo "make all does not actually run everything; you can also run:"
	@echo "    ${MAKE_BIN} tsv"
	@echo ""

CLI: ZSVBIN="zsv "

CLI:
	${MAKE} all ZSVBIN=${ZSVBIN} -n | sed 's/[.]*\/.*\/bin\/zsv_/zsv /g' | sh

all: count select

BENCHMARKS_CSV=benchmarks${ALL_SUFFIX}.csv
BENCHMARKS_TXT=benchmarks${ALL_SUFFIX}.txt
BENCHMARKS_AGG_CSV=benchmarks_agg${ALL_SUFFIX}.csv
BENCHMARKS_AGG_SQL=benchmarks_agg${ALL_SUFFIX}.sql

csv: ${BENCHMARKS_CSV}
	@echo "Results saved at $<. To re-run, first run \`make reset\`"

csv_average: benchmarks_average${ALL_SUFFIX}.csv
	@echo "Results saved at $<. To re-run, first run \`make reset\`"

${BENCHMARKS_AGG_CSV}: ${BENCHMARKS_CSV} ${BENCHMARKS_AGG_SQL}
	cat $< | grep -v tsv-utils | zsv sql "`cat ${BENCHMARKS_AGG_SQL}`" > $@.tmp
	mv $@.tmp $@

GRAPH_GP=plot_metrics${ALL_SUFFIX}
GRAPH_TIME_GP=plot_metrics_time${ALL_SUFFIX}
GRAPH_INPUT=/tmp/graph
HAVE_GNUPLOT=$(shell command -v gnuplot > /dev/null && echo 1 || echo 0)

graph: graph-select graph-count graph-combined

graph-count graph-select: graph-%: ${BENCHMARKS_AGG_CSV} ${GRAPH_GP}.gp.in ${GRAPH_TIME_GP}.gp.in
ifneq ($(HAVE_GNUPLOT),1)
	echo "Please install gnuplot" && exit 1
else
	cat ${GRAPH_GP}.gp.in | sed 's#INPUT_CSV#${GRAPH_INPUT}-$*.csv#g' \
	| sed 's/OPERATION/$*/g' > /tmp/${GRAPH_GP}-$*.gp
	zsv select --search '$*' $< | zsv select --search '_big' > ${GRAPH_INPUT}-$*.csv
	gnuplot /tmp/${GRAPH_GP}-$*.gp

	cat ${GRAPH_TIME_GP}.gp.in | sed 's#INPUT_CSV#${GRAPH_INPUT}-$*.csv#g' \
	| sed 's/OPERATION/$*/g' > /tmp/${GRAPH_TIME_GP}-$*.gp
	gnuplot /tmp/${GRAPH_TIME_GP}-$*.gp
	echo "Generated benchmark_$*_time.png and benchmark_$*_memory.png"
endif

GRAPH_GP_COMBINED=plot_combined${ALL_SUFFIX}.gp
graph-combined: ${BENCHMARKS_AGG_CSV} ${GRAPH_GP_COMBINED}.in
ifneq ($(HAVE_GNUPLOT),1)
	echo "Please install gnuplot" && exit 1
else
	cat ${GRAPH_GP_COMBINED}.in | sed 's#INPUT_CSV#${GRAPH_INPUT}-combined.csv#g' > /tmp/${GRAPH_GP_COMBINED}
	head -n1 $< > ${GRAPH_INPUT}-combined.csv
	grep 'select,worldcitiespop_big' $< >> ${GRAPH_INPUT}-combined.csv
	gnuplot /tmp/${GRAPH_GP_COMBINED}
endif

${BENCHMARKS_TXT}: worldcitiespop.csv
	${MAKE} -e all TIME_ALL=${TIME_ALL} | tee $@.tmp
	@mv $@.tmp $@

${BENCHMARKS_CSV}: ${BENCHMARKS_TXT}
	cat $< | ${AWK_SCRIPT} > $@.tmp
	@mv $@.tmp $@

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
	@(${TIMER} ${ZSVBIN}${COUNT} < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(${TIMER} ${ZSVBIN}${COUNT} < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(${TIMER} ${ZSVBIN}${COUNT} < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "zsv --parallel       : "
	@(${TIMER} ${ZSVBIN}${COUNT} --parallel $< > /dev/null) 2>&1 | xargs
	@printf "zsv --parallel       : "
	@(${TIMER} ${ZSVBIN}${COUNT} --parallel $< > /dev/null) 2>&1 | xargs
	@printf "zsv --parallel       : "
	@(${TIMER} ${ZSVBIN}${COUNT} --parallel $< > /dev/null) 2>&1 | xargs
	@echo ""

ifneq ($(HAVE_XSV),1)
	@echo "xsv not found: skipping (run \'brew install xsv\' to install)"
else
	@printf "xsv                  : "
	@(${TIMER} xsv count < $< > /dev/null) 2>&1 | xargs
	@printf "xsv                  : "
	@(${TIMER} xsv count < $< > /dev/null) 2>&1 | xargs
	@printf "xsv                  : "
	@(${TIMER} xsv count < $< > /dev/null) 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_POLARS),1)
	@echo "polars not found: skipping (run \`make polars-setup\` to set up)"
else
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (${TIMER} python3 -c 'import polars as pl; print(pl.scan_csv("$<").select(pl.len()).collect().item())' > /dev/null) 2>&1 | xargs
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (${TIMER} python3 -c 'import polars as pl; print(pl.scan_csv("$<").select(pl.len()).collect().item())' > /dev/null) 2>&1 | xargs
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (${TIMER} python3 -c 'import polars as pl; print(pl.scan_csv("$<").select(pl.len()).collect().item())' > /dev/null) 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_DUCKDB),1)
	@echo "duckdb not found: skipping"
else
	@printf "duckdb               : "
	@(${TIMER} duckdb -c "SELECT count(*) FROM '$<'" > /dev/null) 2>&1 | xargs
	@printf "duckdb               : "
	@(${TIMER} duckdb -c "SELECT count(*) FROM '$<'" > /dev/null) 2>&1 | xargs
	@printf "duckdb               : "
	@(${TIMER} duckdb -c "SELECT count(*) FROM '$<'" > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "duckdb (QUOTE='')    : "
	@(${TIMER} duckdb -c "SELECT count(*) FROM read_csv('$<',all_varchar=true,quote='')" > /dev/null) 2>&1 | xargs
	@printf "duckdb (QUOTE='')    : "
	@(${TIMER} duckdb -c "SELECT count(*) FROM read_csv('$<',all_varchar=true,quote='')" > /dev/null) 2>&1 | xargs
	@printf "duckdb (QUOTE='')    : "
	@(${TIMER} duckdb -c "SELECT count(*) FROM read_csv('$<',all_varchar=true,quote='')" > /dev/null) 2>&1 | xargs
	@echo ""

endif

	@printf "tsv-utils            : "
	@(${TIMER} ~/install/tsv-utils/bin/number-lines -d, < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils            : "
	@(${TIMER} ~/install/tsv-utils/bin/number-lines -d, < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils            : "
	@(${TIMER} ~/install/tsv-utils/bin/number-lines -d, < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "tsv-utils (tsv input): "
	@(${TIMER} ~/install/tsv-utils/bin/number-lines < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (tsv input): "
	@(${TIMER} ~/install/tsv-utils/bin/number-lines < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (tsv input): "
	@(${TIMER} ~/install/tsv-utils/bin/number-lines < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@echo ""

tsv: worldcitiespop_mil.csv
	@echo "${ZSVBIN}"2tsv

	@printf "zsv                  : "
	@(${TIMER} ${ZSVBIN}2tsv < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(${TIMER} ${ZSVBIN}2tsv < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(${TIMER} ${ZSVBIN}2tsv < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "tsv-utils (csv2tsv)  : "
	@(${TIMER} ~/install/tsv-utils/bin/csv2tsv < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (csv2tsv)  : "
	@(${TIMER} ~/install/tsv-utils/bin/csv2tsv < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (csv2tsv)  : "
	@(${TIMER} ~/install/tsv-utils/bin/csv2tsv < $< > /dev/null) 2>&1 | xargs
	@echo ""

ifneq ($(HAVE_C2TSV),1)
	@echo "c2tsv not found-- skipping"
else
	@printf "c-blake/nio/c2tsv    : " # from https://github.com/c-blake/nio
	@(${TIMER} c2tsv < $< > /dev/null)  2>&1 | xargs
	@printf "c-blake/nio/c2tsv    : "
	@(${TIMER} c2tsv < $< > /dev/null)  2>&1 | xargs
	@printf "c-blake/nio/c2tsv    : "
	@(${TIMER} c2tsv < $< > /dev/null)  2>&1 | xargs
endif
	@echo ""

select: select-worldcitiespop_mil select-worldcitiespop_big

select-worldcitiespop_mil select-worldcitiespop_big: select-% : %.csv %.tsv
	@echo "-----------------------------------------------"
	@echo "  Benchmarking select: $*"
	@echo "-----------------------------------------------"

	@printf "zsv                  : "
	@(${TIMER} ${ZSVBIN}${SELECT} -W -n -- 2 1 3-7 < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(${TIMER} ${ZSVBIN}${SELECT} -W -n -- 2 1 3-7 < $< > /dev/null) 2>&1 | xargs
	@printf "zsv                  : "
	@(${TIMER} ${ZSVBIN}${SELECT} -W -n -- 2 1 3-7 < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "zsv --parallel       : "
	@(${TIMER} ${ZSVBIN}${SELECT} --parallel -W -n $< -- 2 1 3-7 > /dev/null) 2>&1 | xargs
	@printf "zsv --parallel       : "
	@(${TIMER} ${ZSVBIN}${SELECT} --parallel -W -n $< -- 2 1 3-7 > /dev/null) 2>&1 | xargs
	@printf "zsv --parallel       : "
	@(${TIMER} ${ZSVBIN}${SELECT} --parallel -W -n $< -- 2 1 3-7 > /dev/null) 2>&1 | xargs
	@echo ""

ifneq ($(HAVE_XSV),1)
	@echo "xsv not found: skipping (run \'brew install xsv\' to install)"
else
	@printf "xsv                  : "
	@(${TIMER} xsv select 2,1,3-7 < $< > /dev/null) 2>&1 | xargs
	@printf "xsv                  : "
	@(${TIMER} xsv select 2,1,3-7 < $< > /dev/null) 2>&1 | xargs
	@printf "xsv                  : "
	@(${TIMER} xsv select 2,1,3-7 < $< > /dev/null) 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_POLARS),1)
	@echo "polars not found: skipping (run \`make polars-setup\` to set up)"
else
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (${TIMER} python3 -c 'import polars as pl; pl.scan_csv("$<", infer_schema=False).select(pl.nth(1, 0, 2, 3, 4, 5, 6)).sink_csv("/dev/null")') 2>&1 | xargs
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (${TIMER} python3 -c 'import polars as pl; pl.scan_csv("$<", infer_schema=False).select(pl.nth(1, 0, 2, 3, 4, 5, 6)).sink_csv("/dev/null")') 2>&1 | xargs
	@printf "polars               : "
	@. ${VENV_ACTIVATE}; (${TIMER} python3 -c 'import polars as pl; pl.scan_csv("$<", infer_schema=False).select(pl.nth(1, 0, 2, 3, 4, 5, 6)).sink_csv("/dev/null")') 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_DUCKDB),1)
	@echo "duckdb not found: skipping"
else
	@printf "duckdb               : "
	@(${TIMER} duckdb -c "COPY (from read_csv('$<', all_varchar=true) select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null'") 2>&1 | xargs
	@printf "duckdb               : "
	@(${TIMER} duckdb -c "COPY (from read_csv('$<', all_varchar=true) select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null'") 2>&1 | xargs
	@printf "duckdb               : "
	@(${TIMER} duckdb -c "COPY (from read_csv('$<', all_varchar=true) select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null'") 2>&1 | xargs
	@echo ""

	@printf "duckdb (QUOTE='')    : "
	@(${TIMER} duckdb -c "COPY (from read_csv('$<', all_varchar=true, quote='') select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null' (quote '')") 2>&1 | xargs
	@printf "duckdb (QUOTE='')    : "
	@(${TIMER} duckdb -c "COPY (from read_csv('$<', all_varchar=true, quote='') select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null' (quote '')") 2>&1 | xargs
	@printf "duckdb (QUOTE='')    : "
	@(${TIMER} duckdb -c "COPY (from read_csv('$<', all_varchar=true, quote='') select #2, #1, #3, #4, #5, #6, #7) TO '/dev/null' (quote '')") 2>&1 | xargs
	@echo ""
endif

ifneq ($(HAVE_TSVUTILS),1)
	@echo "tsv-utils not found: skipping"
else
	@printf "tsv-utils            : "
	@(${TIMER} ${TSV_UTILS}/tsv-select -d, -f 1-7 < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils            : "
	@(${TIMER} ${TSV_UTILS}/tsv-select -d, -f 1-7 < $< > /dev/null) 2>&1 | xargs
	@printf "tsv-utils            : "
	@(${TIMER} ${TSV_UTILS}/tsv-select -d, -f 1-7 < $< > /dev/null) 2>&1 | xargs
endif
	@echo ""

	@printf "tsv-utils (tsv input): "
	@(${TIMER} ${TSV_UTILS}/tsv-select -f 1-7 < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (tsv input): "
	@(${TIMER} ${TSV_UTILS}/tsv-select -f 1-7 < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@printf "tsv-utils (tsv input): "
	@(${TIMER} ${TSV_UTILS}/tsv-select -f 1-7 < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "csvcut               : "
	@(${TIMER} csvcut -c 2,1,3-7 < $< > /dev/null) 2>&1 | xargs
	@echo ""

	@printf "miller (mlr)         : "
	@(${TIMER} mlr --csv cut -o -f City,Country,AccentCity,Region,Population,Latitude,Longitude $< > /dev/null) 2>&1 | xargs
	@printf "miller (mlr)         : "
	@(${TIMER} mlr --csv cut -o -f City,Country,AccentCity,Region,Population,Latitude,Longitude $< > /dev/null) 2>&1 | xargs
	@echo ""

reset:
	rm -f benchmarks*.csv benchmarks*.txt *.tmp *.png

clean: reset
	rm -f worldcitiespop_big.csv worldcitiespop_big.tsv worldcitiespop_mil.csv worldcitiespop_mil.tsv worldcitiespop.csv

.PHONY: help all count select select-% clean polars-setup csv% tsv% graph%


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

