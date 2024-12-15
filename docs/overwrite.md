# Overwrite - Manage Overwrites Associated with a CSV File

The `overwrite` utility allows you to manage a list of "overwrites" associated with a given CSV input file. Each overwrite entry is a tuple consisting of row, column, and new value, along with optional timestamp and author metadata.

## Usage

```shell
overwrite <file> <command> [arguments] [options]
```

### Commands

**List**  
`list`  
List all overwrites in the overwrites file.

**Add**  
`add <cell> <value>`  
Add an overwrite entry.  
Option for either row-col style cells, or Excel A1 style.
Examples:  
1. Overwrite the first column of the first non-header row:  
   ```sh
   overwrite mydata.csv add B2 "new value"
   ```  
   or  
   ```sh
   overwrite mydata.csv add 1-1 "new value"
   ```

2. Change the header in the second column to "ID #":  
   ```sh
   overwrite mydata.csv add B1 "ID #"
   ```  
   or  
   ```sh
   overwrite mydata.csv add 0-1 "ID #"
   ```

**Remove**  
`remove <cell>`  
Remove an overwrite entry.
Similar to add for examples, without value.

**Clear**  
`clear`
Remove any or all overwrites.

**Bulk Operations**
`bulk-add <datafile>`  
or
`bulk-remove <datafile>`  
Bulk operations add or remove overwrite entries from a CSV or JSON file.

**Note:**
JSON not currently supported.

### Options

- **-h, --help**  
  Show this help message.

- **--old-value <value>**  
  For `add` or `remove`, only proceed if the current overwrite value matches the given value at the given cell.

- **--force**  
  - For `add`, proceed even if an overwrite for the specified cell already exists.  
  - For `remove`, exit without error even if no overwrite for the specified cell already exists.

- **--no-timestamp**  
  For `add`, do not save a timestamp when adding an overwrite.

- **--all**  
  For `remove`, remove all overwrites and delete the SQLite file.

- **--A1**  
  For `list`, display addresses in A1-notation.

## Description

Overwrite data for a given input file `/path/to/my-data.csv` is stored in the "overwrites" table of `/path/to/.zsv/data/my-data.csv/overwrite.sqlite3`.

For bulk operations, the data file must be a CSV with "row", "col," and "value" columns and may optionally include "old value," "timestamp," and/or "author."

## Examples

Example CSV file ([mydata.csv](../data/mydata.csv)):
| Arabian 	           | Fertile Crescent | South Caucasus |
| -------------------- | ------------     | -------------- |
| Kuwait               | Iraq   	      | Armenia        |
| Oman   	           | Jordan   	      | Azerbaijan     |
| Qatar     	       | Lebanon   	      | Georgia        |
| Yemen   	           | Palestine        |	               |
| Bahrain   	       | Syria   	      |  	           |
| Saudi Arabia         | Israel   	      |   	           |
| United Arab Emirates |       		      | 	           |

### Bulk Overwrite operations
Example layout of a bulk file [bulkdata.csv](../data/bulkdata.csv):

| row | col | value        | timestamp  |
| --- | --- | ------------ | ---------- |
| 1   | 2   | Saudi Arabia | 1733283235 |
| 1   | 7   | Kuwait 	   | 1733283102 |
| 2   | 3   | Israel 	   | 1733282194 |
| 2   | 7   | Jordan 	   | 1733258125 |
| 2   | 2   | Palestine    | 1733285225 |
| 2   | 5   | Iraq 	       | 1733284211 |
| 3   | 3   | Georgia 	   | 1733284010 |
| 3   | 4   | Azerbaijan   | 1733285510 | 

bulk-add would add overwrites to the overwrite file, where bulk-remove would remove matching overwrites from the overwrite file.

```sh
overwrite mydata.csv bulk-add bulkdata.csv
select mydata.csv --apply-overwrites
```
Output:
| Arabian 	       | Fertile Crescent | South Caucasus |
| ---------------- | ---------------- | -------------- |
| Saudi Arabia     | Palestine	      | Armenia        |
| Oman   	       | Israel 	      | Georgia	       |
| Qatar     	   | Lebanon   	      | Azerbaijan	   |
| Yemen   	       | Iraq   	      |	               |
| Bahrain   	   | Syria   	      |  	           |
| Saudi Arabia     | Jordan		      |   	           |
| Kuwait 	       |       		      | 	           |

Now we can list the overwrites
```sh
overwrite mydata.csv list
```
Output:
```
row,column,value,timestamp,author
1,2,Saudi Arabia,1733283235,
1,7,Kuwait,1733283102,
2,2,Palestine,1733285225,
2,3,Israel,1733282194,
2,5,Iraq,1733284211,
2,7,Jordan,1733258125,
3,3,Georgia,1733284010,
3,4,Azerbaijan,1733285510,
```

Applying bulk-remove
```sh
overwrite mydata.csv bulk-remove bulkdata.csv
overwrite mydata.csv list
```
Output:
```
row,column,value,timestamp,author
```

### Basic Overwrite operations

To add an overwrite entry that changes the value in cell B2:

To add a value:
```sh
overwrite mydata.csv add B2 "Syria"
select mydata.csv --apply-overwrites
```
Output:
| Arabian 	           | Fertile Crescent | South Caucasus |
| -------------------- | ------------     | -------------- |
| Kuwait               | Syria   	      | Armenia        |
| Oman   	           | Jordan   	      | Azerbaijan     |
| Qatar     	       | Lebanon   	      | Georgia        |
| Yemen   	           | Palestine        |	               |
| Bahrain   	       | Syria   	      |  	           |
| Saudi Arabia         | Israel   	      |   	           |
| United Arab Emirates |       		      | 	           |

