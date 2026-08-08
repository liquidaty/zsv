# `sheet` CSV viewer

zsv's `sheet` command is a lightweight grid/spreadsheet-like terminal viewer
for tabular data, specifically intended to be useful in situations including:

- working within a terminal window, either on the local computer or a remote
  computer
- spreadsheet program such as Excel or OpenOffice is not installed
- you don't want to wait for a heavy program to load (esp with large files)
- working with large files
- working in a memory-constrained environment

## Features

### General

`sheet` is designed for:

- Minimal memory use (regardless of file size)
- Fast response time (regardless of file size)
- Use in any terminal environment on local or remote machine

### Data input / parsing

`sheet` uses the zsv parser and supports its various options:

- Non-comma delimiter
- Multi-row headers
- UTF8 characters
- "Real-world" CSV including edge cases
- Operating system compatibility: Windows, MacOS, Linux, BSD
  - Web assembly is not yet supported (requires better support for terminal
    emulation and threading)

For more about the parser in general, visit the [README.md](../README.md)

### Application

`sheet` features are in an early stage and still have significant room
for improvement (of existing features) and expansion (of new features).

Current features:

- View & navigate: view a tabular data file as a grid and navigate around
- vim-like key bindings
  - emacs-like key bindings are still experimental
  - both vim- and emacs- key bindings can be improved
- Search: find or filter, by literal text or regex (PCRE2 syntax)
- SQL: filter by sql expression
- Sort: by any column (numeric-aware) or by a SQL expression
- Large files: quickly opens large files with background indexing after which
  full file can be navigated
- Pivot: generate pivot tables based on unique values or a user-provided SQL
  expression. Current limitations:
  - only generates a frequency count
  - does not offer custom aggregation columns
  - blocks the UI until the entire file has been processed

Other features under current consideration or plan:

- Command history and basic edit operations (Tab-completion of commands is
  already supported)
- Make status-bar messages persist until the next keypress. Today the main loop
  clears the status on every poll, so an error (a bad regex, an unknown command,
  "Not found") is shown for only about 0.2 seconds
- Accept non-ASCII input at prompts; they currently take single printable bytes,
  so a UTF-8 search term cannot be typed even though the parser handles UTF-8
- Reorder/remove column(s) or row(s)
- Adjustable column width
- Further performance optimizations
  - parallelized find/filter
  - parallelized indexing
  - transformation internal API: return control after fixed # rows processed
    instead of # rows returned
- Tighter integration with [overwrite](overwrite.md) capabilities
- Comparison
- Write-related features (edit, find/replace etc)

#### Comparison

Within sheet, side-by-side columns can be compared and color-coded by typing
`:compare` and then entering column ranges to compare e.g. `1 v 8` or `1-3 v
8-10` or `City1 v City 2` or `Name vs Name`. Compared cells will be color coded
green if equal or red if different.

<img width="881" height="192" alt="image"
src="https://github.com/user-attachments/assets/6a5c185f-ef2e-4782-a658-dca553b2adb9"
/>

## Can't find what you're looking for?

Feel free to suggest new features by creating a new issue.

# Command list

Press `?` to see a list of commands:

| Key(s)         | Action     | Description                                         |
| -------------- | ---------- | --------------------------------------------------- |
| :q             | quit       | Exit the application (also `:quit`)                 |
| <esc>          | escape     | Leave the current view or cancel a…                 |
| ^              | first      | Jump to the first column                            |
| $              | last       | Jump to the last column                             |
| <shift><left>  | first      | Jump to the first column                            |
| <shift><right> | last       | Jump to the last column                             |
| k              | up         | Move up one row                                     |
| j              | down       | Move down one row                                   |
| h              | left       | Move left one column                                |
| l              | right      | Move right one column                               |
| <up>           | up         | Move up one row                                     |
| <down>         | down       | Move down one row                                   |
| <left>         | left       | Move left one column                                |
| <right>        | right      | Move right one column                               |
| <ctrl>d        | pagedown   | Move down one page                                  |
| <ctrl>u        | pageup     | Move up one page                                    |
| <page up>      | pagedown   | Move down one page                                  |
| <page down>    | pageup     | Move up one page                                    |
| g g            | top        | Jump to the first row                               |
| G              | bottom     | Jump to the last row (nG for specific row e.g. 10G) |
| /              | find       | Set a search term and jump to the …                 |
| n              | next       | Jump to the next search result                      |
| \|             | gotocolumn | Find a column by name and jump to the first match   |
| \\             | gotocolumnnext | Jump to the next column matching the find-column term |
| e              | open       | Open another CSV file                               |
| f              | filter     | Filter by specified text                            |
| F              | filtercol  | Filter by specified text only in c…                 |
| :              | subcommand | Editor subcommand                                   |
| ?              | help       | Display a list of actions and key-…                 |
| ^J             | <Enter>    | Follow hyperlink (if any)                           |
| ^M             | <Enter>    | Follow hyperlink (if any)                           |
| v              | pivot      | Group rows by the column under the…                 |
| V              | pivotexpr  | Group rows with group-by SQL expre…                 |
|                | where      | Filter by sql expression                            |
| o              | sort       | Sort rows by the column under the …                 |
| O              | sortdesc   | (alias: sort!) Sort rows…                           |
|                | sortexpr   | Sort rows by SQL expression                         |

