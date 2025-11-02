# `sheet` CSV viewer

zsv's `sheet` command is a lightweight grid/spreadsheet-like terminal viewer
for tabular data, specifically intended to be useful in situations including:
- working within a terminal window, either on the local computer or a remote
  computer
- spreadsheet program such as Excel or OpenOffice is not installed
- you don't want to wait for a heavy program to load (esp with large files)
- working with large files
- working in a memory-constrained environment

# Features

## General

`sheet` is designed for:
- Minimal memory use (regardless of file size)
- Fast response time (regardless of file size)
- Use in any terminal environment on local or remote machine

## Data input / parsing

`sheet` uses the zsv parser and supports its various options:
- Non-comma delimiter
- Multi-row headers
- UTF8 characters
- "Real-world" CSV including edge cases
- Operating system compatibility: Windows, MacOS, Linux, BSD
  - Web assembly is not yet supported (requires better support for terminal emulation and threading)

For more about the parser in general, visit the [README.md](../README.md)

## Application features

`sheet` features are in an early stage and still have significant room
for improvement (of existing features) and expansion (of new features).

Current features:
- View & navigate: view a tabular data file as a grid and navigate around
- vim-like key bindings
  - emacs-like key bindings are still experimental
  - both vim- and emacs- key bindings can be improved
- Search: find or filter, by literal text or regex (PCRE2 syntax)
  - Currently, regex only supported in filter (will soon be supported via find)
- SQL: filter by sql expression
- Large files: quickly opens large files with background indexing after which full file can be navigated
- Pivot: generate pivot tables based on unique values or a user-provided SQL
  expression. Current limitations:
  - only generates a frequency count
  - does not offer custom aggregation columns
  - blocks the UI until the entire file has been processed

Other features under current consideration or plan:
- Add regex option to `find`
- Make it harder to accidentally quit (Ctrl-Q or Shift-Q or :q instead of just q?)
- Command history, autocomplete, basic edit operations etc
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

## Can't find what you're looking for?

Feel free to suggest new features by creating a new issue.

# Command list

Press `?` to see a list of commands:

|Key(s)        |Action    |Description                        |
|--------------|----------|-----------------------------------|
|q             |quit      |Exit the application               |
|<esc>         |escape    |Leave the current view or cancel a…|
|^             |first     |Jump to the first column           |
|$             |last      |Jump to the last column            |
|<shift><left> |first     |Jump to the first column           |
|<shift><right>|last      |Jump to the last column            |
|k             |up        |Move up one row                    |
|j             |down      |Move down one row                  |
|h             |left      |Move left one column               |
|l             |right     |Move right one column              |
|<up>          |up        |Move up one row                    |
|<down>        |down      |Move down one row                  |
|<left>        |left      |Move left one column               |
|<right>       |right     |Move right one column              |
|<ctrl>d       |pagedown  |Move down one page                 |
|<ctrl>u       |pageup    |Move up one page                   |
|<page up>     |pagedown  |Move down one page                 |
|<page down>   |pageup    |Move up one page                   |
|g g           |top       |Jump to the first row              |
|G             |bottom    |Jump to the last row               |
|/             |find      |Set a search term and jump to the …|
|n             |next      |Jump to the next search result     |
|\|             |gotocolumn|Go to column                       |
|e             |open      |Open another CSV file              |
|f             |filter    |Filter by specified text           |
|F             |filtercol |Filter by specified text only in c…|
|:             |subcommand|Editor subcommand                  |
|?             |help      |Display a list of actions and key-…|
|^J            |<Enter>   |Follow hyperlink (if any)          |
|^M            |<Enter>   |Follow hyperlink (if any)          |
|v             |pivot     |Group rows by the column under the…|
|V             |pivotexpr |Group rows with group-by SQL expre…|
|              |where     |Filter by sql expression           |

# Quick usage guide

