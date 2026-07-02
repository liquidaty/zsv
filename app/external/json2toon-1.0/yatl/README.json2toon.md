# Vendored yatl (TOON SAX parser)

This is a vendored snapshot of the [`yatl`](file:///Users/lqbot/src/yatl) library
(`yatl` = "yet another TOON library"): a streaming, event-driven (SAX) parser for
TOON whose public API deliberately mirrors YAJL's. It is used by json2toon's
reverse path (TOON → JSON) as its **sole** TOON parser, exactly as the vendored
YAJL is the sole JSON parser for the forward path: the TOON SAX callbacks drive
the `jsonwriter` push API, which owns every JSON concern (brackets, separators,
key colons, string/number escaping).

Vendoring (rather than linking a system `libyatl`) keeps cross targets (i686,
sbx, mingw, emscripten) self-contained, mirroring the yajl / toonwriter handling.

- Source version: **yatl 1.0.0**, commit `a50eac8` (includes the `yatl_max_depth`
  option; escape set strict to TOON §7.1). MIT; see `LICENSE`.
- Compiled: `yatl.c`, `yatl_parser.c`, `yatl_buf.c`, `yatl_encode.c`,
  `yatl_alloc.c`, `yatl_version.c`.
- Internal headers: `yatl_parser.h`, `yatl_buf.h`, `yatl_encode.h`,
  `yatl_alloc.h`.
- Public headers (`yatl/`): `yatl_common.h`, `yatl_parse.h`, `yatl_version.h`.

## Include layout

The library's sources reference public headers as `#include <yatl/...>` and
internal headers as `#include "..."`. The verbatim public headers therefore live
under `yatl/`, and the sources plus internal headers live flat in this directory.
Build with `-Ithird_party/yatl` so the angle includes resolve through `yatl/`
while the quoted includes resolve next to the `.c` files. (No shim directory is
needed — unlike yajl, yatl never uses the `"api/..."` include form.)

## Symbol visibility

The whole library is compiled with `-w -fvisibility=hidden`; the shared object's
export filter keeps only `json2toon_*` / `toon2json_*`, so every `yatl_*` symbol
stays internal to `libjson2toon` (the `YATL_API` default-visibility annotation in
the headers is neutralized the same way the vendored yajl's is).

## To update

Re-copy the files listed above from the yatl repo and bump the version note here.
Do not edit them in place — changes belong upstream in yatl (patch the canonical
repo, verify its own `make test` / `make test ASAN=1`, then re-vendor).
