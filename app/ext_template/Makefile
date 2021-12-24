# Copyright (C) zsv contributors. All rights reserved.
#
# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

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

ifeq ($(DEBUG),1)
  DBG_SUBDIR+=dbg
else
  DBG_SUBDIR+=rel
endif

ifeq ($(WIN),0)
  BUILD_SUBDIR=$(shell uname)/${DBG_SUBDIR}
  SO=so
  ifeq ($(BUILD_SUBDIR),Linux)
    CFLAGS+= -ldl -lpthread
  endif
  CFLAGS+= -fPIC
else
  BUILD_SUBDIR=win/${DBG_SUBDIR}
  CFLAGS+= -D__USE_MINGW_ANSI_STDIO -D_ISOC99_SOURCE
  CFLAGS+= -Wl,--strip-all
  CFLAGS+= -fpie
  SO=dll
endif

DEBUG=0
ifeq ($(DEBUG),0)
  CFLAGS+= -O3 -DNDEBUG -Wshadow -Wall -Wextra -pedantic -DSTDC_HEADERS -flto
else
  CFLAGS += -g
endif

THIS_LIB_BASE=$(shell cd ../.. && pwd)
CCBN=$(shell basename ${CC})
EXTENSION_ID=xx
BUILD_DIR=${THIS_MAKEFILE_DIR}/build/${BUILD_SUBDIR}/${CCBN}
TARGET=${BUILD_DIR}/bin/zsvext${EXTENSION_ID}.${SO}

UTILS1=
CFLAGS_SHARED=-shared
ifneq ($(findstring emcc,$(CC)),) # emcc
  CFLAGS_SHARED=-s SIDE_MODULE=1
  # add some additional file system support glue
  UTILS1+= emcc/fs_api
endif

UTILS1+= # put any zsv utils here
UTILS=$(addprefix ${BUILD_DIR}/objs/utils/,$(addsuffix .o,${UTILS1}))

CFLAGS+= -I${THIS_LIB_BASE}/include

all: ${TARGET}

# zsv utils
${BUILD_DIR}/objs/utils/%.o:
	(cd .. && make CONFIGFILE=${CONFIGFILEPATH} CC=${CC} DEBUG=${DEBUG} $@ )

install: ${TARGET}
	@echo "To install, copy ${TARGET} to a system path or the same dir"
	@echo "as the zsv binary"

clean:
	@rm -f ${TARGET}

${TARGET}: YOUR_EXTENSION_zsvext.c ${UTILS}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} ${CFLAGS_SHARED} $< ${UTILS} -o $@

.PHONY: all test clean test-%
