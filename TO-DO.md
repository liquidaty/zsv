# Core Parser

### Performance
- add zsv_opts option to build index while parsing
- add zsv_opts option to use index if available
- add start_row and end_row options to `zsv_opts`

### input formats
- single-registration-function support for additional file suffix support. This functionality could come from either a built-in or an extension
  - for example, `zsv_register_file_reader("bz2", ...)` and `zsvwriter_register_file_writer("bz2", ...)`
  - should also handle file formats that may contain multiple files e.g. multiple tabs in xlsx

# CLI
- add index-related parser options to general command-line options
- change --overwrite-auto to --apply-overwrites
- add `--all` option to `help` which will output all help messages for all commands (including extension commands)

---
## Performance

Row indexing

---

## Sheet

### Key bindings
- align key bindings with vim
- support alternate key binding profiles (emacs, custom1, etc)
- register built-in cmds before extension cmds
- add subcommand invocation by name e.g. emacs style (M-x run-command) or vim style (':' + command)

### Open/edit file
- tab for autocomplete
- multi-tab read and write (e.g. XLSX support via plug-in)
- column operation plug-in (add, remove, modify column)

### Data editing
- add manual edit plug-in
- add view-edits plug-in
- needs to support buffer-level handlers
  - for example:
    - user opens mydata.csv
    - user filters to rows that contain "xyz"; results are displayed in a new buffer
    - user tries to edit cell in the new (filtered data) buffer
      - either this attempt fails because the buffer is read-only, or
      - the buffer handles this in a specific manner to trace the edited row back to the correct row in the original data

### Extensions
- Extend the my_extension.c such that when a buffer in the displayed list is selected, pressing Return will load that buffer
- cell plug-in: display context menu
- update temp file management to centralized list with ref count
- add option to cancel event handling before it is finished running

### Interface
- progress tracking
- title line?
- help

---

## New commands

### ls
- list files that have saved zsv settings (i.e. each subdirectory in .zsv/data)

### audit/dump
- dump all file-specific saved settings (properties, edits/corrections etc)
