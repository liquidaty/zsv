  #!/bin/sh
  # Downloads workcitiespop.csv as worldcitiespop_lf.csv and then generates
  # - worldcitiespop_cr.csv (CR line endings)
  # - worldcitiespop_crlf.csv (CRLF line endings)
  # from it

  # Remove any existing files
  rm -f worldcitiespop_lf.csv
  rm -f worldcitiespop_cr.csv
  rm -f worldcitiespop_crlf.csv
  
  # Download/generate files
  curl -o worldcitiespop_lf.csv -LO 'https://burntsushi.net/stuff/worldcitiespop.csv'
  tr '\n' '\r' < worldcitiespop_lf.csv > worldcitiespop_cr.csv
  sed 's/$/\r/' worldcitiespop_lf.csv > worldcitiespop_crlf.csv
