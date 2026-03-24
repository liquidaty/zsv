# CSV Parser Benchmark: Fast Parser Quoting Modes

Date: 2026-03-24
Platform: Linux x86_64
CPU: Intel(R) Xeon(R) CPU E5-2686 v4 @ 2.30GHz (single-threaded)
Estimated sequential read bandwidth: 25.0 GB/s (single-core, from `dd if=/dev/zero`)
Data: 500000 rows x 20 columns, best-of-5 iterations

## Tools

| Tool | Version | Notes |
|------|---------|-------|
| zsv legacy | (this repo) | Standard character-by-character parser |
| zsv fast | (this repo) | SIMD parser, prefix-XOR for quoted blocks |
| zsv fast+MQ | (this repo) | SIMD parser with `--malformed-quoting` (scalar for quoted blocks) |
| xsv | 0.13.0 | BurntSushi/xsv |
| xan | 0.56.0 | medialab/xan |
| polars | polars-cli 0.9.0 | polars-cli (SQL interface over Polars engine) |

## Datasets

| Name | Size | Description |
|------|------|-------------|
| unquoted | ~118 MB | No quote characters anywhere |
| sparse_quoted | ~118 MB | ~2% of cells are standard quoted (with commas) |
| standard_quoted | ~127 MB | ~20% of cells are RFC 4180 quoted |
| nonstandard_quoted | ~127 MB | ~27% of cells have mid-cell quotes in unquoted fields plus standard quoted fields |

## Correctness

N/A = produces incorrect results for this dataset.

| Dataset | zsv legacy | zsv fast | zsv fast+MQ | xsv | xan | polars |
|---------|------|------|------|------|------|------|
| unquoted | correct | correct | correct | correct | correct | correct* |
| sparse_quoted | correct | correct | correct | correct | correct | correct* |
| standard_quoted | correct | correct | correct | correct | correct | correct* |
| nonstandard_quoted | correct | **incorrect** | correct | correct | **incorrect** | correct* |

\*polars handles mid-cell quotes in unquoted fields but fails on other
nonstandard patterns (e.g. malformed quoted fields like `"bb"bb"b`).

## Results: count (row counting)

### Wall time

| Dataset | zsv legacy | zsv fast | zsv fast+MQ | xsv | xan | polars |
|---------|------|------|------|------|------|------|
| unquoted | 0.087s | 0.034s | 0.038s | 0.195s | 0.047s | 0.140s |
| sparse_quoted | 0.093s | 0.039s | 0.091s | 0.187s | 0.057s | 0.146s |
| standard_quoted | 0.185s | 0.055s | 0.364s | 0.278s | 0.157s | 0.192s |
| nonstandard_quoted | 0.206s | N/A | 0.411s | 0.281s | N/A | 0.205s |

### Throughput (GB/s)

Hardware sequential read bandwidth: 25.0 GB/s

| Dataset | zsv legacy | zsv fast | zsv fast+MQ | xsv | xan | polars |
|---------|------|------|------|------|------|------|
| unquoted | 1.31 GB/s (5% of hw max) | 3.36 GB/s (13% of hw max) | 3.00 GB/s (12% of hw max) | .58 GB/s (2% of hw max) | 2.43 GB/s (9% of hw max) | .81 GB/s (3% of hw max) |
| sparse_quoted | 1.23 GB/s (4% of hw max) | 2.94 GB/s (11% of hw max) | 1.26 GB/s (5% of hw max) | .61 GB/s (2% of hw max) | 2.01 GB/s (8% of hw max) | .78 GB/s (3% of hw max) |
| standard_quoted | .66 GB/s (2% of hw max) | 2.23 GB/s (8% of hw max) | .33 GB/s (1% of hw max) | .44 GB/s (1% of hw max) | .78 GB/s (3% of hw max) | .64 GB/s (2% of hw max) |
| nonstandard_quoted | .59 GB/s (2% of hw max) | N/A | .30 GB/s (1% of hw max) | .43 GB/s (1% of hw max) | N/A | .60 GB/s (2% of hw max) |

## Results: select (full parse + CSV output)

### Wall time

| Dataset | zsv legacy | zsv fast | zsv fast+MQ | xsv | xan | polars |
|---------|------|------|------|------|------|------|
| unquoted | 0.399s | 0.393s | 0.406s | 0.873s | 0.141s | 0.511s |
| sparse_quoted | 0.409s | 0.418s | 0.471s | 0.881s | 0.147s | 0.501s |
| standard_quoted | 0.607s | 0.642s | 0.922s | 1.132s | 0.262s | 0.605s |
| nonstandard_quoted | 0.676s | N/A | 1.007s | 1.246s | N/A | 0.672s |

### Throughput (GB/s)

Hardware sequential read bandwidth: 25.0 GB/s

| Dataset | zsv legacy | zsv fast | zsv fast+MQ | xsv | xan | polars |
|---------|------|------|------|------|------|------|
| unquoted | .28 GB/s (1% of hw max) | .29 GB/s (1% of hw max) | .28 GB/s (1% of hw max) | .13 GB/s (0% of hw max) | .81 GB/s (3% of hw max) | .22 GB/s (0% of hw max) |
| sparse_quoted | .28 GB/s (1% of hw max) | .27 GB/s (1% of hw max) | .24 GB/s (0% of hw max) | .13 GB/s (0% of hw max) | .78 GB/s (3% of hw max) | .22 GB/s (0% of hw max) |
| standard_quoted | .20 GB/s (0% of hw max) | .19 GB/s (0% of hw max) | .13 GB/s (0% of hw max) | .10 GB/s (0% of hw max) | .47 GB/s (1% of hw max) | .20 GB/s (0% of hw max) |
| nonstandard_quoted | .18 GB/s (0% of hw max) | N/A | .12 GB/s (0% of hw max) | .09 GB/s (0% of hw max) | N/A | .18 GB/s (0% of hw max) |
