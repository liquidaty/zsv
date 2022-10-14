# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

CC ?= emcc
SED ?= sed
NODE_PKG_NAME=zsv-parser

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

BROWSER_JS=${BUILD_DIR}/zsv.js
INDEX=${BUILD_DIR}/index.html
EMJS=${BUILD_DIR}/zsv.em.js
WASM=${BUILD_DIR}/zsv.em.wasm

CFLAGS+= ${CFLAGS_PIC} -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_RUNTIME_METHODS="['setValue','addFunction','removeFunction']" -s RESERVED_FUNCTION_POINTERS=4 -s EXPORTED_FUNCTIONS="['_free','_malloc']"

ifeq ($(DEBUG),0)
  CFLAGS+= -O3 -DNDEBUG #  -std=gnu11 -Wno-gnu-statement-expression -Wshadow -Wall -Wextra -Wno-missing-braces -pedantic -DSTDC_HEADERS -D_GNU_SOURCE -lm -mavx2 -ftree-vectorize -flto
else
  CFLAGS += ${CFLAGS_DEBUG}
endif

INDEX=${BUILD_DIR}/index.html
OTHERUTIL=${BUILD_DIR}/localhost.pem
STATIC=${INDEX} ${OTHERUTIL}

##### test-related definitions
COLOR_NONE=\033[0m
COLOR_GREEN=\033[1;32m
COLOR_RED=\033[1;31m
COLOR_BLUE=\033[1;34m
COLOR_PINK=\033[1;35m

TEST_PASS=echo "${COLOR_BLUE}$@: ${COLOR_GREEN}Passed${COLOR_NONE}"
TEST_FAIL=(echo "${COLOR_BLUE}$@: ${COLOR_RED}Failed!${COLOR_NONE}" && exit 1)
#####

.PHONY: help all run clean prep node setup

help:
	@echo "make [build|run|node|test|clean]"
	@echo "to generate minified code, run"
	@echo "  make setup"
	@echo "once, then run"
	@echo "  make MINIFY=1 [build|run|node|test]"

build: ${BROWSER_JS} ${STATIC}
	@echo Built ${BROWSER_JS}

run: ${BROWSER_JS} ${STATIC}
	cd ${BUILD_DIR} && ${THIS_MAKEFILE_DIR}/browser/util/http-dev.py

clean:
	rm -rf build node

prep: ../../app/external/utf8proc-2.6.1/utf8proc.h

../../app/external/utf8proc-2.6.1/utf8proc.h:
	make -C ../../app ./external/utf8proc-2.6.1/utf8proc.h THIS_MAKEFILE_DIR=.

test: npm/test/index.js node
	@mkdir -p build/test
	@cp -p $< node/
	@echo "Running test (example) program node/index.js"
	@(cd node && node index.js ../../../data/test/desc.csv > ../build/test/out.json 2> ../build/test/out.err1)
	@sed 's/[0-9.]*ms//g' < build/test/out.err1 > build/test/out.err
	@cmp build/test/out.err npm/test/out.err
	@cmp build/test/out.json npm/test/out.json && ${TEST_PASS} || ${TEST_FAIL}

${INDEX}: browser/index.html
	@mkdir -p `dirname "$@"`
	@cp -p $< $@

${OTHERUTIL}: ${BUILD_DIR}/% : browser/util/%
	@mkdir -p `dirname "$@"`
	@cp -p $< $@

${EMJS}: ${ZSVSRC} zsv_parser_api_dummy.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} $^ -o $@

${WASM}: ${EMJS}

${BROWSER_JS}: prep ${EMJS} js/head.js js/foot.js
	@mkdir -p `dirname "$@"`
	@cat js/head.js > $@.tmp.js
	@cat ${EMJS} >> $@.tmp.js
	@cat js/foot.js >> $@.tmp.js
ifeq ($(MINIFY),1)
	@uglifyjs $@.tmp.js -c -m > $@
else
	@mv $@.tmp.js $@
endif

### node package build
NODE_INDEX=node/node_modules/${NODE_PKG_NAME}/index.js
NODE_WASM=node/node_modules/${NODE_PKG_NAME}/zsv.em.wasm
${NODE_WASM}: ${WASM}
	@mkdir -p `dirname "$@"`
	@cp -p $< $@

NODE_PKG_JSON=node/node_modules/${NODE_PKG_NAME}/package.json
${NODE_PKG_JSON}: npm/package.json
	@mkdir -p `dirname "$@"`
	@cp -p $< $@

${NODE_INDEX}: ${BROWSER_JS} js/foot_node.js
	@mkdir -p `dirname "$@"`
	@cat $^ > $@.tmp.js
ifeq ($(MINIFY),1)
	@uglifyjs $@.tmp.js -c -m > $@
else
	@mv $@.tmp.js $@
endif

setup:
	npm install -g uglify-es


node: ${NODE_WASM} ${NODE_INDEX} ${NODE_PKG_JSON}
	@echo "Built node module in node/node_modules/zsv-parser"

## node test