# Quick usage guide

The below examples use the following files. Note that noaa.csv has a 2-row header:

```shell
curl -LO 'https://burntsushi.net/stuff/worldcitiespop_mil.csv'
curl -o noaa.csv 'https://data.pmel.noaa.gov/pmel/erddap/tabledap/pmel_co2_moorings_cb2d_135a_c444.csv?station_id,longitude,latitude,time,SST&time>=2024-01-01'

# some examples use tab-delimited data:
zsv 2tsv noaa.csv > noaa.tsv
```

The term "buffer" is used herein to describe data that is loaded into `sheet`
for viewing.

## Loading a CSV file (into a buffer) for viewing

Run `sheet` and load a file:

```shell
zsv sheet worldcitiespop_mil.csv
```

<img width="649" height="480" alt="image"
src="https://github.com/user-attachments/assets/80f11eae-9971-46d1-8d49-5d8607dc81c7"
/>

To load another file for viewing, press `e` and enter the file name at the
prompt, then press Enter.

You can also use the global parser modifiers to modify how the file is parsed
e.g.

```shell
$ head -5 noaa.tsv  # view raw data

station_id	longitude	latitude	time	SST
	degrees_east	degrees_north	UTC	deg C
cce1	-122.51	33.48	2024-01-01T00:17:00Z	15.771
cce1	-122.51	33.48	2024-01-01T03:17:00Z	15.676
cce1	-122.51	33.48	2024-01-01T06:17:00Z	15.655

$ zsv sheet --header-row-span 2 -t noaa.tsv # open in sheet viewer; combine first 2 rows
```

<img width="628" height="292" alt="image"
src="https://github.com/user-attachments/assets/4c21ff11-f7f9-4182-a73d-531c41fa9528"
/>

## Closing a buffer or the application

Press `Esc` to close the current buffer, and type `:q` (or `:quit`) followed by
Enter to close the application. Quitting is deliberately a command rather than a
single keystroke, so it cannot happen by accident.

## Commands and Tab completion

Press `:` to enter a command. Pressing Tab cycles alphabetically through the
commands matching what you have typed so far, wrapping around at the end; with
nothing typed it cycles through every command. For example, `:` then `p` then
Tab yields `pagedown`, `pageup`, `pivot`, `pivotexpr`, and back to `pagedown`.
Editing the text restarts the cycle from the new prefix.

## Navigation

Use arrow keys to move one row or column at a time, or `Shift-right`,
`Shirt-left`, `G` or `g g` to move to the last column, the first column, the
last row or the first row, respectively

## Search syntax

`find` and `filter` take the same search syntax:

| You type   | Meaning                                                          |
| ---------- | ---------------------------------------------------------------- |
| `abc`      | literal: cells containing `abc`                                  |
| `/ab[Cc]`  | regular expression `ab[Cc]` (PCRE2 syntax)                       |
| `/ab[Cc]/` | the same; the closing slash is optional                          |
| `/abc/i`   | regular expression `abc`, case-insensitive                       |
| `\/abc`    | literal: cells containing `/abc`                                  |

