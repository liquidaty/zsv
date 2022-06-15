# CSV, JSON and SQLITE3

Summary:

- Yes, CSV sucks. But it, and its xxx-delimited brethren, are still around a
  lot, and let's face it, delimited format does have certain advantages (such as
  being at least somewhat readable directly by human eyes), which is probably
  why they've been around so long
- sqlite3 is the most widely deployed database engine in the world
- JSON is useful as an intermediate format between CSV and sqlite3 because it is
  more API-friendly than CSV, is more git- and diff-friendly than the binary
  sqlite3 format, can be used to hold both metadata as well as data, and can be
  devised for performant stream-based consumption
- the same could be said of other binary table formats (parquet etc), though
  that is out of scope for the discussion here

## Background

Each of these formats, commonly used for tabular data, has different strengths
and weaknesses:

- CSV is easy to use in any text editor or common apps such as Excel or Google
  Sheets, but lacks schema, data types or indexing

- JSON is easy to use for API inter-communication and REST APIs, editable with
  any text editor, supports primitive data types; but, lacks native indexing or
  schema (though JSON Schema can be overlaid for validation and documentation)
  and is difficult for non-technical users to use or analyze

- sqlite3 supports schema, indexes and other SQL operations, but requires
  sqlite3 and knowledge of SQL to use, and file format is binary which is not
  human-readable and is difficult to version-track either metadata or data

## Benefits of converting between formats

- easily stream CSV or DB data through APIs and back to CSV / DB

- use standard version control on sqlite3 files, covering metadata and data, by
  converting to JSON and tracking that instead of the binary DB file

- Using DB over CSV offers specific benefits not available in other formats such
  as function-based indexes and much more

## Using `zsv` to work with data in your format of choice

`zsv` provides high-performance conversion to and from various formats
including:

- CSV (or TSV or pipe- or other- delimited)
- JSON (as arrays, objects, or [database-friendly table schema](db.schema.json))
- sqlite3

To install `zsv`, see https://github.com/liquidaty/zsv/blob/main/BUILD.md

