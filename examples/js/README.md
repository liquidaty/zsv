# ZSV in web assembly example

## Overview

Two examples here demonstrate how the zsv CSV parser can be compiled to web assembly and called via javascript
via a static page in a browser or a Node module.

Most of the operative code is in [js/foot.js](js/foot.js) which effectively just converts between Javascript and emscripten.

### Browser
To run the browser demo, run `make run`.
Static files will be built in a subdirectory of the `build` directory, and a python local https server
will be started to serve them on https://127.0.0.1:8888

You can view a [demo of the built example here](https://liquidaty.github.io/zsv/examples/wasm/build/)

### Node module

To build a node module, run `make node`. Module files will be placed in node/node_modules/zsv-parser

### Node example and test
To run a test via node, run `make test`. The node module will be built, a sample program will be copied to
`node/index.js`, which reads CSV from stdin and outputs JSON, and a test will be run

## Prerequisites

To build, you need emscripten. To run the example web server, you need python3. Unlike some of the other examples,
this example does not require that libzsv is already installed

## Quick start

1. From the zsv base directory, run configure in your emscripten environment and save the config output
   to config.emcc:
   ```
   emconfigure ./configure CONFIGFILE=config.emcc
   ```

2. Change back to this directory (examples/wasm), then run `emmake make run`. You should see output messages
   ending with `Listening on https://127.0.0.1:8888`

3. Navigate to https://127.0.0.1:8888. If you get a browser warning, then using Chrome you can type "thisisunsafe" to proceed

4. Click the button to upload a file

## Performance

Running ZSV lib from Javascript is still experimental and is not yet fully optimized. Some performance challenges are
unique to web assembly + Javascript, especially where a lot of string data
is being passed between Javascript and the library (see e.g. https://hacks.mozilla.org/2019/08/webassembly-interface-types/)

Currently, this Node/javascript package reads data either via unbuffered synchronous file read, or via `parseBytes` which
requires a somewhat inefficient series of memory copies.

On small files (under 1 MB), zsv-lib is 30-75% faster than, for example, the csv-parser library. However, on larger files,
likely due to its unoptimized raw synchronous read, it can be about 50% slower.

Using a buffered async file read is likely to substantially increase performance.
That said, at this time, the library remains reasonably fast with bounded memory.
If further performance or API enhancements would be important to you, feel free to post an issue
on https://github.com/liquidaty/zsv.

## All the build commands

Separate commands can be used for build, run and clean:
```
make build
make node
make run
make clean
```

Add MINIFY=1 to any of the above to generate minified code

To see all make options:
```
make
```
