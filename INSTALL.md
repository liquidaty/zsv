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

#### A note on compilers
GCC 11+ is the recommended compiler. Compared with clang,
on OSX (Intel), it produces faster code, and on some platforms
clang fails (e.g. FreeBSD-- see below)

### FreeBSD

Using clang on FreeBSD, compilation may appear to succeed but result in an executable
that crashes with a BUS error. To avoid this, please build with gcc, not clang e.g.:
```
su -
CC=gcc ./configure && make install
```
