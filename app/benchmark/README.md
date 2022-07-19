## ZSV Benchmarks

### Summary
* zsv/lib is still in alpha development. Everything here is preliminary
* These benchmarks are enough to be suggestive but not enough to be conclusive. They were run on a limited variety of hardware, OS platforms and build options. YMMV depending on OS, processor and compilation flags/options
* zsv performed faster than all other utilities tested; on our test system (OSX) by ~1.5x-27x, by similar or smaller margins
  (in each case we tested, by at least 20%) on other operating systems
* Four utilities were tested: zsv, xsv, tsv-utils and mlr
* The below figured were based on results from runs on OSX (Intel). Similar results were observed on other operating systems, but in some cases the difference was significantly smaller (for example, zsv
* On most platforms, zsv performed about 2x as fast as xsv, 1.5-2x as fast as tsv-utils, and 25x+ faster than mlr or the python csv-utils family

![image](https://user-images.githubusercontent.com/26302468/146497899-48174114-3b18-49b0-97da-35754ab56e48.png)
![image](https://user-images.githubusercontent.com/26302468/146498211-afc77ce6-4229-4599-bf33-81bf00c725a8.png)

Apple M1 chip (updated 7/3/2022)

`zsv`'s performance advantage when running on the M1 chip is still noticeable, but is narrower:
`count` is about 15-20% faster; `select` is about 25-30% faster. Other operations and comparisons were not tested on this platform.

The main difference in the instructions generated for M1 is the smaller 128bit vector size (see e.g. https://lemire.me/blog/2020/12/13/arm-macbook-vs-intel-macbook-a-simd-benchmark/) and the lack of an M1 `movemask` intrinsic.

### Choice of tests and input data

Two tests, "count" and "select", were chosen to most closely track
raw CSV parsing performance, and to reduce the impact of other
processing tasks (for example, "search" was not tested because that would
primarily measure the performance of the search algorithm rather than
the CSV parser).

We used a range of input data for our internal tests, all of which yielded
results that were consistent with the benchmark tests. For the benchmark
tests, we used the same dataset that xsv uses for its benchmark tests.

Another factor we considered was the impact of I/O overhead. Because the
"count" and "select" operations are relatively fast (compared to, for example,
regular expression matching), it is possible that the entire test may be
I/O bound and that the results might primarily just measuring I/O speed
which could have enough variability to swamp any performance differences
attributable to the particular utility being run. This consideration did
not turn out to be an issue, as the results differed for each utility
by consistent and statistically significant amounts.

### Limitations

These benchmarks are obviously extremely limited. However, we believe they are
sufficient to show the relative performance of zsv as compared to other similar
utilities. While there were statistically significant differences in relative
performance depending on various factors such as the number of columns extracted,
the number of columns per row of data, the average size of each data column,
the frequency of cells that were quoted and/or require quote escaping, and other
various factors.

### Test environment

Below are reported from tests run on OSX (Intel). Similar results were achieved on Windows, Linux and
FreeBSD. See above note for results on M1.

In some cases, especially on Windows, compiler settings had a significant impact.
If you observe results that materially differ, in terms of zsv vs other utility performance,
from what shown below, please let us know.


### Utilities compared:

The following utilities were compared:

* `xsv`: version 0.13.0, installed via brew
* `tsv-utils` (v2.2.1): installed via download of pre-built PGO-optimized binaries
* `mlr` (5.10.2): installed via brew (not shown in graph-- very slow compared to others)
* `zsv` (alpha): built from source using the default `configure` settings
* `csvcut` (1.0.6) (not shown in graph-- very slow compared to others)

### Further notes:

* `tsv-util` using a comma delimiter does *not* handle quoted data,
  unlike xsv (and zsv), and thus its output may be incorrect. For this reason,
  these tests ran tsv-utils both using a custom delimiter, and also on TSV data
  that had been converted from the original CSV data. The performance in either case
  was effectively the same

* `mlr` and `csvcut` are not shown in the graph since their performance was well over 10x slower
  than the others. `mlr` was included in the test was to compare with
  another solution written in the same language (i.e. C) as zsv, since
  tsv-utils, xsv and zsv are all written in different languages, and `csvcut` was
  included since csvcut/csvkit seem to be fairly commonly used for CSV processing

* Our test system shown in the above graph was a pre-M1 OSX MBA.
  We also tested on Linux, BSD
  and Windows. In each case, zsv was the fastest, but in some cases the margin
  was smaller (e.g. 20%+ instead of 50% vs xsv on Win).

### Results in above graph (pre-M1 OSX MBA)

#### count (5 runs, excluding first run)

zsv:  0.076
xsv:  0.151
tsv-utils: 0.150
mlr: not run
csvcut: n/a

#### select (5 runs, excluding first run)
zsv: 0.162
xsv: 0.327
tsv-utils: 0.24
csvcut: 6.88
mlr: 4.53
