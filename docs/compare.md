# Tabular file compare

zsv's `compare` command compares multiple tables and outputs differences in tabular or JSON format.

## Background

Examples where multiple tables may contain differences that need to be identified include:
- preliminary vs final data, where a transaction or activity involves a set of resources that may have changed
- transactions involving assets that are known to multiple parties, each with independent systems of record
- predictive model updates where values in a column of outputs may change when a model version changes

## Challenges

Comparing tables of data presents several challenges, some of which are addressed by `zsv compare` and others that are not.

Challenges that `zsv compare` aims to fully solve include:
* Achieve high performance and scalability with bounded memory for pre-sorted input
* Columns might not appear in the same order across all inputs
* Column population may not be the same across all inputs
* Column names might be duplicated within any one input
* Values may have leading whitespace that should be ignored
* Matching might be 1-1, but alternatively might be by one or more 'key' columns
* Row population may not be identical; one or more input(s) may have rows (as identified by 'key' columns)
  that one or more other input(s) do not
* The number of inputs to compare might exceed 2
* Inputs may be large and memory may be limited
* Desired output format may be any of myriad tabular, JSON or other options

Challenges that `zsv compare` aims to solve for limited cases includes:
* Provide an easy built-in option to handle unsorted data with reasonable single-threaded performance

Challenges that `zsv compare` does not try to solve include:
* The name of any given column to compare across inputs might differ across inputs
  (e.g. "My Column X" vs "My_Column_X")
* Exact comparison may be undesirable when content differs cosmetically but not substantively, e.g. in
  scale ("70" vs "0.70"), format ("1/1/2023" vs "2023-01-01"), enumeration ("Washington" vs "WA")
  precision ("5.2499999999999" vs "5.25") and other
* Comparing large, unsorted datasets may require significant time / CPU / memory resources as sort must be performed prior to comparison

(If you are an interested in solutions to these kinds of problems, please contact [Liquidaty](info@liquidaty.com)
and/or check out https://hub.liquidaty.com.)

## Matching and sorting

Row matching and sorting is handled as follows:
* Rows between inputs are matched either by row number or by one or more specified key columns
* Input is assumed to be sorted and uses bounded memory
* Unsorted input can still be processed; will sort using the sqlite3 API

## Performance

No rigorous benchmarking has yet been performed, but preliminary testing yields reasonable performance and memory usage.
Running a comparison of two 40MB CSV files, each a table of 100,000 rows with 61 columns, containing approximately
60,000 differences, took about 5.8 seconds and used a maximum about 1.8MB of RAM on a 2019 MBA.
The same test with sorting used significantly more memory (up to ~40MB) and took about 8 seconds to complete.