`zsv`'s `2json`, `2db` and `jq` commands provide conversion between delimited,
json and sqlite3 formats. A streamlined JSON schema is used to retain metadata
when converting between JSON and sqlite3 formats, using the
[zsv](https://github.com/liquidaty/zsv), [jq](https://github.com/stedolan/jq)
and [sqlite3](https://www.sqlite.org) libraries.

### CSV to JSON

#### The common (and not very useful) way

The most common CSV to JSON conversion approaches, referred to herein as the
"plain-array" and "plain-objects" approaches, have limited utility:

```shell
zsv 2json data.csv --no-header < data.csv # output as arrays
zsv 2json data.csv --object < data.csv    # output as objects
```

The problem with the first format is that it treats the header row as a data row
and lacks support for additional metadata such as data type.

The second format has the same weakness and furthermore takes up significantly
more space, though it has the advantage of each row being independently
human-readable without without having to look at the header information to know
which data corresponds to which column. That said, this advantage has limited
benefit only useful, in the best case, for debugging purposes. To quantify the
difference in data volume, using the first 10k rows of worldcitiespop.csv, the
`plain-object` format is 78% larger or 117% larger for pretty-printed or compact
JSON, respectively.

#### Adding column metadata

A better approach is to replace each header name with an object, to provide a
means to store metadata separately from data. One approach is to simply allow
the first row to use an object in lieu of a scalar galue, in what we refer to
herein as a [row-based schema](csv.schema.json):

```shell
# output as an array of rows, where the first row is a list of column header objects
zsv 2json < data.csv > data.json

# back to CSV
zsv jq '[.[0]|[.[]|.name]] + .[1:]|.[]' --csv < data.json

# or
zsv jq '.[]|[.[]|(if (.|type) == "object" then .name else . end)]' --csv < data.json
```

Note that the above `zsv jq` filter is essentially the same as what you would
use with `jq` in conjunction with `@csv` and `-r`, but the csv output from `zsv
jq ... --csv` is a bit more compact due to the omission of unnecessary
surrounding quotes

This approach has the same overall structure as the `plain-array` method, but
extends each cell's schema to allow for an object that can hold metadata. This
provides significantly more flexibility, but still has a significant drawback:
it does not allow for table-level metadata.

#### Adding table metadata

To support both table and column metadata, `zsv` uses a schema comprised of a
tuple with two elements: metadata and data.

This [database-friendly approach](db.schema.json) supports table metadata such
as table name and indexes.

Future enhancements could support table-level validations/constraints and other
features, and this approach could be merged with the aforementioned [row-based
schema](csv.schema.json) to support row-level or cell-level metadata, to
represent spreadsheet data such as cell-level formatting.

```shell
# Convert CSV to JSON
# Additionally, specify a non-unique index on Population and a unique index on the combination of City, Country and Region
zsv 2json --database --index "ix1 on state" --unique-index "ix2 on City, Country, Region" < data.csv > database.json

# Convert the resulting JSON to a sqlite3 database
zsv 2db -o my.db --table mytable < database.json

# JSON back to CSV
zsv jq '[.[0]|.columns|[.[]|.name]] + .[1]|.[]' --csv < database.json

## or
zsv jq '.[]|if (.|type) == "object" then (.columns|[.[]|.name]) else . end|.[]' --csv < database.json
```

## Questions

### Why the bundled / "swiss army knife" approach?

An alternative approach, which is more consistent with the unix philosophy
whereby "each utility does one and only one thing", would be leave anything
related to sqlite3 and JSON to `sqlite3`, `jq`, respectively, or similar
standalone utilities.

However, the unix philosophy also assumed that a full toolset would always be
available, whereas we cannot assume that anyone already has `jq` or `sqlite3`
installed. It is true that we could have distributed them with this package but
then we introduce potential conflict where they were already installed.
Furthermore, there are other benefits to bundling:

- tasks that involve multiple utilities can be further simplified and
  modularized For example, CSV->DB conversion can be further broken down into
  CSV->JSON->DB, and the middle JSON form can be independently manipulated. This
  additional modularization / granularity provides greater optionality with
  regard to flexibility and control of how data is processed

- deployment is simplified (there is only one executable), and the total
  executable code size is compact even when fully statically compiled. this is
  convenient in environments where dynamically-linked code can be challenging,
  such as WebAssembly

The main drawback of the bundled approach is that if a single command needs to
be updated, the entire executable must be updated. In most cases, our experience
is that the above benefits are worth this cost. However, one need not choose one
over the other: the `zsv` source also supports unbundled binary builds (though
the unbundled binaries are not part of the officially supported packages).

Note that `zsv` does not attempt to replace `jq` or `sqlite3` and only
integrates a portion of their features that is necessary for basic conversion,
testing and other common pipeline operations.

### For each of these JSON schemas, why is top-level construct is an array and not an object

At some point while reading this, you may have wondered: why is the JSON schema
an array instead of an object? Why not something like:

```json
{
  metadata: ... # columns, indexes, whatever
  data: ...
}
```

Indeed, to programmers familiar with data structures, this would seem like a
logical approach.

One of the central design principles that `zsv` follows is to support
stream-based processing whenever possible, in order to maximize performance
efficiency. To do that, serialized data must always locate metadata before data.

Unfortunately, JSON does not support ordered properties within an object. As a
result, if the top-level container was an object, there would be no guarantee
that when serialized, metadata would always be written (or read, in the case of
the recipient) before data. The only way to enforce order in JSON is to use a
list.

#### For the database-compatible JSON schema, why use a 2-element tuple?

An alternative to the 2-element tuple would be to simply use a list such as is
used in the [row-based schema](csv.schema.json). The primary reason to prefer
the tuple approach is *optionality*. Using the tuple approach still offers the
option to support everything the row-based schema offers, while furthermore
offering the benefit of a container that is unambiguously and solely dedicated
to metadata.

### How can this approach be extended to databases or database services (vs a single table at a time)?

To extend this approach to an entire database, there are several objectives to
consider:

1. coverage: all metadata (and data) must be covered
2. modularity: metadata must be separable not from data, but also within its own
   categories (e.g. tables vs indexes)
3. performance: metadata and data must be serializable in a form that can be
   efficiently unserialized

The [database-friendly table schema](db.schema.json) is compatible with all of
these objectives.

Table and column metadata is already covered, so the only remaining levels of
metadata to address are database and database-service. These could be handled
through a top-level object with properties such as "tables", "views" etc.

However, for serialization, it may be advantageous to output all metadata for
all tables (and other objects) before any data. In addition, there may be
reasons to want to specify the order in which metadata objects or table data is
populated. As discussed above for table objects, this requires ordering which in
turn requires a list rather than an object.

So putting them all together, a database-wide schema could be structured as a
(metadata, data) tuple as follows:

```text
# first, define a metadata object with a discriminator
{
  type: table|view|...
  object: [meta, data]    # if type = table, the object will be in [database-friendly table schema](db.schema.json)
}

# now, our database tuple:
[
  # metadata comes first
  [
    object1, # only the first (metadata) element of the tuple should be populated
    object2,
    ...
  ],
  
  # table data comes next, in an ordered list
  [
    object1, # the first element should contain the object name, and the second populated with data
    object2,
    ...
  ]
]
```

A similar approach could be used for a collection of databases. The above is not
meant to be a comprehensive final design, but hopefully conveys the basic idea
that the [database-friendly table schema](db.schema.json) will not require
breaking change in order to leverage for extended purposes

### Can you extend the `zsv 2db` command and/or related JSON schema to support the sqlite3 feature XYZ

For standard sqlite3 features, the answer is generally yes!

Please [open an issue](https://github.com/liquidaty/zsv/issues/new/choose) with
your request.
