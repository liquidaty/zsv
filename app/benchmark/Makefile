# Makefile for use with GNU make

CONFIGFILE ?= ../../config.mk
$(info Using config file ${CONFIGFILE})
include ${CONFIGFILE}

CC ?= cc

WIN=
ifeq ($(WIN),)
  WIN=0
  ifneq ($(findstring w64,$(CC)),) # e.g. mingw64
    WIN=1
  endif
endif
ifeq ($(WIN),0)
  BUILD_SUBDIR=$(shell uname)/rel
else
  BUILD_SUBDIR=win/rel
endif

CCBN=$(shell basename ${CC})
ZSVBIN=../../build/${BUILD_SUBDIR}/${CCBN}/bin/zsv_

help:
	@echo "To run all tests (set QUICK to skip mlr and csvcut):"
	@echo "    make all [QUICK=1]"

all: count select

worldcitiespop_mil.csv:
	curl -LO 'https://burntsushi.net/stuff/worldcitiespop_mil.csv'

worldcitiespop_mil.tsv: worldcitiespop_mil.csv
	~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/csv2tsv < $< > $@

count: worldcitiespop_mil.csv worldcitiespop_mil.tsv
	@echo "${ZSVBIN}"count

	@printf "zsv                  : "
	@(time ${ZSVBIN}count < $< > /dev/null) 2>&1 | xargs

	@printf "xsv                  : "
	@(time xsv count < $< > /dev/null) 2>&1 | xargs

	@printf "tsv-utils            : "
	@(time ~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/number-lines -d, < $< > /dev/null) 2>&1 | xargs

	@printf "tsv-utils (tsv input): "
	@(time ~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/number-lines < $(word 2,$^) > /dev/null) 2>&1 | xargs


select: worldcitiespop_mil.csv worldcitiespop_mil.tsv
	@echo "${ZSVBIN}"select
	@printf "zsv                  : "
	@(time ${ZSVBIN}select -W -n -- 2 1 3-7 < $< > /dev/null) 2>&1 | xargs

	@printf "xsv                  : "
	@(time xsv select 2,1,3-7 < $< > /dev/null) 2>&1 | xargs

	@printf "tsv-utils            : "
	@(time ~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/tsv-select -d, -f 1-7 < $< > /dev/null) 2>&1 | xargs

	@printf "tsv-utils (tsv input): "
	@(time ~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/tsv-select -f 1-7 < $(word 2,$^) > /dev/null) 2>&1 | xargs

ifneq ($(QUICK),1)
	@printf "csvcut               : "
	@(time csvcut -c 2,1,3-7 < $< > /dev/null) 2>&1 | xargs

	@printf "miller (mlr)         : "
	@(time mlr --csv cut -o -f City,Country,AccentCity,Region,Population,Latitude,Longitude $< > /dev/null) 2>&1 | xargs
endif

.PHONY: help all count select
