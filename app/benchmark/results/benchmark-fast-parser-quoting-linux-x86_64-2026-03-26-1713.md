# CSV Parser Benchmark: Fast Parser Quoting Modes

Date: 2026-03-26
Platform: Linux x86_64
CPU: Intel(R) Xeon(R) Platinum 8488C (single-threaded)
Estimated disk I/O bandwidth: 6.8 GB/s (sequential read)
Data: 500000 rows x 20 columns, best-of-5 iterations

## Tools

| Tool | Version | Notes |
|------|---------|-------|
| zsv legacy | (this repo) | Standard character-by-character parser |
| zsv legacy --parallel | (this repo) | Legacy parser, multi-threaded |
| zsv fast | (this repo) | SIMD parser, prefix-XOR for quoted blocks |
| zsv fast --parallel | (this repo) | SIMD parser, multi-threaded |
| xsv | 0.13.0 | BurntSushi/xsv |
| xan | 0.56.0 | medialab/xan |
| qsv | qsvlite 0.135.0-mimalloc--96-96;150.79 | jqnatividad/qsv (qsvlite) |

## Datasets

| Name | Size | Description |
|------|------|-------------|
| unquoted | ~118 MB | No quote characters anywhere |
| sparse_quoted | ~118 MB | ~2% of cells are standard quoted (with commas) |
| standard_quoted | ~127 MB | ~20% of cells are RFC 4180 quoted |
| nonstandard_quoted | ~127 MB | ~27% of cells have mid-cell quotes in unquoted fields plus standard quoted fields |

## Correctness

N/A = produces incorrect results for this dataset.

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | qsv |
|---------|------|------|------|------|------|------|------|
| unquoted | correct | correct | correct | correct | correct | correct | correct |
| sparse_quoted | correct | correct | correct | correct | correct | correct | correct |
| standard_quoted | correct | correct | correct | correct | correct | correct | correct |
| nonstandard_quoted | correct | correct | **incorrect** | **incorrect** | correct | **incorrect** | correct |
## Results: count (row counting)

### Wall time

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | qsv |
|---------|------|------|------|------|------|------|------|
| unquoted | 0.056s | 0.007s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.021s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.005s** | 0.126s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.026s** | 0.104s |
| sparse_quoted | 0.057s | 0.007s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.020s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.005s** | 0.128s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.031s** | 0.106s |
| standard_quoted | 0.119s | 0.009s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.027s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.006s** | 0.185s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.094s** | 0.167s |
| nonstandard_quoted | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.139s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.009s** | N/A | N/A | 0.190s | N/A | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.171s** |

### Throughput (GB/s)

Disk I/O bandwidth: 6.8 GB/s (sequential read)

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | qsv |
|---------|------|------|------|------|------|------|------|
| unquoted | 2.04 GB/s | 16.33 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **5.44 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **22.86 GB/s** | .90 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **4.39 GB/s** | 1.09 GB/s |
| sparse_quoted | 2.01 GB/s | 16.40 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **5.74 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **22.97 GB/s** | .89 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **3.70 GB/s** | 1.08 GB/s |
| standard_quoted | 1.03 GB/s | 13.68 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **4.56 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **20.52 GB/s** | .66 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **1.31 GB/s** | .73 GB/s |
| nonstandard_quoted | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **.88 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **13.73 GB/s** | N/A | N/A | .65 GB/s | N/A | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **.72 GB/s** |

## Results: select (full parse + CSV output)

### Wall time

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | qsv |
|---------|------|------|------|------|------|------|------|
| unquoted | 0.115s | 0.030s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.113s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.029s** | 0.600s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.082s** | 0.222s |
| sparse_quoted | 0.134s | 0.032s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.116s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.030s** | 0.609s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.085s** | 0.227s |
| standard_quoted | 0.290s | 0.041s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.155s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.032s** | 0.787s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.162s** | 0.338s |
| nonstandard_quoted | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.338s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **0.045s** | N/A | N/A | 0.873s | N/A | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **0.358s** |

### Throughput (GB/s)

Disk I/O bandwidth: 6.8 GB/s (sequential read)

| Dataset | zsv legacy | zsv legacy --parallel | zsv fast | zsv fast --parallel | xsv | xan | qsv |
|---------|------|------|------|------|------|------|------|
| unquoted | .99 GB/s | 3.81 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **1.01 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **3.94 GB/s** | .19 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **1.39 GB/s** | .51 GB/s |
| sparse_quoted | .85 GB/s | 3.58 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **.99 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **3.82 GB/s** | .18 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **1.35 GB/s** | .50 GB/s |
| standard_quoted | .42 GB/s | 3.00 GB/s | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **.79 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **3.84 GB/s** | .15 GB/s | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **.76 GB/s** | .36 GB/s |
| nonstandard_quoted | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **.36 GB/s** | <img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **2.74 GB/s** | N/A | N/A | .14 GB/s | N/A | <img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **.34 GB/s** |
