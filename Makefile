# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_DIR:=$(shell basename "${THIS_MAKEFILE_DIR}")
THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

CONFIGFILE ?= config.mk
include ${CONFIGFILE}

CONFIGFILEPATH=$(shell ls ${CONFIGFILE} >/dev/null 2>/dev/null && realpath ${CONFIGFILE})
ifeq ($(CONFIGFILEPATH),)
  $(error Config file ${CONFIGFILE} not found)
endif

help:
	@echo "**** Welcome to ZSV/lib (alpha) ****"
	@echo
	@echo "This package has two primary components:"
	@echo "* zsvlib  : a fast CSV parser library"
	@echo "* zsv     : CSV editor and toolkit (that uses zsvlib)"
	@echo
	@echo "\`zsv\` also supports dynamic extensions, a sample of which you can"
	@echo "build and run as described in docs/extension.md"
	@echo
	@echo "To build and install zsvlib and zsv:"
	@echo "  ./configure && make install"
	@echo
	@echo "To build and install only zsvlib:"
	@echo "  ./configure && make lib"
	@echo
	@echo "To build and install only zsv (i.e. install both, remove zsvlib):"
	@echo "  ./configure && make install && make uninstall-lib"
	@echo
	@echo "To save and build from a configuration without losing the current one,"
	@echo "use the configuration option CONFIGFILE e.g.:"
	@echo "  ./configure --config-file=/path/to/config.custom"
	@echo "  ./configure && make -C src CONFIGFILE=/path/to/config.custom install"
	@echo
	@echo "For more information, see README.md"

lib:
	@make -C src install CONFIGFILE=${CONFIGFILEPATH}

install:
	@make -C src install CONFIGFILE=${CONFIGFILEPATH}
	@make -C app install CONFIGFILE=${CONFIGFILEPATH}

all:
	@make -C src install CONFIGFILE=${CONFIGFILEPATH}
	@make -C app all CONFIGFILE=${CONFIGFILEPATH}

clean:
	@make -C app clean CONFIGFILE=${CONFIGFILEPATH}
	@make -C src clean CONFIGFILE=${CONFIGFILEPATH}

uninstall: uninstall-lib uninstall-app

uninstall-app:
	make -C app uninstall CONFIGFILE=${CONFIGFILEPATH}

uninstall-lib:
	make -C src uninstall CONFIGFILE=${CONFIGFILEPATH}

.PHONY: help install uninstall uninstall-app uninstall-lib
