void str2hex(unsigned char *to, const unsigned char *p, size_t len) {
  static const char *hex = "0123456789abcdef";

  for (; len--; p++) {
    *to++ = hex[p[0] >> 4];
    *to++ = hex[p[0] & 0x0f];
  }
}

static char UTF8_charLenC(int firstChar) { /* return 0 on eof, -1 on error, > 0 if char read) */
  char len;
  if(firstChar == EOF) len = 0;
  else if(!(firstChar & 128)) len = 1;
  else if((firstChar & 224) == 192) len = 2; /* 110xxxxx */
  else if((firstChar & 240) == 224) len = 3; /* 1110xxxx */
  else if((firstChar & 248) == 240) len = 4; /* 11110xxx */
  else len = -1; /* error: 5/6-byte forms and stray continuation bytes are invalid (RFC 3629) */
  return len;
}

#define JSON_ESC_CHAR(c) (c == '"' || c == '\\' || c < 32)

static unsigned int json_esc1(const unsigned char *s, unsigned int slen,
                              unsigned int *replacelen,
                              unsigned char replace[], // replace buff should be at least 8 bytes long
                              const unsigned char **new_s,
                              size_t max_output_size) {
  unsigned char c;
  char c_len;
  const unsigned char *orig_s = s;
  *replacelen = 0;

  if(!max_output_size) {
    *new_s = s + slen;
    return 0;
  }

  /* Length-delimited: an embedded NUL is data (escaped to a \u00 hex escape
   * by the JSON_ESC_CHAR path below), not a terminator -- do not stop the scan
   * on it. slen and max_output_size bound the loop; NUL-terminated callers pass
   * slen == strlen, so they still stop at the NUL exactly. */
  while(slen && (size_t)(s - orig_s) < max_output_size) {
    c = *s;
    c_len = UTF8_charLenC(*s);

    if(c_len > 0 && ((unsigned char)c_len > slen || (size_t)((s - orig_s) + c_len) > max_output_size))
      break; // roll back and return

    if(c_len > 1) {
      // pass the sequence through verbatim only if its trailing bytes are valid
      // continuation bytes (10xxxxxx); otherwise the lead is bad utf8 and a
      // following '"', '\\' or control byte must not ride through unescaped
      int k;
      for(k = 1; k < c_len; k++)
        if((s[k] & 0xC0) != 0x80)
          break;
      if(k == c_len) {
        s += c_len;
        slen -= c_len;
        continue;
      }
      c_len = -1; // invalid sequence: handle as bad utf8 below
    }
    if(c_len < 0) { // bad utf8: drop the offending byte, resume after it
      *replacelen = 0;
      *new_s = s + 1;
      return (unsigned int)(s - orig_s);
    } else if(JSON_ESC_CHAR(c)) {
      char hex = 0;
      switch(c) {
      case '"':
	replace[1] = '"';
	break;
      case '\\':
	replace[1] = '\\';
	break;
      case '\b':
	replace[1] = 'b';
	break;
      case '\f':
	replace[1] = 'f';
	break;
      case '\n':
	replace[1] = 'n';
	break;
      case '\r':
	replace[1] = 'r';
	break;
      case '\t':
	replace[1] = 't';
	break;
      default:
	hex=1;
      }

      replace[0] = '\\';
      if(hex) {
	// unicode-hex: but 2/3 are not always zeroes...
	replace[1] = 'u';
	replace[2] = '0';
	replace[3] = '0';
	str2hex(replace+4, &c, 1);
	*replacelen = 6;
	replace[6] = '\0';
      } else {
	*replacelen = 2;
	replace[2] = '\0';
      }

      if((size_t)((s - orig_s) + *replacelen) > max_output_size) {
        *replacelen = 0; // roll back and return
        break;
      }

      *new_s = s+1;
      return (unsigned int)(s - orig_s);
    }
    s++, slen--;
  }

  *replacelen = 0;
  *new_s = s;
  return (unsigned int)(s - orig_s);
}
