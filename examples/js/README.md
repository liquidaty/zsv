# Using libzsv from Javascript (browser or Node)

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
When running in the browser, libzsv is fast, but not quite as other CSV parsers that have been optimized for Javascript
(unlike when running natively, where libzsv is the fastest that we are aware of, that parses in the same manner as Excel for all edge cases).

The reason that pure-Javascript parsers can be faster in the browser is almost certaintly due to the extra memory operations required to pass string data beteween Javascript and web assembly (e.g. as described at https://hacks.mozilla.org/2019/08/webassembly-interface-types/).

Nonetheless, libzsv as web assembly called from Javascript is still extremely fast, and its primary objectives-- to be reasonably fast when used
via Javascript, while providing a consistent interface across any platform you are building for (browser, node, or bare metal on any operating system)
and offering the fastest bare-metal parser with flexible custom-function configuration-- are still very comfortably met.

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
