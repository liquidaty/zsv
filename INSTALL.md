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

#### A note on compilers
GCC 11+ is the recommended compiler. Compared with clang,
gcc in some cases it seems to produce faster code for reasons
we have not yet determined.