A leading slash starts a regular expression. A trailing slash closes it only
when every character after that slash is a flag letter — so `/http://x` is the
one regular expression `http://x`, not `http:/` with flags. `i`
(case-insensitive) is the only flag; an unrecognized letter, including an
uppercase `I`, means the slash was not a delimiter. To match a pattern that
really does end in a slash plus a flag letter, escape the slash: `/a\/i` is the
regular expression `a\/i` and matches `a/i`.

A `\/` escape is recognized only at the very start; elsewhere a backslash is
passed through to the pattern (or matched literally). An empty regular
expression (`/` or `//`) would match every cell, so it is rejected rather than
run.

Note that a regular expression is matched against each cell on its own, so `^`
and `$` anchor to the start and end of a cell.

When entering a search via a command rather than a prompt (`:filter …`), give it
unquoted and without spaces: the command lexer rejects backslash escapes inside
quotes, and splits on spaces. `\/abc` typed at the `f` prompt works; `:filter
"\/abc"` does not.

## Find

Press `/` and enter a search value to find the next matching cell. Press `n` to
find again.

## Filter

Press `f` or `F` to apply a global filter, or a filter on only the current
column, respectively.

A filtered buffer's `Row #` column shows original row positions, including for
`:where` on files with a column named `rowid` (same rule as under Sort below).

For example, running a filter of `/^Dö[nm]` on worldcitiespop_mil.csv:

<img width="823" height="350" alt="image"
src="https://github.com/user-attachments/assets/06389c59-7b14-4435-ba81-5b1da62dbc9d"
/>

## Pivot

## Unique values of current column

Press `v` to generate a pivot table on the unique values of your current column.
For example, after navigating to the Country column in worldcitiespop_mil.csv
and pressing `v`:

<img width="631" height="346" alt="image"
src="https://github.com/user-attachments/assets/a4da2cf7-235c-4a2f-9484-e8b3e4243135"
/>

In the above image, the cursor was moved to the value `ir` because that was the
value under the cursor when the pivot table was generated.

Within the pivot table buffer, you can "drill-down" on any row by navigating to
the row and pressing Enter. For example, from the above image, moving down three
rows to the `je` row with a Count of `18`, then pressing `Enter` opens a new
buffer displaying the underlying 18 rows:

<img width="355" height="425" alt="image"
src="https://github.com/user-attachments/assets/f28118cc-9e31-4c5a-b0f5-75ef8fb2a3d2"
/>

<img width="383" height="422" alt="image"
src="https://github.com/user-attachments/assets/43f1b97f-01d1-4222-a08d-1499f96725df"
/>

A drill-down buffer's `Row #` column shows original row positions, including
for files with a column named `rowid` (same rule as under Sort below).

### Custom values / expression

Press `V` to generate a pivot table based on a custom SQL expression. For
example, after loading worldcitiespop_mil.csv and pressing `V`, then entering
`case when latitude > 35 then '>35' else '<=35' end`:

<img width="643" height="91" alt="image"
src="https://github.com/user-attachments/assets/6379d227-a796-43f9-93bf-8f98d3d0cf48"
/>

## Sort

Press `o` to sort rows ascending by the column under the cursor, or `O` to sort
descending. Sorting opens the sorted data as a new buffer (press `Esc` to return
to the unsorted view); the `Row #` column keeps the original row numbers, so
with the cursor on `Row #` (the default position), `o`/`O` orders by original
row number. The same is available as a command:
`:sort [<column>] [asc|desc] [n|f]`, where `<column>` is matched
case-insensitively (quote names containing spaces, e.g. `:sort "Row #" desc`)
and defaults to the column under the cursor. `:sortdesc` (alias `:sort!`, as in
vim) accepts the same arguments; it only differs in its default direction, so
an explicit `asc` overrides it.

Argument grammar: `asc` and `desc` are reserved words, never column names (a
column literally named `asc` is reachable only via the cursor keys), and the
last one given wins. A token made only of the letters `n`/`f` (any case) names
a column when the column slot is still empty and such a column exists —
leftmost wins — and otherwise sets the sort flags below. So with a column named
`n`, `:sort Country n` sorts Country with integer semantics while
`:sort n Country` is a usage error (`n` took the column slot), and `:sort n n`
is the explicit column-then-flag escape hatch. When flags combine, `f` beats
`n`.

Column sorts without a flag use the following policy:

