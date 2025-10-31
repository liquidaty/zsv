# Makefile snippet to be included into standlone Makefile with BUILD_DIR already defined
THIS_MAKEFILE_DIR_TMP:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

ifeq ($(DEBUG),1)
  PCRE2_CFLAGS=-g
else
  PCRE2_CFLAGS=-O3
endif

help:
	@echo 'make targets:'
	@echo '  ${BUILD_DIR}/external/pcre2/lib/libpcre2-8.a'

${BUILD_DIR}/external/pcre2/lib/libpcre2-8.a:
	@mkdir -p ${BUILD_DIR}/external/pcre2
	@cd ${BUILD_DIR}/external/pcre2 && \
	tar xf ${THIS_MAKEFILE_DIR_TMP}/pcre2-pcre2-10.47.tar.gz && \
	cd pcre2-pcre2-10.47 && \
	CFLAGS="${PCRE2_CFLAGS}" ./configure --prefix=${BUILD_DIR}/external/pcre2 --enable-static=yes --enable-shared=no && \
	${MAKE} install
