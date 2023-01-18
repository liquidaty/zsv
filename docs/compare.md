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
* Comparing multiple tables currently is somewhat achievable with built-in utilities such as `awk`,
  but requires different solutions for different operating systems and platforms, and typically requires custom
  scripting depending on input-specific situations. Searching for "table comparison" on stackoverflow, for example,
  yields multiple questions whose answers all involving custom scripting, many of which assume the tables already
  reside in a relational database. A better solution would be consistent across platforms and supports
  canned options for common use cases
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
* Desired output format may be any of myriad tabular, JSON or other formats
* Case-insensitive matching across a full Unicode character set

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

## Example

Imagine we want to compare the following two tables:

### Table 1 (t1.csv)

|Country|City|AccentCity|Region|Population|Latitude|Longitude|
|--|--|--|--|--|--|--|
|de|placken|Placken|07||52.15|8.433333|
|gb|duffryn|Duffryn|Z3||51.4375|-3.304444|
|id|kemitang|Kemitang|07||-7.544444|109.183333|
|ie|oggal|Oggal|02||54.2597222|-7.9266667|
|mk|cvetovo|Cvetovo|92||41.8580556|21.4097222|
|pl|ciesle male|Ciesle Male|86||52.176861|17.649143|
|ru|chishmabash|Chishmabash|73||55.4708|53.8996|
|tz|lituhi mission|Lituhi Mission|14||-10.55|34.6|
|us|steep falls|Steep Falls|ME||43.7938889|-70.6530556|
|za|hota|Hota|05||-31.640685|27.681357|
|zr|kakova|Kakova|09||0.7333333|29.0166667|

### Table 2 (t2.csv)

|Region|Population|Latitude|Longitude|Country|City|AccentCity|
|--|--|--|--|--|--|--|
|92||41.8580556|21.4097222|mk|cvetovo|Cvetovo|
|86||52.176861|17.649143|pl|ciesle male|Ciesle XXX|
|39||41.677473|27.067859|tr|yenioe|Yenioe|
|14||-10.55|34.6|tz|lituhi mission|Lituhi Mission|
|ME||43.7938889|-70.6530556|us|steep falls|Steep Falls|
|05||-31.640685|27.681357|za|hota|Hota|
|XX||0.7333333|29.0166667|zr|kakova|Kakova|
|03||26.934908|115.628584|cn|fulongling|Fulongling|
|07||52.15|10.4|de|placken|Placken|
|Z3||51.4375|-3.304444|gb|duffryn|Duffryn|
|07||-7.544444|109.183333|id|kemitang|Kemitang|
|12||52.9911111|-6.8938889|ie|burtown cross roads|Burtown XXX|
|02||54.2597222|-7.9266667|ie|oggal|Oggal|
|16||34.788889|127.670833|kr|chusamdong|Chusamdong|

Then:
* rows should be matched on the key combination of city and country
* input will need to be sorted
* each table has a slightly different population

We can run:
```
zsv compare t1.csv t2.csv --sort -k country -k city
```

and get the following output:

|country|city|Column|t1.csv|t2.csv|
|--|--|--|--|--|
|cn|fulongling|&lt;key&gt;|Missing||
|de|placken|Longitude|8.433333|10.4|
|ie|burtown cross roads|&lt;key&gt;|Missing||
|kr|chusamdong|&lt;key&gt;|Missing||
|pl|ciesle male|AccentCity|Ciesle Male|Ciesle XXX|
|ru|chishmabash|&lt;key&gt;||Missing|
|tr|yenioe|&lt;key&gt;|Missing||
|zr|kakova|Region|09|XX|

Or in either of two JSON formats:
```
zsv compare --json t1.csv t2.csv --sort -k country -k city
```

to get:

```
[
  [
    "country",
    "city",
    "Column",
    "t1.csv",
    "t2.csv"
  ],
  [
    "cn",
    "fulongling",
    "<key>",
    "Missing",
    null
  ],
  [
    "de",
    "placken",
    "Longitude",
    "8.433333",
    "10.4"
  ],
  ...
]
```

or:
```
zsv compare --json-object t1.csv t2.csv --sort -k country -k city
```

to get:
```
[
  {
    "country": "cn",
    "city": "fulongling",
    "Column": "<key>",
    "t1.csv": "Missing"
  },
  {
    "country": "de",
    "city": "placken",
    "Column": "Longitude",
    "t1.csv": "8.433333",
    "t2.csv": "10.4"
  },
  ...
]
```

## Performance

No rigorous benchmarking has yet been performed, but preliminary testing yields reasonable performance and memory usage.
Running a comparison of two 40MB CSV files, each a table of 100,000 rows with 61 columns, containing approximately
60,000 differences, took about 5.8 seconds and used a maximum about 1.8MB of RAM on a 2019 MBA.
The same test with sorting used significantly more memory (up to ~40MB) and took about 8 seconds to complete.