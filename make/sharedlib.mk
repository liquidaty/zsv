ifeq ($(OS),Windows_NT)
  WIN=1
endif

ifneq ($(WIN),)
  SHAREDLIB_EXT:=dll
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Linux)
    SHAREDLIB_EXT := so
  endif
  ifeq ($(UNAME_S),Darwin)
    SHAREDLIB_EXT := dylib
  endif
endif
