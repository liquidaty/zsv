# CSV Parser Benchmark: Fast Parser Quoting Modes

Date: 2026-03-24
CPU: Intel Xeon E5-2686 v4 @ 2.30GHz (single-threaded)
Data: 500,000 rows x 20 columns, best-of-5 iterations

## Tools

| Tool | Version | Notes |
|------|---------|-------|
| zsv legacy | (this repo) | Standard character-by-character parser |
| zsv fast | (this repo) | SIMD parser, prefix-XOR for quoted blocks |
| zsv fast+MQ | (this repo) | SIMD parser with `--malformed-quoting` (scalar for quoted blocks) |
| xsv | 0.13.0 | BurntSushi/xsv |
| xan | 0.56.0 | medialab/xan |
| polars | 0.9.0 | polars-cli (SQL interface over Polars engine) |

## Datasets

| Name | Size | Description |
|------|------|-------------|
| unquoted | 118 MB | No quote characters anywhere |
| sparse_quoted | 118 MB | ~2% of cells are standard quoted (with commas) |
| standard_quoted | 127 MB | ~20% of cells are RFC 4180 quoted |
| nonstandard_quoted | 127 MB | ~27% of cells have mid-cell quotes in unquoted fields (e.g. `12" monitor`, `say "hello" world`) plus standard quoted fields |

## Correctness

N/A = produces incorrect results for this dataset.

| Dataset | zsv legacy | zsv fast | zsv fast+MQ | xsv | xan | polars |
|---------|-----------|----------|-------------|-----|-----|--------|
| unquoted | correct | correct | correct | correct | correct | correct |
| sparse_quoted | correct | correct | correct | correct | correct | correct |
| standard_quoted | correct | correct | correct | correct | correct | correct |
| nonstandard_quoted | correct | **incorrect** | correct | correct | **incorrect** | correct* |

\*polars handles mid-cell quotes in unquoted fields correctly but fails on other
nonstandard patterns such as malformed quoted fields (e.g. `"bb"bb"b`). Its
correctness on this specific dataset is not generalizable to all nonstandard CSV.

xan returned 250,151 rows instead of 500,000 on nonstandard_quoted data.

## Results: count (row counting)

| Dataset | zsv legacy | zsv fast | zsv fast+MQ | xsv | xan | polars |
|---------|-----------|----------|-------------|-----|-----|--------|
| unquoted | 0.087s | **0.035s** | 0.038s | 0.194s | 0.049s | 0.140s |
| sparse_quoted | 0.093s | **0.036s** | 0.068s | 0.196s | 0.055s | 0.144s |
| standard_quoted | 0.190s | **0.053s** | 0.256s | 0.277s | 0.158s | 0.193s |
| nonstandard_quoted | 0.208s | N/A | 0.284s | 0.279s | N/A | **0.206s** |

## Results: select (full parse + CSV output)

| Dataset | zsv legacy | zsv fast | zsv fast+MQ | xsv | xan | polars |
|---------|-----------|----------|-------------|-----|-----|--------|
| unquoted | 0.423s | 0.414s | 0.426s | 0.875s | **0.142s** | 0.512s |
| sparse_quoted | 0.436s | 0.439s | 0.488s | 0.878s | **0.147s** | 0.511s |
| standard_quoted | 0.623s | 0.655s | 0.911s | 1.131s | **0.267s** | 0.594s |
| nonstandard_quoted | 0.696s | N/A | 1.009s | 1.246s | N/A | **0.661s** |

## Observations

- **zsv fast count** is the fastest row counter across all valid scenarios (0.035-0.053s),
  2.5-3.6x faster than zsv legacy and 4-5.5x faster than xsv.
- **xan select** is remarkably fast for output but fails on nonstandard quoting.
- **polars** handles most data types correctly and is competitive on select, but its
  nonstandard quoting support is partial (see correctness note above).
- **xsv** handles all data types correctly but is consistently the slowest.
- **zsv fast+MQ** trades count speed for nonstandard correctness; still faster than xsv on count.
- zsv fast's advantage comes from the SIMD no-quote path. For heavily quoted data, the
  prefix-XOR path keeps count fast, but select speed is comparable to legacy since
  cell normalization dominates.

## How to reproduce

```sh
# Generate benchmark data
python3 app/benchmark/gen_quoted_test.py  # or use the generation script from the test

# Build zsv without malformed-quoting support (fast = prefix-XOR)
./configure --prefix=/tmp/zsv-no-mq && make -C src install && make -C app install

# Build zsv with malformed-quoting support (fast = scalar for quoted blocks)
./configure --prefix=/tmp/zsv-mq --support-nonstandard-quoting && make -C src install && make -C app install

# Runtime flag also works (any build):
/tmp/zsv-no-mq/bin/zsv count --parser fast --malformed-quoting data.csv

# Benchmark
time /tmp/zsv-no-mq/bin/zsv count --parser fast standard_quoted.csv
time /tmp/zsv-mq/bin/zsv count --parser fast nonstandard_quoted.csv
```
