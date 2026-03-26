# CSV Parser Benchmark: Fast Parser Quoting Modes

Date: 2026-03-26
Platform: Darwin arm64
CPU: Apple M3 Pro (single-threaded)
Estimated disk I/O bandwidth: 10.2 GB/s (sequential read)
Data: 500000 rows x 20 columns, best-of-5 iterations

## Tools

| Tool | Version | Notes |
|------|---------|-------|
| zsv legacy | (this repo) | Standard character-by-character parser |
| zsv legacy --parallel | (this repo) | Legacy parser, multi-threaded |
| zsv fast | (this repo) | SIMD parser, prefix-XOR for quoted blocks |
| zsv fast --parallel | (this repo) | SIMD parser, multi-threaded |
| xsv | 0.13.0 | BurntSushi/xsv |
| xan | 0.54.1 | medialab/xan |
| polars | 1.35.2 | Python polars library (single-threaded, via venv) |
| qsv | qsv 8.1.1-mimalloc | jqnatividad/qsv (qsv) |
| duckdb | v0.10.2 1601d94f94 | Default (multi-threaded, with quote handling) |
| duckdb (QUOTE='') | v0.10.2 1601d94f94 | Multi-threaded, no quote recognition |
| duckdb 1-thread (QUOTE='') | v0.10.2 1601d94f94 | Single-threaded, no quote recognition |

## Datasets

| Name | Size | Description |
|------|------|-------------|
| unquoted | ~118 MB | No quote characters anywhere |
| sparse_quoted | ~118 MB | ~2% of cells are standard quoted (with commas) |
| standard_quoted | ~127 MB | ~20% of cells are RFC 4180 quoted |
| nonstandard_quoted | ~127 MB | ~27% of cells have mid-cell quotes in unquoted fields plus standard quoted fields |

## Correctness

N/A = produces incorrect results for this dataset.

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv | duckdb | duckdb (QUOTE='') | duckdb 1-thread (QUOTE='') |
|---------|------|------|------|------|------|------|------|------|------|------|------|
| unquoted | correct | correct | correct | correct | correct | correct | correct* | correct | correct | correct | correct |
| sparse_quoted | correct | correct | correct | correct | correct | correct | correct* | correct | correct | correct | correct |
| standard_quoted | correct | correct | correct | correct | correct | correct | correct* | correct | correct | correct | correct |
| nonstandard_quoted | correct | correct | **incorrect** | **incorrect** | correct | **incorrect** | **incorrect*** | correct | correct | correct | correct |

\*polars handles mid-cell quotes in unquoted fields but fails on other
nonstandard patterns (e.g. malformed quoted fields like `"bb"bb"b`).

## Results: count (row counting)

### Wall time

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv | duckdb | duckdb (QUOTE='') | duckdb 1-thread (QUOTE='') |
|---------|------|------|------|------|------|------|------|------|------|------|------|
| unquoted | 0.107s | 0.018s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.023s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.008s** | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.087s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.023s** | 0.092s | 0.124s | 0.085s | 0.049s | 0.112s |
| sparse_quoted | 0.109s | 0.020s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.023s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.008s** | 0.092s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.028s** | 0.091s | 0.127s | 0.073s | 0.053s | 0.153s |
| standard_quoted | 0.147s | 0.025s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.026s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.009s** | 0.124s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.082s** | 0.092s | 0.159s | 0.085s | 0.055s | 0.163s |
| nonstandard_quoted | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.158s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.026s** | N/A | N/A | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.129s** | N/A | N/A | 0.160s | 0.084s | 0.056s | 0.163s |

### Throughput (GB/s)

Disk I/O bandwidth: 10.2 GB/s (sequential read)

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv | duckdb | duckdb (QUOTE='') | duckdb 1-thread (QUOTE='') |
|---------|------|------|------|------|------|------|------|------|------|------|------|
| unquoted | 1.06 GB/s | 6.35 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **4.97 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **14.29 GB/s** | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **1.31 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **4.97 GB/s** | 1.24 GB/s | .92 GB/s | 1.34 GB/s | 2.33 GB/s | 1.02 GB/s |
| sparse_quoted | 1.05 GB/s | 5.74 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **4.99 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **14.35 GB/s** | 1.24 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **4.10 GB/s** | 1.26 GB/s | .90 GB/s | 1.57 GB/s | 2.16 GB/s | .75 GB/s |
| standard_quoted | .83 GB/s | 4.92 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **4.73 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **13.68 GB/s** | .99 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **1.50 GB/s** | 1.33 GB/s | .77 GB/s | 1.44 GB/s | 2.23 GB/s | .75 GB/s |
| nonstandard_quoted | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **.78 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **4.75 GB/s** | N/A | N/A | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **.95 GB/s** | N/A | N/A | .77 GB/s | 1.47 GB/s | 2.20 GB/s | .75 GB/s |

## Results: select (full parse + CSV output)

### Wall time

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv | duckdb | duckdb (QUOTE='') | duckdb 1-thread (QUOTE='') |
|---------|------|------|------|------|------|------|------|------|------|------|------|
| unquoted | 0.143s | 0.044s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.097s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.036s** | 0.189s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.077s** | 0.410s | 0.215s | 0.110s | 0.104s | 0.395s |
| sparse_quoted | 0.166s | 0.047s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.105s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.035s** | 0.195s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.081s** | 0.408s | 0.222s | 0.105s | 0.066s | 0.204s |
| standard_quoted | 0.289s | 0.067s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.138s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.038s** | 0.260s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.145s** | 0.426s | 0.278s | 0.130s | 0.071s | 0.216s |
| nonstandard_quoted | 0.324s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.076s** | N/A | N/A | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.288s** | N/A | N/A | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.299s** | 0.139s | N/A | N/A |

### Throughput (GB/s)

Disk I/O bandwidth: 10.2 GB/s (sequential read)

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv | duckdb | duckdb (QUOTE='') | duckdb 1-thread (QUOTE='') |
|---------|------|------|------|------|------|------|------|------|------|------|------|
| unquoted | .79 GB/s | 2.59 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **1.17 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **3.17 GB/s** | .60 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **1.48 GB/s** | .27 GB/s | .53 GB/s | 1.03 GB/s | 1.09 GB/s | .28 GB/s |
| sparse_quoted | .69 GB/s | 2.44 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **1.09 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **3.28 GB/s** | .58 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **1.41 GB/s** | .28 GB/s | .51 GB/s | 1.09 GB/s | 1.74 GB/s | .56 GB/s |
| standard_quoted | .42 GB/s | 1.83 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **.89 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **3.24 GB/s** | .47 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **.84 GB/s** | .28 GB/s | .44 GB/s | .94 GB/s | 1.73 GB/s | .57 GB/s |
| nonstandard_quoted | .38 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **1.62 GB/s** | N/A | N/A | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **.42 GB/s** | N/A | N/A | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **.41 GB/s** | .88 GB/s | N/A | N/A |
