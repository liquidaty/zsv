# Using libzsv

## Building

To build, cd to this directory (`cd examples/lib`) and then run `make build`

For further make options, run `make`

## API overview

### Example

A typical usage conssits of:
- defining row and/or cell handler functions
- connecting an input stream, and
- running the parser

For example, to simply count lines:
```
void count_row(void *ctx) {
  unsigned *count = ctx;
  (*count)++;
}

zsv_parser p = zsv_new(NULL);
if(!p)
  fprintf(stderr, "Out of memory!\n");
else {
  size_t count = 0;
  zsv_set_input(stdin);
  zsv_set_row_handler(p, count_row);
  zsv_set_context(p, &count);
  zsv_finish(p);
  zsv_delete(p);
  printf("Parsed %zu rows\n", count);
}
```

### Input data

Data can be passed into libzsv through any of the following:
1. passing a FILE pointer to read from
2. passing a custom read function (such as `fread` and context pointer (such as `FILE *`))
3. passing a byte array

The first two approaches are advanced by calling `zsv_parse_more()` until EOF or the parse is
cancelled, and have the marginal performance advantage of minimizing memory copying.
In the third approach, the byte array is passed using `zsv_parse_bytes()`.

### Callbacks

Data is passed back to your application through caller-specified callbacks as each cell and/or row
is parsed:
```
void (*cell_handler)(void *ctx, unsigned char *utf8_value, size_t len);

void (*row_handler)(void *ctx);

```

#### Iterating through cells in a row

A common usage pattern is to define a row callback that iterates through
cells in the row using `zsv_cell_count()` and `zsv_get_cell()`.

Using this approach, every parsed cell can be processed,
without specifying a cell handler callback.

For example, we could print every other cell as follows:
```
void my_row_handler(void *ctx) {
  zsv_parser p = ctx;

  /* print every other cell */
  size_t cell_count = zsv_cell_count(p);
  for(size_t i = 0, j = zsv_cell_count(p); i < j; i+=2) {
    /* use zsv_get_cell() to get our cell data */
    struct zsv_cell c = zsv_get_cell(data->parser, i);
    printf("Cell %zu: %.*s\n", c.len, c.str);
  }
}

...
zsv_parser p = zsv_new(NULL);
zsv_set_row_handler(p, my_row_handler);
zsv_set_context(p, &count);
```

### Options
Various options are supported using `struct zsv_opts`, only some of which are described
in this introduction. To see all options, please visit
[zsv/common.h](../../include/zsv/common.h).

#### Delimiter
The `delimiter` option can specify any single-character delimiter other than
newline, form feed or quote. The default value is a comma.

#### Blank leading rows
By default, libzsv skips any leading blank rows, before it makes any callbacks. This behavior
can be disabled by setting the `keep_empty_header_rows` option flag.

#### Maximum number of rows
By default, libzsv assumes a maximum number of columns per table of 1024. This
can be changed by setting the `max_columns` option.

#### Header row span
If your header spans more than one row, you can instruct libzsv to merge header cells
with a single space before it makes its initial row handler call by setting `header_span`
to a number greater than 1. For example, if your input is as follows:
```
Street,Phone,City,Zip
Address,Number,,Code
888 Madison,212-555-1212,New York,10001
```

and you set `header_span` to 2, then in your first row handler call, the cell values
will be `Street Address`, `Phone Number`, `City` and `Zip Code`, after which the
row handler will be called for each single row that is parsed.

## Full examples

This directory contains two simple examples using libzsv:

* [simple.c](simple.c): parse a CSV file and for each row, output the row number,
  the total number of cells and the number of blank cells

* [print_my_column.c](print_my_column.c): parse a CSV file, look for a specified
  column of data, and for each row of data, output only that column

* [parse_by_chunk.c](parse_by_chunk.c): read a CSV file in chunks, parse each
  chunk, and output number of rows. This example
  uses `zsv_parse_bytes()` (whereas the other two examples use `zsv_parse_more()`)
