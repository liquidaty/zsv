# zsv+lib: the world's fastest (simd) CSV parser, with an extensible CLI

lib + CLI:
[![ci](https://github.com/liquidaty/zsv/actions/workflows/ci.yml/badge.svg)](https://github.com/liquidaty/zsv/actions/workflows/ci.yml)
![GitHub release (latest by date)](https://img.shields.io/github/v/release/liquidaty/zsv?logo=github&style=flat-square)
![GitHub all releases (downloads)](https://img.shields.io/github/downloads/liquidaty/zsv/total?logo=github&style=flat-square)
[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square)](https://github.com/liquidaty/zsv/blob/master/LICENSE)

npm:
[![NPM Version][npm-version-image]][npm-url]
[![NPM Install Size][npm-install-size-image]][npm-install-size-url]

[npm-install-size-image]: https://badgen.net/packagephobia/install/zsv-lib
[npm-install-size-url]: https://packagephobia.com/result?p=zsv-lib
[npm-url]: https://npmjs.org/package/zsv-lib
[npm-version-image]: https://badgen.net/npm/v/zsv-lib

Playground (without `sheet` viewer command): https://liquidaty.github.io/zsv

zsv+lib is a [fast CSV parser](./app/benchmark/README.md) library and extensible command-line utility. It
achieves high performance using SIMD operations, [efficient memory
use](docs/memory.md) and other optimization techniques, and can also parse
generic-delimited and fixed-width formats, as well as multi-row-span headers.

## CLI

The ZSV CLI can be compiled to virtually any target, including
[WebAssembly](examples/js), and offers a variety of [commands](#batteries-included) including `select`, `count`,
direct CSV `sql`, `flatten`, `serialize`, `2json` conversion, `2db` sqlite3
conversion, `stack`, `pretty`, `2tsv`, `compare`, `paste`, `overwrite`,
`check` and more.

The ZSV CLI also includes [`sheet`](docs/sheet.md), an in-console interactive
grid viewer that includes basic navigation, filtering, and pivot table with
drill down, and that supports custom extensions:

<img src="https://github.com/user-attachments/assets/c2ae32a3-48c4-499d-8ef7-7748687bd24f" width="50%">

## Installation

- `brew` (MacOS, Linux):
  - `brew install zsv`
- `winget` (Windows):
  - `winget.exe install zsv`
- `npm` (parser only), `nuget`, `yum`, `apt`, `choco` and more
  - See [INSTALL.md](INSTALL.md)
- Download
  - Pre-built binaries and packages for macOS, Windows, Linux and BSD can be
    downloaded from the [Releases](https://github.com/liquidaty/zsv/releases)
    page.
- Build
  - See [BUILD.md](BUILD.md) to build from source.

## Playground

An [online playground](https://liquidaty.github.io/zsv) is available as well
(without the `sheet` feature due to browser limitations)

If you like zsv+lib, do not forget to give it a star! 🌟

## Performance

Performance results compare favorably vs other CSV utilities (`xsv`,
`tsv-utils`, `csvkit`, `mlr` (miller) etc).

See [benchmarks](./app/benchmark/README.md)

## Which "CSV"

"CSV" is an ambiguous term. This library uses, *by default*, the same definition
as Excel (the library and app have various options to change this default
behavior); a more accurate description of it would be "UTF8 delimited data
parser" insofar as it requires UTF8 input and its options support customization
of the delimiter and whether to allow quoting.

In addition, zsv provides a *row-level* (as well as cell-level) API and provides
"normalized" CSV output (e.g. input of `this"iscell1,"thisis,"cell2` becomes
`"this""iscell1","thisis,cell2"`). Each of these three objectives (Excel
compatibility, row-level API and normalized output) has a measurable performance
impact; conversely, it is possible to achieve-- which a number of other CSV
parsers do-- much faster parsing speeds if any of these requirements (especially
Excel compatibility) are dropped.

### Examples of input that does not comply with RFC 4180

The following is a comprehensive list of all input patterns that are
non-compliant with RFC 4180, and how zsv (by default) parses each:

|Input Description|Parser treatment|Example input|How example input is parsed|
|--|--|--|--|
|Non-ASCII input, UTF8 BOM| BOM at start of the stream is ignored|(0xEF BB BF)|Ignored|
|Non-ASCII input, valid UTF8|Parsed as UTF8|你,好|cell1 = 你, cell2 = 好|
|Non-ASCII input, invalid UTF8|Parsed as UTF8; any non-compliant bytes are retained, or replaced with specified char|aaa,bXb,ccc where Y is malformed UTF8|cell1 = aaa, cell2 = bXb, cell3 = ccc|
|`\n`, `\r`, or `\r\n` newlines|Any non-quote-captured occurrence of `\n`, `\r`, `\r\n` or `\n\r` is parsed as a row end|`1a,1b,1c\n`<br>`2a,2b,2c\r`<br>`3a,3b,3c\n\r`<br>`4a,4b,4c\r\n`<br>`5a,"5\nb",5c\n`<br>`6a,"6b\r","6c"\n`<br>`7a,7b,7c`|Parsed as 7 rows each with 3 cells|
|Unquoted quote|Treated like any other non-delmiter|`aaa,b"bb,ccc`|Cell 2 value is `b"bb`, output as CSV `"b""bb"`|
|Closing quote followed by character other than delimiter (comma) or row end|Treated like any other non-delmiter|`"aa"a,"bb"bb"b,ccc`|Cell 1 value is `aaa`, cell2 value is `bbbb"b`, output as CSV `aaa` and `"bbbb""b"`|
|Missing final CRLF|Ignored; end-of-stream is considered end-of-row if not preceded by explicit row terminator|`aaa,bbb,ccc<EOF>`|Row with 3 cells, same as if input ended with row terminator preceding `EOF`|
|Row and header contain different number of columns (cells)|Number of cells in each row is independent of other rows|`aaa,bbb\n`<br>`aaa,bbb,ccc`|Row 1 = 2 cells; Row 2 = 3 cells|
|Header row contains duplicate cells or embedded newlines|Header rows are parsed the same was as other rows (see NOTE below)|`<BOF>"a\na","a\na"`|Two cells of `a\na`|

The above behavior can be altered with various optional flags:
* Header rows can be treated differently if options are used to skip rows
and/or use multi-row header span -- see documentation for further detail.
* Quote support can be turned off, to treat quotes just like any other non-
  delimiter character
* Cell delimiter can be a character other than comma
* Row delimiter can be specfied as CRLF only, in which case a standalone CR
  or LF is simply part of the cell value, even without quoting

## Built-in and extensible features

`zsv` is an extensible CSV utility, which uses zsvlib, for tasks such as slicing
and dicing, querying with SQL, combining, serializing, flattening,
[converting between CSV/JSON/sqlite3](docs/csv_json_sqlite.md) and more.

`zsv` is streamlined for easy development of custom dynamic extensions.

zsvlib and `zsv` are written in C, but since zsvlib is a library, and `zsv`
extensions are just shared libraries, you can extend `zsv` with your own code in
any programming language, so long as it has been compiled into a shared library
that implements the expected
[interface](./include/zsv/ext/implementation_private.h).

## Key highlights

- Available as BOTH a library and an application (coming soon: standalone
  zsvutil library for common helper functions such as csv writer)
- Open-source, permissively licensed
- Handles real-world CSV the same way that spreadsheet programs do (*including
  edge cases*). Gracefully handles (and can "clean") real-world data that may be
  "dirty".
- Runs on macOS (tested on clang/gcc), Linux (gcc), Windows (mingw), BSD
  (gcc-only) and in-browser (emscripten/wasm)
- High performance (fastest vs all alternatives we've benchmarked)
  [app/benchmark/README.md](app/benchmark/README.md)
- Lightweight: low memory usage (regardless of input data size) and binary size for
  both lib (~30k) and CLI (< 3MB)
- Handles general delimited data (e.g. pipe-delimited) and fixed-width input
  (with specified widths or auto-detected widths), as well as CRLF-only row delims
  with unquoted embedded LF
- Handles multi-row headers
- Handles input from any stream, including caller-defined streams accessed via a
  single caller-defined `fread`-like function
- Easy to use as a library in a few lines of code, via either pull or push
  parsing
- Includes the `zsv` CLI with the following built-in commands:
  - [`sheet`](docs/sheet.md), an in-console interactive and extendable grid viewer
  - `select`, `count`, `sql` query, `desc`ribe, `flatten`, `serialize`, `2json`,
    `2db`, `stack`, `pretty`, `2tsv`, `paste`, `check`, `compare`, `overwrite`,
    `jq`
  - easily [convert between CSV/JSON/sqlite3](docs/csv_json_sqlite.md)
  - [compare multiple files](docs/compare.md)
  - [overwrite cells in files](docs/overwrite.md)
  - [and more](#batteries-included)
- CLI is easy to extend/customize with a few lines of code via modular plug-in
  framework. Just write a few custom functions and compile into a distributable
  DLL that any existing zsv installation can use.

## Why another CSV parser/utility?

Our objectives, which we were unable to find in a pre-existing project, are:

- Reasonably high performance
- Runs on any platform, including web assembly
- Available as both a library and a standalone executable / command-line
  interface utility (CLI)
- Memory-efficient, configurable resource limits
- Handles real-world CSV cases the same way that Excel does, including all edge
  cases (quote handling, newline handling (either `\n` or `\r`), embedded
  newlines, abnormal quoting e.g. aaa"aaa,bbb...)
- Handles other "dirty" data issues:
  - Assumes valid UTF8, but does not misbehave if input contains bad UTF8
  - Option to specify multi-row headers
  - Does not assume or stop working in case of inconsistent numbers of columns
- Easy to use library or extend/customize CLI

There are several excellent tools that achieve high performance. Among those we
considered were xsv and tsv-utils. While they met our performance objective,
both were designed primarily as a utility and not a library, and were not easy
enough, for our needs, to customize and/or to support modular customizations
that could be maintained (or licensed) independently of the related project (in
addition to the fact that they were written in Rust and D, respectively, which
happen to be languages with which we lacked deep experience, especially for web
assembly targeting).

Others we considered were Miller (`mlr`), `csvkit` and Go (csv module), which
did not meet our performance objective. We also considered various other
libraries using SIMD for CSV parsing, but none that we tried met the "real-world
CSV" objective.

Hence, zsv was created as a library and a versatile application, both optimized
for speed and ease of development for extending and/or customizing to your
needs.

## Batteries included

`zsv` comes with several built-in commands:

- [`sheet`](docs/sheet.md): an in-console, interactive grid viewer
- `echo`: read CSV from stdin and write it back out to stdout. This is mostly
  useful for demonstrating how to use the API and also how to create a plug-in,
  and has several uses beyond that including adding/removing BOM, cleaning up
  bad UTF8, whitespace or blank column trimming, limiting output to a contiguous
  data block, skipping leading garbage, and even proving substitution values
  without modifying the underlying source
- `check`: scan for anomolies such as rows with a different number of cells
  than the header row or invalid utf8
- `count`: print the number of rows
- `select`: re-shape CSV by skipping leading garbage, combining header rows into
  a single header, selecting or excluding specified columns, removing duplicate
  columns, sampling, converting from fixed-width input, searching and more
- `desc`: provide a quick description of your table data
- `sql`: treat one or more CSV files like database tables and query with SQL
- `pretty`: format for console (fixed-width) display, or convert to markdown
  format
- `serialize` (inverse of flatten): convert an NxM table to a single 3x (Nx(M-1))
  table with columns: Row, Column Name, Column Value
- `flatten` (inverse of serialize): flatten a table by combining rows that share
  a common value in a specified identifier column
- `2json`: convert CSV to JSON. Optionally, output in
  [database schema](docs/db.schema.json)
- `2tsv`: convert to TSV (tab-delimited) format
- `stack`: merge CSV files vertically
- `paste`: horizontally paste two tables together (given inputs X and Y,
   output 1...N rows where each row contains the entire corresponding
   row in X followed by the entire corresponding row in Y)
- `compare`: compare two or more tables of data and output the differences
- `overwrite`: overwrite a cell value; changes will be reflected in any zsv
  command when the --apply-overwrites option is specified
- `jq`: run a `jq` filter
- `2db`: [convert from JSON to sqlite3 db](docs/csv_json_sqlite.md)
- `prop`: view or save parsing options associated with a file, such as initial
  rows to ignore, or header row span. Saved options are be applied by default
  when processing that file.

Most of these can also be built as an independent executable named `zsv_xxx`
where `xxx` is the command name.

## Running the CLI

After installing, run `zsv help` to see usage details. The typical syntax is
`zsv <command> <parameters>` e.g.:

```shell
zsv sql my_population_data.csv "select * from data where population > 100000"
```

## Using the API

Simple API usage examples include:

### Pull parsing

```c
zsv_parser parser = zsv_new(NULL);
while (zsv_next_row(parser) == zsv_status_row) { // for each row
  // ...
  const size_t cell_count = zsv_cell_count(parser);
  for (size_t i = 0; i < cell_count; i++) { // for each cell
    struct zsv_cell cell = zsv_get_cell(parser, i);
    printf("cell: %.*s\n", cell.len, cell.str);
    // ...
  }
}
```

### Push parsing

```c
static void my_row_handler(void *ctx) {
  zsv_parser parser = ctx;
  const size_t cell_count = zsv_cell_count(parser);
  for (size_t i = 0; i < cell_count; i++) {
    // ...
  }
}

int main() {
  zsv_parser parser = zsv_new(NULL);
  zsv_set_row_handler(parser, my_row_handler);
  zsv_set_context(parser, parser);
  while (zsv_parse_more(parser) == zsv_status_ok);
  return 0;
}
```

Full application code examples can be found at
[examples/lib/README.md](examples/lib/README.md).

An example of using the API, compiled to wasm and called via Javascript, is in
[examples/js/README.md](examples/js/README.md).

For more sophisticated (but at this time, only sporadically
commented/documented) use cases, see the various CLI C source files in the `app`
directory such as `app/serialize.c`.

## Creating your own extension

You can extend `zsv` by providing a pre-compiled shared or static library that
defines the functions specified in `extension_template.h` and which `zsv` loads
in one of three ways:

- as a static library that is statically linked at compile time
- as a dynamic library that is linked at compile time and located in any library
  search path
- as a dynamic library that is located in the same folder as the `zsv`
  executable and loaded at runtime if/as/when the custom mode is invoked

### Example and template

You can build and run a sample extension by running `make test` from
`app/ext_example`.

The easiest way to implement your own extension is to copy and customize the
template files in [app/ext_template](app/ext_template/README.md)

## Possible enhancements and related developments

- optimize search; add search with hyperscan or re2 regex matching, possibly
  parallelize?
- optional OpenMP or other multi-threading for row processing
- auto-generated documentation, and better documentation in general
- Additional benchmarking. Would be great to use
  <https://bitbucket.org/ewanhiggs/csv-game/src/master/> as a springboard to
  benchmarking a number of various tasks
- encoding conversion e.g. UTF16 to UTF8

## Contribute

- [Fork](https://github.com/liquidaty/zsv/fork) the project.
- Check out the latest [`main`](https://github.com/liquidaty/zsv/tree/main)
  branch.
- Create a feature or bugfix branch from `main`.
- Update your required changes.
- Make sure to run `clang-format` (version 15 or later) for C source updates.
- Commit and push your changes.
- Submit the PR.

## License

[MIT](https://github.com/liquidaty/zsv/blob/master/LICENSE)

The zsv CLI uses some permissively-licensed third-party libraries.
See [misc/THIRDPARTY.md](misc/THIRDPARTY.md) for details.
