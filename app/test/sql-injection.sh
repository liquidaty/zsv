#!/bin/bash
set -e

# Create test data
echo "1,2,3" > test.csv
echo "4,5,6" >> test.csv

# Test valid SELECT query (should succeed)
../../zsv sql test.csv "select * from data" > test-sql-injection.out
diff test-sql-injection.out expected/test-sql-injection.out

# Test SQL injection attempts (should fail)
../../zsv sql test.csv "select * from data; drop table data" 2>/dev/null && exit 1
../../zsv sql test.csv "select * from data union select * from sqlite_master" 2>/dev/null && exit 1
../../zsv sql test.csv "select * from data where 1=1; --" 2>/dev/null && exit 1
../../zsv sql test.csv "select * from data/**/union/**/select*from sqlite_master" 2>/dev/null && exit 1

# Clean up
rm -f test.csv test-sql-injection.out

echo "SQL injection prevention tests passed"
exit 0
