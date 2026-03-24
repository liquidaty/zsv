# CSV Parser Benchmark: Fast Parser Quoting Modes

Date: 2026-03-24
Platform: Linux x86_64
CPU: Intel(R) Xeon(R) CPU E5-2686 v4 @ 2.30GHz (single-threaded)
Estimated sequential read bandwidth: 22.7 GB/s (single-core, from `dd if=/dev/zero`)
Data: 500000 rows x 20 columns, best-of-5 iterations

## Tools

| Tool | Version | Notes |
|------|---------|-------|
| zsv legacy | (this repo) | Standard character-by-character parser |
| zsv legacy --parallel | (this repo) | Legacy parser, multi-threaded |
| zsv fast | (this repo) | SIMD parser, prefix-XOR for quoted blocks |
| zsv fast --parallel | (this repo) | SIMD parser, multi-threaded |
| zsv fast+MQ | (this repo) | SIMD parser with `--malformed-quoting` (scalar for quoted blocks) |
| xsv | 0.13.0 | BurntSushi/xsv |
| xan | 0.56.0 | medialab/xan |
| polars | polars-cli 0.9.0 | polars-cli (SQL interface over Polars engine) |
| qsv | qsvlite 0.135.0-mimalloc--4-4;12.49 GiB-0 B-14.29 GiB-15.62 GiB (Unknown_target compiled with Rust 1.81) installed | jqnatividad/qsv (lite) |

## Datasets

| Name | Size | Description |
|------|------|-------------|
| unquoted | ~118 MB | No quote characters anywhere |
| sparse_quoted | ~118 MB | ~2% of cells are standard quoted (with commas) |
| standard_quoted | ~127 MB | ~20% of cells are RFC 4180 quoted |
| nonstandard_quoted | ~127 MB | ~27% of cells have mid-cell quotes in unquoted fields plus standard quoted fields |

## Correctness

N/A = produces incorrect results for this dataset.

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | zsv fast+MQ | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|------|
| unquoted | correct | correct | correct | correct | correct | correct | correct | correct* | correct |
| sparse_quoted | correct | correct | correct | correct | correct | correct | correct | correct* | correct |
| standard_quoted | correct | correct | correct | correct | correct | correct | correct | correct* | correct |
| nonstandard_quoted | correct | correct | **incorrect** | **incorrect** | correct | correct | **incorrect** | correct* | correct |

\*polars handles mid-cell quotes in unquoted fields but fails on other
nonstandard patterns (e.g. malformed quoted fields like `"bb"bb"b`).

## Results: count (row counting)

### Wall time

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | zsv fast+MQ | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|------|
| unquoted | 0.090s | 0.027s | 0.035s | 0.023s | 0.040s | 0.193s | 0.047s | 0.142s | 0.156s |
| sparse_quoted | 0.095s | 0.030s | 0.040s | 0.028s | 0.092s | 0.197s | 0.058s | 0.145s | 0.160s |
| standard_quoted | 0.190s | 0.059s | 0.055s | 0.061s | 0.365s | 0.277s | 0.155s | 0.193s | 0.254s |
| nonstandard_quoted | 0.213s | 0.065s | N/A | N/A | 0.410s | 0.280s | N/A | 0.205s | 0.259s |

### Throughput (GB/s)

Hardware sequential read bandwidth: 22.7 GB/s

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | zsv fast+MQ | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|------|
| unquoted | 1.27 GB/s | 4.23 GB/s | 3.26 GB/s | 4.97 GB/s | 2.85 GB/s | .59 GB/s | 2.43 GB/s | .80 GB/s | .73 GB/s |
| sparse_quoted | 1.20 GB/s | 3.82 GB/s | 2.87 GB/s | 4.10 GB/s | 1.24 GB/s | .58 GB/s | 1.98 GB/s | .79 GB/s | .71 GB/s |
| standard_quoted | .64 GB/s | 2.08 GB/s | 2.23 GB/s | 2.01 GB/s | .33 GB/s | .44 GB/s | .79 GB/s | .63 GB/s | .48 GB/s |
| nonstandard_quoted | .58 GB/s | 1.90 GB/s | N/A | N/A | .30 GB/s | .44 GB/s | N/A | .60 GB/s | .47 GB/s |

## Results: select (full parse + CSV output)

### Wall time

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | zsv fast+MQ | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|------|
| unquoted | 0.403s | 0.241s | 0.394s | 0.619s | 0.406s | 0.879s | 0.144s | 0.507s | 0.360s |
| sparse_quoted | 0.416s | 0.248s | 0.419s | 0.623s | 0.470s | 0.881s | 0.150s | 0.511s | 0.366s |
| standard_quoted | 0.608s | 0.322s | 0.641s | 0.644s | 0.918s | 1.133s | 0.267s | 0.605s | 0.541s |
| nonstandard_quoted | 0.675s | 0.339s | N/A | N/A | 1.014s | 1.245s | N/A | 0.660s | 0.576s |

### Throughput (GB/s)

Hardware sequential read bandwidth: 22.7 GB/s

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | zsv fast+MQ | xsv | xan | polars | qsv |
|---------|------|------|------|------|------|------|------|------|------|
| unquoted | .28 GB/s | .47 GB/s | .29 GB/s | .18 GB/s | .28 GB/s | .13 GB/s | .79 GB/s | .22 GB/s | .31 GB/s |
| sparse_quoted | .27 GB/s | .46 GB/s | .27 GB/s | .18 GB/s | .24 GB/s | .13 GB/s | .76 GB/s | .22 GB/s | .31 GB/s |
| standard_quoted | .20 GB/s | .38 GB/s | .19 GB/s | .19 GB/s | .13 GB/s | .10 GB/s | .46 GB/s | .20 GB/s | .22 GB/s |
| nonstandard_quoted | .18 GB/s | .36 GB/s | N/A | N/A | .12 GB/s | .09 GB/s | N/A | .18 GB/s | .21 GB/s |
