# Makefile snippet to be included into standlone Makefile with BUILD_DIR already defined
THIS_MAKEFILE_DIR_TMP:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

ifeq ($(DEBUG),1)
  PCRE2_CFLAGS=-g
else
  PCRE2_CFLAGS=-O3
endif

PCRE2_CFLAGS+=-fPIC

PDCURSES_VERSION ?= 3.9

help:
	@echo 'make targets:'
	@echo '  ${BUILD_DIR}-external/pcre2/lib/libpcre2-8.a'
	@echo '  ${BUILD_DIR}-external/pdcurses/PDCurses-${PDCURSES_VERSION}/wincon/libpdcurses.a'

PCRE2_CONFIG_OPTIIONS=
ifneq ($(CONFIGURE_HOST),)
  PCRE2_CONFIG_OPTIIONS+=--host=${CONFIGURE_HOST}
endif

${BUILD_DIR}-external/pcre2/lib/libpcre2-8.a:
	@mkdir -p ${BUILD_DIR}-external/pcre2
	@cd ${BUILD_DIR}-external/pcre2 && \
	tar xf ${THIS_MAKEFILE_DIR_TMP}/pcre2-pcre2-10.47.tar.gz && \
	cd pcre2-pcre2-10.47 && \
	CFLAGS="${PCRE2_CFLAGS}" ./configure ${PCRE2_CONFIG_OPTIIONS} \
	--prefix=${BUILD_DIR}-external/pcre2 \
	--enable-static=yes \
	--enable-shared=no && \
	${MAKE} install

# PDCurses (Windows/MinGW console library) for the 'sheet' feature: untar the
# vendored source and build the static wincon backend, mirroring pcre2 above.
# CC/AR/MAKE come from the config file included alongside this snippet.
${BUILD_DIR}-external/pdcurses/PDCurses-${PDCURSES_VERSION}/wincon/libpdcurses.a:
	@mkdir -p ${BUILD_DIR}-external/pdcurses
	@cd ${BUILD_DIR}-external/pdcurses && \
	tar xf ${THIS_MAKEFILE_DIR_TMP}/PDCurses-${PDCURSES_VERSION}.tar.gz && \
	cd PDCurses-${PDCURSES_VERSION}/wincon && \
	${MAKE} CC=${CC} AR=${AR} WIDE=Y UTF8=Y && \
	mv pdcurses.a libpdcurses.a && \
	rm -f ./*.o
