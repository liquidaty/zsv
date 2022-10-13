# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_DIR:=$(shell basename "${THIS_MAKEFILE_DIR}")
THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

CONFIGFILE ?= ../../config.mk
include ${CONFIGFILE}

CONFIGFILEPATH=$(shell ls ${CONFIGFILE} >/dev/null 2>/dev/null && realpath ${CONFIGFILE})
ifeq (${CONFIGFILEPATH},)
  $(error Config file ${CONFIGFILE} not found)
endif

ifeq ($(MAKE),)
  MAKE=make
endif

CFLAGS+= -I${PREFIX}/include
ifeq ($(ZSV_EXTRAS),1)
  CFLAGS+= -DZSV_EXTRAS
endif

WIN=
ifeq ($(WIN),)
  WIN=0
  ifneq ($(findstring w64,$(CC)),) # e.g. mingw64
    WIN=1
  endif
endif

EXE=
ifeq ($(WIN),1)
  EXE=.exe
endif

CFLAGS+=-g -O0

BUILD_DIR=build
LIBS+=-lzsv

help:
	@echo "**** Examples using libzsv ****"
	@echo
	@echo "Dependencies:"
	@echo "  - libzsv must already be installed (for more info, \`cd ../.. && ${MAKE}\`)"
	@echo "  - configuration file must be available (to generate, \`cd ../.. && ./configure\`)"
	@echo
	@echo "To build using the default configuration:"
	@echo "  ${MAKE} build"
	@echo
	@echo "To build using a specified configuration:"
	@echo "  ${MAKE} CONFIGFILE=/path/to/config.mk build"
	@echo
	@echo "To remove all build files:"
	@echo "  ${MAKE} clean"
	@echo

build: simple print_my_column push_parser


simple print_my_column push_parser: % : ${BUILD_DIR}/%${EXE}
	@echo Built $<

${BUILD_DIR}/print_my_column${EXE} ${BUILD_DIR}/simple${EXE} ${BUILD_DIR}/push_parser${EXE}: ${BUILD_DIR}/%${EXE} : %.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -o $@ $< ${LIBS} -L${LIBDIR}

clean:
	@rm -rf ${BUILD_DIR}

.PHONY: help build clean simple print_my_column push_parser
