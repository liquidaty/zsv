/*
 * Copyright (C) 2021 Tai Chi Minh Ralph Eastwood, Matt Wong and Guarnerix dba Liquidaty
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */
#ifndef ZSV_PARSER_H
#define ZSV_PARSER_H

#include <zsv_api.h>

struct zsv_ctx {
//  void * scanner;
  unsigned char bom_check; // 0 = not checked, 1/2/3 = read 1/2/all bytes of BOM
  char final; // true if this is the last chunk
  enum zsv_status status;
  const char *delim;
  char chunk_trailing_partial[6]; // 4 bytes + 2 ending nulls. used if chunk ends in partial utf8 char

  char cell_trailing_partial[6]; // 4 bytes + 2 ending nulls. used if cell buff ends in partial utf8 char
  size_t current_column;
  size_t current_length;
  size_t scanned_length;
  size_t cum_scanned_length;
  char last_lineend;
  size_t last_lineend_position; // cum_scanned_length of last lineend position

  const unsigned char *embedded_lineend; // defaults to \r
  unsigned int embedded_lineend_len;

  char quoted; // 0 = no quote scanned; 1 = open quote scanned; 2 = open and close quotes scanned
  struct {
    unsigned char *buff;
    size_t size; // max_str_size_before_overflow: max size of any cell text before overflow() is called
    size_t used;
  } buff;
  char in_overflow;
  struct zsv_opts *opts;

  struct {
    unsigned char *buff;
    size_t size;
    size_t used;
  } overflow;
};

#endif
