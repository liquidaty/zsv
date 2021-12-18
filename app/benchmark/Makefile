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
	@echo "To run all tests:"
	@echo "    make all"

all: count select

worldcitiespop_mil.csv:
	curl -LO 'https://burntsushi.net/stuff/worldcitiespop_mil.csv'

worldcitiespop_mil.tsv: worldcitiespop_mil.csv
	~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/csv2tsv < $< > $@

count: worldcitiespop_mil.csv worldcitiespop_mil.tsv
	@echo "${ZSVBIN}"count
	@printf "   "
	@(time ${ZSVBIN}count < $< > /dev/null) 2>&1 | xargs
	@echo xsv
	@printf "   "
	@(time xsv count < $< > /dev/null) 2>&1 | xargs
	@echo tsv-utils
	@printf "   "
	@(time ~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/number-lines -d, < $< > /dev/null) 2>&1 | xargs
	@echo "tsv-utils (tsv input)"
	@printf "   "
	@(time ~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/number-lines < $(word 2,$^) > /dev/null) 2>&1 | xargs


select: worldcitiespop_mil.csv worldcitiespop_mil.tsv
	@echo "${ZSVBIN}"select
	@printf "   "
	@(time ${ZSVBIN}select -W -n -- 2 1 3-7 < $< > /dev/null) 2>&1 | xargs
	@echo xsv
	@printf "   "
	@(time xsv select 2,1,3-7 < $< > /dev/null) 2>&1 | xargs
	@echo tsv-utils
	@printf "   "
	@(time ~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/tsv-select -d, -f 1-7 < $< > /dev/null) 2>&1 | xargs
	@echo "tsv-utils (tsv input)"
	@printf "   "
	@(time ~/Downloads/tsv-utils-v2.2.1_osx-x86_64_ldc2/bin/tsv-select -f 1-7 < $(word 2,$^) > /dev/null) 2>&1 | xargs
	@echo "miller"
	@printf "   "
	@(time mlr --csv cut -o -f City,Country,AccentCity,Region,Population,Latitude,Longitude $< > /dev/null) 2>&1 | xargs

.PHONY: help all count select
