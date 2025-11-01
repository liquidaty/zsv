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
- SQL: filter by sql expression
- Large files: quickly opens large files with background indexing after which full file can be navigated
- Pivot: generate pivot tables based on unique values or a user-provided SQL
  expression. Current limitations:
  - only generates a frequency count
  - does not offer custom aggregation columns
  - blocks the UI until the entire file has been processed

Other features under current consideration or plan:
- Reorder/remove column(s) or row(s)
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