To remove the added value:
```sh
overwrite mydata.csv remove B2
select mydata.csv --apply-overwrites
```
Output:
| Arabian 	           | Fertile Crescent | South Caucasus |
| -------------------- | ------------     | -------------- |
| Kuwait               | Iraq   	      | Armenia        |
| Oman   	           | Jordan   	      | Azerbaijan     |
| Qatar     	       | Lebanon   	      | Georgia        |
| Yemen   	           | Palestine        |	               |
| Bahrain   	       | Syria   	      |  	           |
| Saudi Arabia         | Israel   	      |   	           |
| United Arab Emirates |       		      | 	           |

To force add a value, even if there is already a value in that cell.
This overwrites the original value in B2 with the new selected value.
```sh
overwrite mydata.csv add B2 "Lebanon" --force
select mydata.csv --apply-overwrites
```
Output:
| Arabian 	           | Fertile Crescent | South Caucasus |
| -------------------- | ------------     | -------------- |
| Kuwait               | Lebanon   	      | Armenia        |
| Oman   	           | Jordan   	      | India          |
| Qatar     	       | Lebanon   	      | Georgia        |
| Yemen   	           | Palestine        |	               |
| Bahrain   	       | Syria   	      |  	           |
| Saudi Arabia         | Israel   	      |   	           |
| United Arab Emirates |       		      | 	           |

To remove/add a value, depending on the old value at the cell position.
This will only trigger if the existing value is "Azerbaijan":
```sh
overwrite mydata.csv add C3 "India" --old-value "Azerbaijan"
select mydata.csv --apply-overwrites
```
Output:
| Arabian 	           | Fertile Crescent | South Caucasus |
| -------------------- | ------------     | -------------- |
| Kuwait               | Iraq   	      | Armenia        |
| Oman   	           | Jordan   	      | India          |
| Qatar     	       | Lebanon   	      | Georgia        |
| Yemen   	           | Palestine        |	               |
| Bahrain   	       | Syria   	      |  	           |
| Saudi Arabia         | Israel   	      |   	           |
| United Arab Emirates |       		      | 	           |

Alternatively, if the old value was not the same as the value in the table.
```sh
overwrite mydata.csv add C3 "Pakistan" --old-value "Armenia"
select mydata.csv --apply-overwrites
```
The output would remain the same, as no values would be changed.

To remove all overwrites and delete the SQLite file:
```sh
overwrite mydata.csv remove --all
select mydata.csv --apply-overwrites
```
Output:
| Arabian 	           | Fertile Crescent | South Caucasus |
| -------------------- | ------------     | -------------- |
| Kuwait               | Iraq   	      | Armenia        |
| Oman   	           | Jordan   	      | Azerbaijan     |
| Qatar     	       | Lebanon   	      | Georgia        |
| Yemen   	           | Palestine        |	               |
| Bahrain   	       | Syria   	      |  	           |
| Saudi Arabia         | Israel   	      |   	           |
| United Arab Emirates |       		      | 	           |
The table now looks like the original table.


## File Storage

Overwrite data is stored in a dedicated SQLite database for each input file. The SQL operations are optimized for performance, by limiting the number of operations.
The SQLite file gets automatically created when a new overwrite is initialized, and is organized based on the input filename.

## Usage Details
```
zsv overwrite -h

Usage:
  overwrite <file> <command> [arguments] [options]

Commands (where <cell> can be <row>-<col> or an Excel-style address):
  list                   : Display all saved overwrite entries
  add <cell> <value>     : Add an overwrite entry
                           Example 1: overwrite the first column of the first
                           non-header row
                             overwrite mydata.csv add B2 "new value"
                               - or -
                             overwrite mydata.csv add 1-1 "new value"
                           Example 2: change the header in the second column
                           to "ID #"
                             overwrite mydata.csv add B1 "new value"
                               - or -
                             overwrite mydata.csv add 0-1 "ID #"
  remove <cell>          : Remove an overwrite entry
  clear                  : Remove any / all overwrites
  bulk-add <datafile>    : Bulk add overwrite entries from a CSV or JSON file
  bulk-remove <datafile> : Bulk remove overwrite entries from a CSV or JSON file

Options:
  -h,--help              : Show this help message
  --old-value <value>    : For `add` or `remove`, only proceed if the old value
                           matches the given value
  --force.               : For `add`, proceed even if an overwrite for the specified
                           cell already exists
                           For `remove`, exit without error even if no overwrite for
                           the specified cell already exists
  --no-timestamp.        : For `add`, don't save timestamp when adding an overwrite
  --all                  : For `remove`, remove all overwrites and delete sqlite file
  --A1                   : For `list`, Display addresses in A1-notation

Description:
  The  `overwrite`  utility  allows  you to manage a list of "overwrites" associated
  with a given CSV input file. Each overwrite entry is a tuple consisting  of  row,
  column,  original  value, and new value, along with optional timestamp and author
  metadata.

  Overwrite data for a given input file `/path/to/my-data.csv` is stored in the "over‐
  writes"  table  of  `/path/to/.zsv/data/my-data.csv/overwrite.sqlite3`.

  For bulk operations, the data file must be a CSV with "row", "col" and "value" columns
  and may optionally include "old value", "timestamp" and/or "author"
```