- Cells that are empty, whitespace-only, or missing (short row) always sort
  last, regardless of direction (spreadsheet convention).
- Numeric-looking cells (after trimming, only chars `[0-9.eE+-]` and at least
  one digit) sort numerically as a group before text (after text when
  descending, i.e. descending is the exact reverse of ascending among
  non-blanks).
- Everything else sorts as text, leading/trailing whitespace ignored, with
  ASCII-only case folding (`COLLATE NOCASE` does not fold non-ASCII; documented
  limitation).
- Locale formats are not parsed: `1,234` and `$50` sort as text, not numbers.

### Numeric sort flags: `n` and `f`

- `n` — leading-integer sort, like vim's `:sort n` except the number must
  start the cell (embedded numbers are not extracted: vim reads `item12` as
  12, here it has no leading number). Cells with no leading number sort first
  when ascending — and last when descending, unlike the blank-cell rule above,
  which holds regardless of direction — in original order either way; the rest
  sort by the integer value of their leading digits (`12abc` reads 12, `3.7` reads 3, `1e3` reads 1, `-5` and `+2` read
  signed). `.5` has no leading digit, so it lands in the no-number group —
  first in original order, not "0" — while `0x10` has a leading digit and
  lands in the integer group with value 0. Integers at or beyond 2^63
  saturate and tie, deterministically in original order.
- `f` — float sort by `CAST` to REAL: token-for-token equal to vim 9.1's
  `:sort f` on ordinary decimal tokens (`-5`, `.5`, `+2`, `3.7`, `10`, `1e3`,
  non-numerics), where every cell without a leading decimal float counts as
  0.0 and sorts inline at 0.0. Divergence from vim on strtod extensions: vim
  parses `0x10` as 16.0, `inf` as infinity and `nan` specially, while here
  all three cast to 0.0. `1e400` overflows to infinity and sorts largest.
- Ties under both flags keep original row order in **both** directions —
  deliberately unlike vim, whose `:sort! n` reverses ties along with
  everything else.
- The status bar distinguishes the flags: `(ascending, integer)` for `n`,
  `(ascending, float)` for `f`.
- `:sortexpr` takes no flags; a trailing junk token simply fails to prepare
  and reports a SQL error in the status bar.

### Stability and chained sorts

Sorts are guaranteed stable: equal-key rows keep their original relative order
(a rowid tiebreak, using whichever rowid spelling the file's columns do not
shadow — see the note below — SQLite's sorter happens to behave stably in
every configuration `sheet` can reach, but that behavior is undocumented, so
it is pinned explicitly). Because a sorted buffer's row identity *is* the previous
sort's row order, sorting a sorted buffer is a true secondary sort: sort by A,
then by B, and rows tied on B stay in A-order.

Notes and limitations:

- If the file has duplicate header names, `:sort` takes the deduplicated name
  as shown by SQL (e.g. `a_2` for the second `a`), matching `:where` behavior.
- Row numbers and the stability tiebreak are positional even when the file has
  a column literally named `rowid` (or `_rowid_`/`oid`, any case): the implicit
  SQLite rowid is referenced by whichever of its three spellings the file's
  columns do not shadow. Only a file declaring all three at once falls back to
  the shadowed `rowid` column — deterministic, but no longer positional.
- Each sort writes a full-size temporary copy of the data (deleted when its
  buffer closes) and blocks the UI until complete, like `:where` and pivot.

### Sort by SQL expression

`:sortexpr <SQL expression> [asc|desc]` sorts by an arbitrary SQL expression,
e.g. `:sortexpr length(City) desc`. Unlike column sorts, the direction applies
to the raw expression only — the blank-last/numeric-first policy above is *not*
applied, since the expression itself defines the ordering. When typed with
arguments after `:sortexpr`, the command lexer consumes `"` quoting and
collapses whitespace runs, so double-quoted identifiers and doubled spaces
inside `'...'` string literals do not survive; enter the expression at the bare
`:sortexpr` prompt for those (single quotes are not special to the lexer, so
`'New York'` survives either way).

## Viewing / clearing errors

If any parsing errors occur, the status bar will indicate with a message `? for
help, :errors for errors`. Entering the command `:errors` will show the list of
errors, and entering the command `:errors-clear` will clear the errors and reset
the status bar.
