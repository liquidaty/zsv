# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_DIR:=$(shell basename "${THIS_MAKEFILE_DIR}")
THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

THIS_MAKE=`basename ${MAKE}`

CONFIGFILE ?= config.mk
include ${CONFIGFILE}

CONFIGFILEPATH=$(shell ls ${CONFIGFILE} >/dev/null 2>/dev/null && realpath ${CONFIGFILE})
ifeq (${CONFIGFILEPATH},)
  $(error Config file ${CONFIGFILE} not found)
endif

help:
	@echo "**** Welcome to zsv+lib ****"
	@echo
	@echo "This package has two primary components:"
	@echo "* zsvlib  : a fast CSV parser library"
	@echo "* zsv     : CSV editor and toolkit (that uses zsvlib)"
	@echo
	@echo "\`zsv\` also supports dynamic extensions, a sample of which you can"
	@echo "build and run as described in docs/extension.md"
	@echo
	@echo "To build, test and install zsvlib and zsv:"
	@echo "  ./configure && ${THIS_MAKE} test"
	@echo
	@echo "To build and install zsvlib and zsv:"
	@echo "  ./configure && ${THIS_MAKE} install"
	@echo
	@echo "To build and install only zsvlib:"
	@echo "  ./configure && ${THIS_MAKE} -C src install"
	@echo
	@echo "To build and install only zsv (i.e. install both, remove zsvlib):"
	@echo "  ./configure && ${THIS_MAKE} install && ${THIS_MAKE} -C src uninstall"
	@echo
	@echo "To save and build from a configuration without losing the current one,"
	@echo "use the configuration option CONFIGFILE e.g.:"
	@echo "  ./configure --config-file=/path/to/config.custom"
	@echo "  ./configure && ${THIS_MAKE} -C src CONFIGFILE=/path/to/config.custom install"
	@echo
	@echo "To clean (remove temporary build objects) (after running configure):"
	@echo "  ${THIS_MAKE} clean"
	@echo
	@echo "To uninstall libs and apps:"
	@echo "  ${THIS_MAKE} uninstall"
	@echo
	@echo "To test:"
	@echo "  ${THIS_MAKE} test"
	@echo
	@echo "To install/uninstall to/from a stage directory with DESTDIR:"
	@echo "  ${THIS_MAKE} install DESTDIR=/tmp/stage"
	@echo "  ${THIS_MAKE} uninstall DESTDIR=/tmp/stage"
	@echo
	@echo "Additional make options are available for the library or the apps by"
	@echo "running ${THIS_MAKE} from the src or app directory."
	@echo
	@echo "For more information, see README.md"

check test:
	@${MAKE} -C app test CONFIGFILE=${CONFIGFILEPATH}
	@${MAKE} -C src install CONFIGFILE=${CONFIGFILEPATH}
	@${MAKE} -C examples/lib test CONFIGFILE=${CONFIGFILEPATH}

DESTDIR ?=

build install uninstall: % :
	@${MAKE} -C src $* CONFIGFILE=${CONFIGFILEPATH} DESTDIR="${DESTDIR}"
	@${MAKE} -C app $* CONFIGFILE=${CONFIGFILEPATH} DESTDIR="${DESTDIR}"

clean:
	@${MAKE} -C src clean CONFIGFILE=${CONFIGFILEPATH}
	@${MAKE} -C app clean-all CONFIGFILE=${CONFIGFILEPATH}
	@rm -rf ${THIS_MAKEFILE_DIR}/build

# Run tests under AddressSanitizer. Uses a separate config file and build
# directory so the normal build is not disturbed.  Not part of the default
# test target -- invoke explicitly with: make test-asan
#
# By default, uses the same CC as the normal build. On macOS with Homebrew
# gcc, the sanitizer runtime may not be available; in that case, override
# with: ASAN_CC=clang make test-asan
ASAN_CONFIGFILE=${THIS_MAKEFILE_DIR}/config-asan.mk
ASAN_CC ?= ${CC}

test-asan:
	CC=${ASAN_CC} \
	CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
	LDFLAGS="-fsanitize=address -Wl,-z,now" \
	./configure --config-file=${ASAN_CONFIGFILE} --prefix=/tmp/zsv-build-asan
	@# Strip -fwhole-program from LDFLAGS_OPT -- it is incompatible with ASAN
	@sed -i 's/-fwhole-program//' ${ASAN_CONFIGFILE}
	@rm -rf build /tmp/zsv-build-asan
	ASAN_OPTIONS="halt_on_error=1:detect_leaks=0" \
	${MAKE} -C src install CONFIGFILE=${ASAN_CONFIGFILE}
	LD_BIND_NOW=1 ASAN_OPTIONS="halt_on_error=1:detect_leaks=0" \
	${MAKE} -C app/test test CONFIGFILE=${ASAN_CONFIGFILE}

clean-asan:
	@rm -f ${ASAN_CONFIGFILE}
	@rm -rf /tmp/zsv-build-asan

.PHONY: help build install uninstall clean check test test-asan clean-asan
