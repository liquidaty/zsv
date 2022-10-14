const process= require('node:process');
const { PerformanceObserver, performance } = require('node:perf_hooks');
const fs = require('fs');
const zsvParser = require('zsv-parser');

/**
 *
 */

let zHandle;
let rowcount;
let start;
let data;
let bytes_read;

function reset() {
  rowcount = 0;
  start = performance.now();
  data = [];
  bytes_read = 0;
}

function row_handler() {
  rowcount++;
  let count = zsvParser.cellCount(zHandle);
  let row = [];
  for(let i = 0; i < count; i++)
    row.push(zsvParser.getCell(zHandle, i));
  data.push(row);
}

function processChunk(chunk) {
  if(chunk && chunk.length) {
    bytes_read += chunk.length;
    zsvParser.parseBytes(zHandle, chunk);
  }
}

function finish() {
  zsvParser.finish(zHandle);
  let end = performance.now()
  console.error('Parsed ' + bytes_read + ' bytes; ' + rowcount + ' rows in ' + (end - start) + 'ms\n' +
                'You can view the parsed data in your browser dev tools console (rt-click and select Inspect)');
  console.log(data);
  zsvParser.delete(zHandle);
}

function initialize() {
  reset();
  zHandle = zsvParser.new(row_handler);
}

zsvParser.runOnLoad(function() {
  // initialize
  initialize();

  // read data
  try {
    // read stdin if we have no arguments, else the first argument
    const readStream = process.argv.length < 3 ? process.stdin : fs.createReadStream(process.argv[2])

    readStream.on('error', (error) => console.log(error.message));

    // set our callbacks
    readStream.on('data', processChunk);
    readStream.on('end', finish);

  } catch(e) {
    console.error('Unable to open for read: ' + inputFileName);
    console.error(e);
  }
});
