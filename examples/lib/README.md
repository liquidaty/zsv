## Examples using libzsv

This directory contains two simple examples using libzsv:

* [simple.c](simple.c): parse a CSV file and for each row, output the row number,
  the total number of cells and the number of blank cells

* [print_my_column.c](print_my_column.c): parse a CSV file, look for a specified
  column of data, and for each row of data, output only that column

* [push_parser.c](push_parser.c): parse a CSV file, output number of rows.
  This example is provided to show how the parser can be used in a push
  instead of a pull manner via `zsv_parse_bytes()`

## Building

To build, cd to this directory (`cd examples/lib`) and then run `make build`

For further make options, run `make`