The below examples use the following files. Note that noaa.csv has a 2-row header:
```
curl -LO 'https://burntsushi.net/stuff/worldcitiespop_mil.csv'
curl -o noaa.csv 'https://data.pmel.noaa.gov/pmel/erddap/tabledap/pmel_co2_moorings_cb2d_135a_c444.csv?station_id,longitude,latitude,time,SST&time>=2024-01-01'

# some examples use tab-delimited data:
zsv 2tsv noaa.csv > noaa.tsv
```

The term "buffer" is used herein to describe data that is loaded into `sheet` for viewing.

## Loading a CSV file (into a buffer) for viewing

Run `sheet` and load a file:

`zsv sheet worldcitiespop_mil.csv`

<img width="649" height="480" alt="image" src="https://github.com/user-attachments/assets/80f11eae-9971-46d1-8d49-5d8607dc81c7" />

To load another file for viewing, press `e` and enter the file name at the prompt, then press Enter.

You can also use the global parser modifiers to modify how the file is parsed e.g.

```
> head -5 noaa.tsv  # view raw data

station_id	longitude	latitude	time	SST
	degrees_east	degrees_north	UTC	deg C
cce1	-122.51	33.48	2024-01-01T00:17:00Z	15.771
cce1	-122.51	33.48	2024-01-01T03:17:00Z	15.676
cce1	-122.51	33.48	2024-01-01T06:17:00Z	15.655

> zsv sheet --header-row-span 2 -t noaa.tsv # open in sheet viewer; combine first 2 rows
```
<img width="628" height="292" alt="image" src="https://github.com/user-attachments/assets/4c21ff11-f7f9-4182-a73d-531c41fa9528" />

## Closing a buffer or the application

Press `Esc` to close the current buffer, and `q` to close the application

## Navigation

Use arrow keys to move one row or column at a time, or `Shift-right`, `Shirt-left`, `G` or `g g` to move
to the last column, the first column, the last row or the first row, respectively

## Find (exact+contains)

Press `/` and enter some test to find the next cell containing that exact text. Press `n` to find again

## Filter (exact+contains or regex)

Press `f` or `F` to apply a global filter, or a filter on only the current column, respectively. If the search value
starts with a slash (`/`), the string following the slash is treated as a regular expression.

For example, running a filter of `/^Dö[nm]` on worldcitiespop_mil.csv:

<img width="823" height="350" alt="image" src="https://github.com/user-attachments/assets/06389c59-7b14-4435-ba81-5b1da62dbc9d" />


## Pivot

## Unique values of current column

Press `v` to generate a pivot table on the unique values of your current column. For example, after navigating to the Country
column in worldcitiespop_mil.csv and pressing `v`:

<img width="631" height="346" alt="image" src="https://github.com/user-attachments/assets/a4da2cf7-235c-4a2f-9484-e8b3e4243135" />

In the above image, the cursor was moved to the value `ir` because that was the value under the cursor when the pivot table
was generated.

Within the pivot table buffer, you can "drill-down" on any row by navigating to the row and pressing Enter. For example,
from the above image, moving down three rows to the `je` row with a Count of `18`, then pressing `Enter` opens
a new buffer displaying the underlying 18 rows:

<img width="355" height="425" alt="image" src="https://github.com/user-attachments/assets/f28118cc-9e31-4c5a-b0f5-75ef8fb2a3d2" />

<img width="383" height="422" alt="image" src="https://github.com/user-attachments/assets/43f1b97f-01d1-4222-a08d-1499f96725df" />


### Custom values / expression

Press `V` to generate a pivot table based on a custom SQL expression. For example, after loading worldcitiespop_mil.csv and
pressing `V`, then entering `case when latitude > 35 then '>35' else '<=35' end`:
<img width="643" height="91" alt="image" src="https://github.com/user-attachments/assets/6379d227-a796-43f9-93bf-8f98d3d0cf48" />

## Viewing / clearing errors

If any parsing errors occur, the status bar will indicate with a message `? for help, :errors for errors`. Entering the command `:errors` will show the list of errors, and entering the command `:errors-clear` will clear the errors and reset the status bar.
