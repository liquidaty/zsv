## Building and installing the library and/or CLI

### Packages

zsv is available from a number of package managers:
OSX: `brew install zsv`
Windows: `nuget install zsv`
Linux: `yum install zsv`

Pre-built binaries for OSX, Windows, Linux and BSD are available at XXXXXX

### From source

To build from source, you'll need a basic unix toolchain with `sh` and `make` or `gmake`:
  `./configure && sudo ./install.sh`

GCC is the recommended compiler, but clang is also supported. 

### Building and installing the CLI

(In each case below, change `make` to `gmake` if applicable)

To build and install:
```
./configure && sudo ./install.sh
```

or:
```
./configure && sudo make install
```

To uninstall:
```
sudo make uninstall
```

To build the independent executables in a local build folder,
use `make install` instead of `make all`


### Building and installing only the library
```
./configure && (cd src && sudo make install)
```


#### A note on compilers
GCC 11+ is the recommended compiler. Compared with clang,
gcc in some cases it seems to produce faster code for reasons
we have not yet determined.
