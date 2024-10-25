ifeq ($(OS),Windows_NT)
  WIN=1
endif

ifeq ($(WIN),1)
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
