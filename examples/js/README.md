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
In this example, ZSV performs well, but is not as fast as other browser-based CSV parsers. That is OK! Here's why:

* It's still pretty darn fast

* It uses a row handler callback function. This provides the user with an easy way to achieve flexibility
  throughout the entire parsing process

* Because it's wasm, translation is bound to have some friction, especially here where a lot of string data
  is being passed between Javascript and the API. See e.g. https://hacks.mozilla.org/2019/08/webassembly-interface-types/
  for an explanation of why this innate performance drag exists between Javascript and wasm

* Its purpose is not to be the fastest in-browser parser. Rather, if you are building something natively, and want
  to use ZSV, you can benefit from using the same code base when you run in the browser. Most likely, ZSV's speed
  will not be the bottleneck, and possibly the benefit of having a single code base shared between native and browser
  environments is more than enough to make it worthwhile.

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
