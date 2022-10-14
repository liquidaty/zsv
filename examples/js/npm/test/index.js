const process= require('node:process');
const { PerformanceObserver, performance } = require('node:perf_hooks');
const fs = require('fs');
const zsvParser = require('zsv-parser');

/**
 * Example using libzsv to parse CSV input and execute a custom row handler function as each row is parsed
 */

/**
 * We will use a separate context for each parser, which is a pattern that allows us to run multiple
 * parsers at the same time independently, although this example only runs one at a time
 */
function createContext() {
  return {
    parser: null,                 // handle to our parser
    rowcount: 0,                  // how many rows we've parsed so far
    startTime: performance.now(), // when the run was started
    data: [],                     // object to hold all data parsed thus far
    bytesRead: 0                  // how many bytes we've parsed thus far
  };
}

/**
 * Define a row handler which will be called each time a row is parsed, and which
 * accesses all data through a context object
 */
function rowHandler(ctx) {
  ctx.rowcount++;
  let count = zsvParser.cellCount(ctx.parser);
  let row = [];
  for(let i = 0; i < count; i++)
    row.push(zsvParser.getCell(ctx.parser, i));
  ctx.data.push(row);
}

/**
 * Define the steps to take after all parsing has completed
 */
function finish(ctx) {
  if(ctx.parser) {
    zsvParser.finish(ctx.parser); /* finish parsing */
    let endTime = performance.now()   /* check the time */

    /* output a message describing the parse volume and performance */
    console.error('Parsed ' + ctx.bytesRead + ' bytes; ' + ctx.rowcount +
                  ' rows in ' + (endTime - ctx.startTime) + 'ms\n' +
                  'You can view the parsed data in your browser dev tools console (rt-click and select Inspect)');

    /**
     * output the parsed data (we could have also done this while we parsed, and not
     * bothered to accumulate it, to save memory)
     */
    console.log(ctx.data);

    /* destroy the parser */
    zsvParser.delete(ctx.parser);
    ctx.parser = null;
  }
}

/**
 * After the zsv-parser module has loaded, read from stdin or the specified file,
 * parse the input, apply the row handler for each parsed row and finish
 */
zsvParser.runOnLoad(function() {

  /* get a new context */
  let ctx = createContext();

  /* initialize parser */
  ctx.parser = zsvParser.new(rowHandler, ctx);

  try {
    /* read stdin if we have no arguments, else the first argument */
    const readStream = process.argv.length < 3 ? process.stdin : fs.createReadStream(process.argv[2])
    readStream.on('error', (error) => console.log(error.message));

    /* while we read, pass data through the parser */
    readStream.on('data', function(chunk) {
      if(chunk && chunk.length) {
        ctx.bytesRead += chunk.length;
        zsvParser.parseBytes(ctx.parser, chunk);
      }
    });

    /* set our final callback */
    readStream.on('end', function() { finish(ctx); });

  } catch(e) {
    console.error('Unable to open for read: ' + inputFileName);
    console.error(e);
    finish(ctx);
  }
});
