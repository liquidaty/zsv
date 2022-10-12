## Examples using libzsv

This directory contains two simple examples using libzsv:
* [simple.c](simple.c): parse a CSV file and for each row, output the row number,
  the total number of cells and the number of blank cells
* [print_my_column.c](print_my_column.c): parse a CSV file, look for a specified
  column of data, and for each row of data, output only that column

## Building

To build, cd to this directory (`cd examples/lib`) and then run `make build`

For further make options, run `make`
