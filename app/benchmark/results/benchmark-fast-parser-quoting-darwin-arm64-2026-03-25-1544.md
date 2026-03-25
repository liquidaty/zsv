# CSV Parser Benchmark: Fast Parser Quoting Modes

Date: 2026-03-25
Platform: Darwin arm64
CPU: Apple M3 Pro (single-threaded)
Estimated disk I/O bandwidth: 11.2 GB/s (sequential read)
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

## Datasets

| Name | Size | Description |
|------|------|-------------|
| unquoted | ~118 MB | No quote characters anywhere |
| sparse_quoted | ~118 MB | ~2% of cells are standard quoted (with commas) |
| standard_quoted | ~127 MB | ~20% of cells are RFC 4180 quoted |
| nonstandard_quoted | ~127 MB | ~27% of cells have mid-cell quotes in unquoted fields plus standard quoted fields |

## Correctness

N/A = produces incorrect results for this dataset.

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|
| unquoted | correct | correct | correct | correct | correct | correct | correct* | correct |
| sparse_quoted | correct | correct | correct | correct | correct | correct | correct* | correct |
| standard_quoted | correct | correct | correct | correct | correct | correct | correct* | correct |
| nonstandard_quoted | correct | correct | **incorrect** | **incorrect** | correct | **incorrect** | **incorrect*** | correct |

\*polars handles mid-cell quotes in unquoted fields but fails on other
nonstandard patterns (e.g. malformed quoted fields like `"bb"bb"b`).

## Results: count (row counting)

### Wall time

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|
| unquoted | 0.106s | 0.020s | 0.023s | 0.008s | 0.088s | 0.023s | 0.088s | 0.123s |
| sparse_quoted | 0.109s | 0.020s | 0.023s | 0.008s | 0.090s | 0.028s | 0.089s | 0.126s |
| standard_quoted | 0.142s | 0.029s | 0.025s | 0.010s | 0.122s | 0.080s | 0.090s | 0.171s |
| nonstandard_quoted | 0.153s | 0.026s | N/A | N/A | 0.125s | N/A | N/A | 0.159s |

### Throughput (GB/s)

Disk I/O bandwidth: 11.2 GB/s (sequential read)

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|
| unquoted | 1.07 GB/s | 5.71 GB/s | 4.97 GB/s | 14.29 GB/s | 1.29 GB/s | 4.97 GB/s | 1.29 GB/s | .92 GB/s |
| sparse_quoted | 1.05 GB/s | 5.74 GB/s | 4.99 GB/s | 14.35 GB/s | 1.27 GB/s | 4.10 GB/s | 1.29 GB/s | .91 GB/s |
| standard_quoted | .86 GB/s | 4.24 GB/s | 4.92 GB/s | 12.31 GB/s | 1.00 GB/s | 1.53 GB/s | 1.36 GB/s | .72 GB/s |
| nonstandard_quoted | .80 GB/s | 4.75 GB/s | N/A | N/A | .98 GB/s | N/A | N/A | .77 GB/s |

## Results: select (full parse + CSV output)

### Wall time

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|
| unquoted | 0.142s | 0.041s | 0.095s | 0.035s | 0.182s | 0.075s | 0.394s | 0.210s |
| sparse_quoted | 0.164s | 0.048s | 0.102s | 0.032s | 0.192s | 0.079s | 0.395s | 0.216s |
| standard_quoted | 0.279s | 0.072s | 0.136s | 0.040s | 0.255s | 0.142s | 0.422s | 0.273s |
| nonstandard_quoted | 0.315s | 0.077s | N/A | N/A | 0.278s | N/A | N/A | 0.294s |

### Throughput (GB/s)

Disk I/O bandwidth: 11.2 GB/s (sequential read)

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|
| unquoted | .80 GB/s | 2.78 GB/s | 1.20 GB/s | 3.26 GB/s | .62 GB/s | 1.52 GB/s | .29 GB/s | .54 GB/s |
| sparse_quoted | .70 GB/s | 2.39 GB/s | 1.12 GB/s | 3.58 GB/s | .59 GB/s | 1.45 GB/s | .29 GB/s | .53 GB/s |
| standard_quoted | .44 GB/s | 1.71 GB/s | .90 GB/s | 3.07 GB/s | .48 GB/s | .86 GB/s | .29 GB/s | .45 GB/s |
| nonstandard_quoted | .39 GB/s | 1.60 GB/s | N/A | N/A | .44 GB/s | N/A | N/A | .42 GB/s |
