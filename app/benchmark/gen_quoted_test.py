#!/usr/bin/env python3
"""Generate test CSV files with 100 columns and various quoting patterns.

Outputs two files:
  - quoted_standard.csv: properly quoted cells with embedded quotes and newlines
  - quoted_nonstandard.csv: non-standard quoting (trailing data after close quote,
    mid-cell quotes in unquoted cells)
"""

import random
import sys
import os

ROWS = 500000
COLS = 100
# 15 columns will have heavy quoting
QUOTED_COLS = sorted(random.Random(42).sample(range(COLS), 15))

random.seed(123)

def gen_standard_quoted_value():
    """Generate a properly quoted cell value."""
    r = random.random()
    if r < 0.3:
        # Embedded double-quote
        templates = [
            'She said ""hello"" to me',
            'The ""quick"" brown fox',
            'Value is ""42""',
            'Name: ""Smith, John""',
            '""Yes"" or ""No""',
            'A ""B"" C ""D"" E',
        ]
        return '"' + random.choice(templates) + '"'
    elif r < 0.6:
        # Embedded newline
        templates = [
            'Line one\nLine two',
            'First\nSecond\nThird',
            'Header\nDetail row',
            'Start\nMiddle\nEnd',
            'Above\nBelow',
        ]
        return '"' + random.choice(templates) + '"'
    elif r < 0.8:
        # Contains comma (needs quoting)
        templates = [
            'Smith, John',
            'New York, NY',
            'red, green, blue',
            'width, height, depth',
            'first, second',
            'A, B, C, D',
        ]
        return '"' + random.choice(templates) + '"'
    else:
        # Both embedded quotes and newlines
        templates = [
            'She said ""hi""\nThen left',
            'Col ""A""\nCol ""B""',
            'Name: ""Jones""\nCity: ""LA""',
        ]
        return '"' + random.choice(templates) + '"'


def gen_nonstandard_quoted_value():
    """Generate a non-standard quoted cell value."""
    r = random.random()
    if r < 0.4:
        # Quote in middle of unquoted cell
        templates = [
            'here: " is a quote',
            'value has " in it',
            'the 6" pipe',
            'a "B" value',
            'say "hello" world',
            '12" monitor',
        ]
        return random.choice(templates)
    elif r < 0.7:
        # Trailing data after closing quote
        templates = [
            '"hello"xyz',
            '"value"123',
            '"data" trailing',
            '"cell"!!',
            '"quoted"end',
        ]
        return random.choice(templates)
    else:
        # Standard quoting (some cells are still standard)
        return gen_standard_quoted_value()


def gen_plain_value():
    """Generate a plain unquoted cell value."""
    r = random.random()
    if r < 0.3:
        return str(random.randint(0, 999999))
    elif r < 0.6:
        words = ['alpha', 'beta', 'gamma', 'delta', 'epsilon', 'zeta',
                 'eta', 'theta', 'iota', 'kappa', 'lambda', 'mu']
        return random.choice(words)
    elif r < 0.8:
        return f'{random.random():.6f}'
    else:
        return ''


def write_csv(filename, use_nonstandard):
    gen_quoted = gen_nonstandard_quoted_value if use_nonstandard else gen_standard_quoted_value
    quoted_set = set(QUOTED_COLS)

    with open(filename, 'w', newline='') as f:
        # Header
        headers = [f'col{i:03d}' for i in range(COLS)]
        f.write(','.join(headers) + '\n')

        for row in range(ROWS):
            cells = []
            for col in range(COLS):
                if col in quoted_set and random.random() < 0.4:
                    cells.append(gen_quoted())
                else:
                    cells.append(gen_plain_value())
            f.write(','.join(cells) + '\n')

    size_mb = os.path.getsize(filename) / (1024 * 1024)
    print(f'  {filename}: {ROWS} rows, {COLS} cols, {size_mb:.1f} MB', file=sys.stderr)


if __name__ == '__main__':
    outdir = os.path.dirname(os.path.abspath(__file__))
    std_file = os.path.join(outdir, 'quoted_standard.csv')
    nonstd_file = os.path.join(outdir, 'quoted_nonstandard.csv')

    print('Generating test CSVs...', file=sys.stderr)
    write_csv(std_file, use_nonstandard=False)
    write_csv(nonstd_file, use_nonstandard=True)
    print('Done.', file=sys.stderr)
