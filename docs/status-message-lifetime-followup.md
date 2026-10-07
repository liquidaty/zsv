# Follow-up: give the command-error status messages the same lifetime

Status: not started. Written 2026-10-06, after the change that made buffer status
messages expire (`ZSVSHEET_STATUS_TIMEOUT_MS`, 4000 ms by default) and made
`Ctrl-C` copy a cell to the system clipboard.

## What the user sees today

`sheet` writes a message to a footer line in two different ways, and only one of
them obeys the timeout:

- A **buffer status**: `zsvsheet_ui_buffer_set_status()` /
  `zsvsheet_ui_buffer_take_status()`, re-applied by
  `zsvsheet_check_buffer_updates()` (app/sheet.c) on every pass of the main loop
  and retired when its clock says the timeout has passed. This is what
  `Copied to clipboard`, `Saved <file>`, `(N filtered rows) `, `Not quit: ...`
  and the "(building index) " placeholder use.
- A **display status**: `zsvsheet_priv_set_status()` writes the footer text
  directly, and the top of every main-loop iteration clears it
  (`zsvsheet_priv_set_status(&display_dims, 1, "")`, app/sheet.c). Since
  `getch()` returns every 200 ms under `halfdelay`, such a message lives about a
  fifth of a second, with or without a keystroke.

Measured with `ZSVSHEET_STATUS_TIMEOUT_MS=0` (never expire) and with the default:
`:zzz` shows `Invalid command: zzz` for ~0.2 s and the footer returns to
`? for help`. The messages on this channel are the ones a user most often needs
to read:

- app/sheet.c `Column not found` (find/goto-column)
- app/sheet.c open-file failures (`<file>: No such file or directory`,
  `Not found: <file>`, `Unexpected error`)
- app/sheet.c pattern errors and `Invalid command: <what was typed>`
- app/sheet.c compare-spec errors (`Invalid compare ranges`,
  `Invalid spec. Use A-B v C-D, ...`)
- anything an extension reports through `ext_sheet_set_status` (app/cli.c)

## The change to make

Give the display status its own clock and the same rule, instead of clearing it
unconditionally:

- Add a file-static `display_status_set_ms` beside `zsvsheet_status_text`
  (app/sheet.c) and set it whenever `zsvsheet_priv_set_status()` actually stores
  a new message (both the `overwrite` and the empty-line paths).
- Replace the per-iteration clear at the top of the main loop with the same test
  the buffer path uses:
  `zsvsheet_status_message_expired(display_status_set_ms, zsv_now_ms(), zsvsheet_status_message_timeout_ms())`
  -> clear; otherwise leave the text alone.
- `zsvsheet_status_prefix()` (the `-- EDIT -- ` prefix) and
  `zsvsheet_display_status_text()` need no change: the prefix is re-derived on
  each repaint today and can keep its behaviour by tracking the same clock.
- `display_buffer_subtable()`'s default hints already use `overwrite=0`, so once
  a message expires the usual hint returns on that repaint, exactly as it does
  for buffer statuses today.

Net effect: `Invalid command: zzz` stays for `ZSVSHEET_STATUS_TIMEOUT_MS`
milliseconds like every other message, and the two channels obey one documented
rule. `docs/sheet.md` ("Status line messages") then loses its second paragraph
about errors clearing on the next pass.

## Why it was not done with the lifetime change

It changes many captured screens and is not what that request asked for: the
request was that messages stop sticking forever, and every message already
disappears. The cost is in the test suite:

- app/test/Makefile exports `ZSVSHEET_STATUS_TIMEOUT_MS=0` for the whole suite so
  fixtures can hold a message as long as a stage needs. Under a display-status
  clock, 0 means a command error *never* expires, where today it disappears on
  the next pass. Any stage whose fixture expects the default hint after an
  action that reported an error would then show the error text instead.
- A first pass must therefore either keep the suite-wide 0 only for buffer
  statuses (a second knob, or a per-test value), or regenerate the affected
  fixtures with the default 4000 ms and accept that a stage captured more than
  four seconds after the error will see the hint.
- `test-sheet-copy-no-tool` and the new `test-sheet-status-timeout` already show
  the shape a regression test should take: a session started with a small
  timeout, one stage that asserts the message is up, one that asserts it is gone.

## Test to add

`test-sheet-status-command-error` (tmux): start `sheet` with
`ZSVSHEET_STATUS_TIMEOUT_MS=1500`, enter an invalid command, assert
`Invalid command: ...` is on screen, `sleep 2`, assert the footer is back to
`? for help`; then repeat with a key pressed inside the window to pin that a
keystroke no longer takes it away early. That last stage is the behaviour change
worth arguing about: today any key clears the message, and under this change only
the timeout would.

## Estimate

Half a day including the suite churn: the code is ~15 lines; finding and
regenerating the affected fixtures is the work, and the suite-wide
`ZSVSHEET_STATUS_TIMEOUT_MS=0` export needs a decision (per-channel value or
per-test overrides).
