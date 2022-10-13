# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

CC ?= emcc
SED ?= sed

CONFIGFILE= ${THIS_MAKEFILE_DIR}/../../config.emcc
$(info Using config file ${CONFIGFILE})
include ${CONFIGFILE}
CONFIGFILEPATH=$(shell ls ${CONFIGFILE} >/dev/null 2>/dev/null && realpath ${CONFIGFILE})
ifeq ($(CONFIGFILEPATH),)
  $(error Config file ${CONFIGFILE} not found)
endif

ifeq ($(findstring emcc,$(CC)),) # emcc
  $(error Please use emcc to compile)
endif

ZSVSRC1=zsv.c ../app/utils/string.c
ZSVSRCDIR=${THIS_MAKEFILE_DIR}/../../src
ZSVSRC=$(addprefix ${ZSVSRCDIR}/,${ZSVSRC1})

INCLUDE_DIR=${THIS_MAKEFILE_DIR}/../../include

CFLAGS+= -I../../app/external/utf8proc-2.6.1 # for app/utils/string.c

ifeq ($(DEBUG),1)
  DBG_SUBDIR+=dbg
else
  DBG_SUBDIR+=rel
endif

CCBN=$(shell basename ${CC})
BUILD_DIR=${THIS_MAKEFILE_DIR}/build/${DBG_SUBDIR}/${CCBN}

TARGET=${BUILD_DIR}/zsv.js
INDEX=${BUILD_DIR}/index.html
EMJS=${BUILD_DIR}/zsv.em.js

CFLAGS+= ${CFLAGS_PIC} -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_RUNTIME_METHODS="['setValue','addFunction','removeFunction']" -s RESERVED_FUNCTION_POINTERS=4 -s EXPORTED_FUNCTIONS="['_free','_malloc']"

ifeq ($(DEBUG),0)
  CFLAGS+= -O3 -DNDEBUG #  -std=gnu11 -Wno-gnu-statement-expression -Wshadow -Wall -Wextra -Wno-missing-braces -pedantic -DSTDC_HEADERS -D_GNU_SOURCE -lm -mavx2 -ftree-vectorize -flto
else
  CFLAGS += ${CFLAGS_DEBUG}
endif

INDEX=${BUILD_DIR}/index.html
OTHERJS=${BUILD_DIR}/stream_file.js
OTHERUTIL=${BUILD_DIR}/localhost.pem
STATIC=${INDEX} ${OTHERJS} ${OTHERUTIL}

.PHONY: help all run clean prep

help:
	@echo make [all|run|clean]

all: ${TARGET} ${STATIC}
	@echo Built ${TARGET}

run: ${TARGET} ${STATIC}
	cd ${BUILD_DIR} && ${THIS_MAKEFILE_DIR}/util/http-dev.py

clean:
	rm -rf ${BUILD_DIR} ${TARGET} ${EMJS} ${STATIC}

prep: ../../app/external/utf8proc-2.6.1/utf8proc.h

../../app/external/utf8proc-2.6.1/utf8proc.h:
	make -C ../../app ./external/utf8proc-2.6.1/utf8proc.h THIS_MAKEFILE_DIR=.


${INDEX}: index.html
	@mkdir -p `dirname "$@"`
	@cp -p $< $@

${OTHERJS}: ${BUILD_DIR}/% : js/%
	@mkdir -p `dirname "$@"`
	@cp -p $< $@

${OTHERUTIL}: ${BUILD_DIR}/% : util/%
	@mkdir -p `dirname "$@"`
	@cp -p $< $@

${EMJS}: ${ZSVSRC} zsv_parser_api_dummy.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} $^ -o $@
#	${CC} ${CFLAGS} -I${INCLUDE_DIR} -L${LIBDIR} -lzsv $^ -o $@

${TARGET}: prep ${EMJS} js/head.js js/foot.js
	@mkdir -p `dirname "$@"`
	@cat js/head.js > $@.tmp
	@cat ${EMJS} >> $@.tmp
	@cat js/foot.js >> $@.tmp
	mv $@.tmp $@
