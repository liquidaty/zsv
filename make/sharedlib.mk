ifeq ($(OS),Windows_NT)
  WIN=1
endif

ifeq ($(WIN),1)
  SHAREDLIB_EXT:=dll
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Linux)
    VERSION_CLEAN := $(word 1,$(subst -, ,$(VERSION)))
    VERSION_PARTS := $(subst ., ,$(VERSION_CLEAN))
    VERSION_MAJOR := $(word 1,$(VERSION_PARTS))
    VERSION_MINOR := $(word 2,$(VERSION_PARTS))
    VERSION_PATCH := $(word 3,$(VERSION_PARTS))
    SHAREDLIB_EXT := so.$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)
    SHAREDLIB_EXT_SO_NAME := so.$(VERSION_MAJOR)
    SHAREDLIB_EXT_LINK_NAME := so
  endif
  ifeq ($(UNAME_S),Darwin)
    SHAREDLIB_EXT := dylib
  endif
endif
