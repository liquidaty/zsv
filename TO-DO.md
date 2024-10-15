# Core Parser

### Performance
- add zsv_opts option to build index while parsing
- add zsv_opts option to use index if available
- add start_row and end_row options to `zsv_opts`

# CLI
- add index-related parser options to general command-line options

---
## Performance

Row indexing

---

## Sheet

### Key bindings
- align key bindings with vim
- support alternate key binding profiles (emacs, custom1, etc)
- register built-in cmds before extension cmds
- add subcommand invocation by name e.g. M-x run-command x

### Open/edit file
- tab for autocomplete

### Data editing
- add manual edit plug-in
- add view-edits plug-in

### Extensions
- lq extension plug-ins

---

## New commands

### ls
- list files that have saved zsv settings (i.e. each subdirectory in .zsv/data)

### audit/dump
- dump all file-specific saved settings (properties, edits/corrections etc)
