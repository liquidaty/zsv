# zsv-parser

zsv-parser is a node package of the high-performance zsv parser for CSV and other delimited
or fixed-width tabular text data

For more information about the underlying library, visit https://github.com/liquidaty/zsv

## Installation


```bash
npm install zsv-parser
```

## Usage

libzsv works generally as follows:
1. You define a row handler function, and push chunks of data through the parser
2. The parser calls your function for each row that is parsed
3. Your row handler extracts data from the current row and processes it however you want

### Pseudo-code example

```js
const zsvParser = require('zsv-parser');

let ctx = { rowcount: 0, parser: null };       /* create a context      */

let rowHandler = function(ctx) {               /* define a row handler  */
  ctx.rowcount++;
  let count = zsvParser.cellCount(ctx.parser); /* loop through row data */
  for(let i = 0; i < count; i++) {
    let cell = zsvParser.getCell(ctx.parser, i);
    ...
  }
}

ctx.parser = zsvParser.new(rowHandler, ctx);   /* create a parser       */

do {                                           /* parse data            */
  let chunk = ...;
  myParser.parseBytes(myParser, chunk);
} while(...);

zsvParser.finish(ctx.parser);                  /* finish parsing        */

console.log('Read ' + ctx.rowcount + ' rows'); /* after parsing is done */

zsvParser.delete(ctx.parser);                  /* clean up              */

```

### API

Note: libzsv supports a wide range of options; not all are yet exposed in this
package. Please feel free to post a ticket at https://github.com/liquidaty/zsv/issues
if you'd like to request any feature
that is either unexposed from the underlying library, or does not yet exist in the
underlying library

```
/**
 * Create a parser
 * @param  rowHandler callback that takes a single (optional) argument
 * @return parser
 */
new(rowHandler, ctx)

/**
 * Incrementally parse a chunk of data
 * @param  parser
 * @param  chunk  byte array of data to parse
 * @return 0 on success, non-zero on error
 */
parseBytes(parser, chunk)

/**
 * Get the number of cells in the current row
 * This function should be called from within your rowHandler callback
 * @param  parser
 * @return number of cells in the current row
 */
cellCount(parser)

/**
 * Get the value of a cell in the current row
 * This function should be called from within your rowHandler callback
 * @param  parser
 * @param  index  0-based index (cell position)
 * @return string value of the cell in the specified index position
 */
getCell(parser, index)

/**
 * Finish parsing, after the final `parseBytes()` call has already been made
 * @param  parser
 */
finish(parser)

/**
 * Destroy a parse that was created with `new()`
 * @param  parser
 */
delete(parser)

/**
 * Queue code to run only after the zsv-parser web assembly module has finished loading
 * @param callback function to call after the zsv-parser module has finished loaded
 */
runOnLoad(callback)

/**
 * Abort parsing prematurely, and do not make any more rowHandler calls
 * @param  parser
 */
abort(z)

```

## Contributing
Pull requests are welcome. Please make sure to update tests as appropriate.

## License
[MIT](https://choosealicense.com/licenses/mit/)
