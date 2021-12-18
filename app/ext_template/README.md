## Extension template

`zsv` can easily be extended by simply creating a shared library
that implements the interface specified in zsv/ext/implementation.h

This C file is a template you can use to implement your own extension.

## Using the template

To use the template:
1. Copy the files in this directory
   (Makefile, configure, and YOUR_EXTENSION_zsvext.c)
   to the location where your extension source will reside
2. Customize YOUR_EXTENSION_zsvext.c as appropriate (see comments in
   the C code for further details)
3. Optionally, rename YOUR_EXTENSION_zsvext.c and update the Makefile
   accordingly

## Dependencies
To build the extension, `zsvlib` and related include files must be installed
(Obviously, since this is a zsv extension, you need `zsv` to run it)

## Building
To build the shared library file, run:
```
./configure && make
```

To install, place the shared library file in any system path, or in the same
folder as `zsv`, then run:
```
zsv register XX
```
where XX is the ID of your extension