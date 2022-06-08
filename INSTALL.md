# Building and installing the CLI

(In each case below, change `make` to `gmake` if applicable)

To build and install:

```shell
./configure && sudo ./install.sh
```

or:

```shell
./configure && sudo make install
```

To uninstall:

```shell
sudo make uninstall
```

To build the independent executables in a local build folder,
use `make install` instead of `make all`

## A note on compilers

GCC 11+ is the recommended compiler. Compared with clang, gcc in some cases
seems to produce faster code for reasons we have not yet determined.
