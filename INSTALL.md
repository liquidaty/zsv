### Building and installing the CLI
To build and install:
```
./configure && sudo make install
```

To uninstall:
```
sudo make uninstall
```

To build the independent executables in a local build folder, substitute `make install`
for `make all`

### FreeBSD

Using clang on FreeBSD, compilation may appear to succeed but result in an executable
that crashes with a BUS error. To avoid this, please build with gcc, not clang e.g.:
```
su -
CC=gcc ./configure && make install
```
