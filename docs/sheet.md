# `sheet` CSV viewer

zsv's `sheet` command is a lightweight grid/spreadsheet-like terminal viewer
for tabular data, specifically intended to be useful in situations including:
- working within a terminal window, either on the local computer or a remote
  computer
- spreadsheet program such as Excel or OpenOffice is not installed
- you don't want to wait for a heavy program to load (esp with large files)
- working with large files
- working in a memory-constrained environment

## Capabilities inherited from zsv parser
zsv `sheet` uses the zsv parser, and therefore supports:
- Non-comma delimiter
- Multi-row headers
- UTF8 characters
- "Real-world" CSV including edge cases

See [README.md](../README.md) for more about the zsv parser

## Features

|Feature|Supported|Comment|
|--|--|--|
|Operating system compatibility|Yes|Windows, MacOS, Linux, BSD|
|Basic cell navigation|Yes|vim-like or email-like key bindings|
|Pivot tables|Basic 'frequency' table only|Equivalent to SQL "select x, count(1) group by x". Open to considering broader capabilities|
|Large files|Background indexing|Files are indexed in the background so that they can be immediately loaded<br>Vertical navigation is limited to index progress; for large files, navigating to the end of the file may require waiting for indexing to complete|
|Find/filter|Basic (exact-string match)|Regex and sql support are planned|
|Replace (or edit)|Planned|This will be implemented by extending zsv's existing overwrite features|
|Web assembly|Not supported|Need support for both browser+wasm terminal emulator, and threading|
