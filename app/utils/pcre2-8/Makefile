THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_LIB_BASE=$(shell cd ${THIS_MAKEFILE_DIR}/../../.. && pwd)
CONFIGFILE ?= ${THIS_LIB_BASE}/config.mk

$(info Using config file ${CONFIGFILE})

CONFIGFILEPATH:=$(shell ls ${CONFIGFILE} >/dev/null 2>/dev/null && realpath ${CONFIGFILE})
ifeq ($(CONFIGFILEPATH),)
  ifeq ($(shell dirname ${CONFIGFILE}),.)
    CONFIGFILEPATH:=$(shell ls ../${CONFIGFILE} >/dev/null 2>/dev/null && realpath ../${CONFIGFILE})
    ifneq ($(CONFIGFILEPATH),)
      $(info Config file ${CONFIGFILE} not found; using ../${CONFIGFILE})
    endif
  endif
endif

ifeq ($(CONFIGFILEPATH),)
  $(error Config file ${CONFIGFILE} not found)
else
  include ${CONFIGFILEPATH}
endif

CCBN=$(shell basename ${CC})
CFLAGS+= -I${PREFIX}/include
ifeq ($(DEBUG),1)
  DBG_SUBDIR+=dbg
else
  DBG_SUBDIR+=rel
endif

WIN=
ifeq ($(WIN),)
  WIN=0
  ifneq ($(findstring w64,$(CC)),) # e.g. mingw64
    WIN=1
  endif
endif
ifeq ($(WIN),0)
  BUILD_SUBDIR=$(shell uname)/${DBG_SUBDIR}
else
  BUILD_SUBDIR=win/${DBG_SUBDIR}
endif
BUILD_DIR=${THIS_LIB_BASE}/build/${BUILD_SUBDIR}/${CCBN}

ifneq ($(HAVE_PCRE2_8),1)
  $(error pcre2-8 library not found ($(CONFIGFILEPATH))-- if you think this is in error, re-run ./configure)
endif

help:
	@echo 'make build|test|clean'

${BUILD_DIR}/pcre2-8-test: pcre2-8-test.c pcre2-8.c
	${CC} -o $@ pcre2-8-test.c pcre2-8.c -L${LIBDIR} -lpcre2-8 -I${INCLUDEDIR}

.PHONY: build test clean

build: ${BUILD_DIR}/pcre2-8-test
	@echo Built ${BUILD_DIR}/pcre2-8-test

test: ${BUILD_DIR}/pcre2-8-test
	@printf "test 1: "
	@[ "`echo 'hixthere' | tr 'x' '\r' | $< '^hi$$' - | xargs`" == "Pattern contains ^ or $$ line anchors. Match" ] && echo Passed || (echo Failed! && exit 1)
	@printf "test 2: "
	@[ "`echo 'hixthere' | tr 'x' '\n' | $< '^hi$$' - | xargs`" == "Pattern contains ^ or $$ line anchors. No Match" ] && echo Passed || (echo Failed! && exit 1)
	@printf "test 3: "
	@[ "`echo 'hixthere' | tr 'x' '\n' | $< 'hi' - | xargs`" == "Pattern does not contain line anchors. Match" ] && echo Passed || (echo Failed! && exit 1)
	@printf "test 4: "
	@[ "`echo 'hixthere' | tr 'x' '\r' | $< 'hi' - | xargs`" == "Pattern does not contain line anchors. Match" ] && echo Passed || (echo Failed! && exit 1)
	@echo 'All tests passed'

clean:
	@rm -f ${BUILD_DIR}/pcre2-8-test
