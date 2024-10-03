#include <utf8proc.h>

static int has_multibyte_char(const char *utf8, size_t len) {
  // this can be further optimized with simd
  // but doing so is probably not noticeably impactful
  // since we only do this for the finite number of cells on the screen
  uint64_t x;
  while (len >= 8) {
    len -= 8;
    // Copy the first 8 bytes into 64-bit integers
    memcpy(&x, utf8, sizeof(x));

    // Check if any high bits are set
    if ((x & 0x8080808080808080ULL) != 0)
      return 1;
    utf8 += 8;
  }
  if (len) {
    x = 0;
    memcpy(&x, utf8, len);
    if ((x & 0x8080808080808080ULL) != 0)
      return 1;
  }
  return 0;
}

static size_t is_newline(const unsigned char *utf8, int wchar_len) {
  return (wchar_len == 1 && strchr("\n\r", utf8[0]));
  // add multibyte newline check?
}

static size_t utf8_bytes_up_to_max_width_and_replace_newlines(unsigned char *str1, size_t len1, size_t max_width,
                                                              size_t *used_width, int *err) {
  utf8proc_int32_t codepoint1;
  utf8proc_ssize_t bytes_read1;
  size_t width_so_far = *used_width = 0;
  int this_char_width = 0;
  size_t bytes_so_far = 0;
  while (bytes_so_far < len1) {
    bytes_read1 = utf8proc_iterate((utf8proc_uint8_t *)str1 + bytes_so_far, len1, &codepoint1);
    if (!bytes_read1) {
      bytes_read1 = 1;
      *err = 1;
      this_char_width = 1;
    } else if (is_newline(str1 + bytes_so_far, bytes_read1)) {
      memset((void *)(str1 + bytes_so_far), ' ', bytes_read1);
      continue;
    } else {
      this_char_width = utf8proc_charwidth(codepoint1);
      if (width_so_far + this_char_width > max_width) {
        *used_width = width_so_far;
        return bytes_so_far;
      }
    }
    width_so_far += this_char_width;
    bytes_so_far += bytes_read1;
  }
  *used_width = width_so_far;
  return bytes_so_far;
}
