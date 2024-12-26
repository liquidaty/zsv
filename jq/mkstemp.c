#include <sys/stat.h>
#include <fcntl.h>

static const char *mkstemp_letters = 
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static int is_ok_dir(const char *path) { // aka dir_exists()
  struct stat path_stat;
  if(!stat(path, &path_stat))
    return S_ISDIR(path_stat.st_mode);
  return 0; // stat() err
}

static int mkdirs(const char *path, char is_filename) {
  char *p = NULL;
  int rc = 0;

  size_t len = strlen(path);
  if(len < 1 || len > FILENAME_MAX)
    return -1;

  char *tmp = strdup(path);
  if(len && strchr("/\\", tmp[len - 1]))
    tmp[--len] = 0;

  int offset = 0;
#ifdef WIN32
  if(len > 1) {
    // starts with two slashes
    if(strchr("/\\", tmp[0]) && strchr("/\\", tmp[1]))
      offset = 2;

    // starts with *:
    else if(tmp[1] == ':')
      offset = 2;
  }
#else
  offset = 1;
#endif

  for(p = tmp + offset; !rc && *p; p++)
    if(strchr("/\\", *p)) {
      char tmp_c = p[1];
      p[0] = '/';
      p[1] = '\0';
      if(*tmp && !is_ok_dir(tmp) && mkdir(tmp, S_IRWXU))
        rc = -1;
      else
        p[1] = tmp_c;
    }

  if(!rc && is_filename == 0 && *tmp && !is_ok_dir(tmp)
     && mkdir(tmp, S_IRWXU))
    rc = -1;

  free(tmp);
  return rc;
}

static unsigned long long random_bits(void) {
  // Number of bits in an unsigned long long on this platform
  const size_t total_bits = sizeof(unsigned long long) * CHAR_BIT;

  unsigned long long result = 0ULL;
  size_t bits_collected = 0;

  /*
   * We'll collect bits in chunks of 8 (one byte) at a time.
   * Because C guarantees RAND_MAX >= 32767, we can safely do (rand() & 0xFF)
   * and get 8 bits each time.
   */
  while (bits_collected < total_bits) {
    // Extract the lower 8 bits (one byte) from rand()
    unsigned long chunk = (unsigned long)(rand() & 0xFF);

    /*
     * Figure out how many bits we still need. If we're near the end,
     * we might need fewer than 8.
     */
    size_t bits_needed = total_bits - bits_collected;
    size_t bits_this_round = (bits_needed < 8) ? bits_needed : 8;

    // Mask off only the bits we actually need from the 8-bit chunk
    unsigned long mask = (1UL << bits_this_round) - 1;
    chunk &= mask;

    // Shift the chunk into the correct position within result
    result |= ((unsigned long long)chunk << bits_collected);

    // We’ve now collected `bits_this_round` more bits
    bits_collected += bits_this_round;
  }

  return result;

}

int mkstemp(char *tmpl) {
  int len;
  char *XXXXXX;
  static unsigned long long value;
  unsigned int count;
  int fd = -1;
  int save_errno = errno;

#define MKSTEMP_LETTER_COUNT 36
#define ATTEMPTS_MIN (MKSTEMP_LETTER_COUNT * MKSTEMP_LETTER_COUNT * MKSTEMP_LETTER_COUNT )

  /* The number of times to attempt to generate a temporary file.  To
     conform to POSIX, this must be no smaller than TMP_MAX.  */
#if ATTEMPTS_MIN < TMP_MAX
  unsigned int attempts = TMP_MAX;
#else
  unsigned int attempts = ATTEMPTS_MIN;
#endif

  len = strlen (tmpl);
  if (len < 6 || !(XXXXXX = strstr(tmpl, "XXXXXX"))) {
    errno = EINVAL;
    return -1;
  }

  value = random_bits();
  unsigned int X_count;
  for(X_count = 6; XXXXXX[X_count] == 'X'; X_count++)
    ;

  mkdirs(tmpl, 1);
  for (count = 0; count < attempts; value += 7777, ++count) {
    unsigned long long v = value;
    unsigned int X_i;
      
    for(X_i = 0; X_i < X_count; X_i++) {
      /* Fill in the random bits.  */
      XXXXXX[X_i] = mkstemp_letters[v % MKSTEMP_LETTER_COUNT];
      v /= MKSTEMP_LETTER_COUNT;
    }
    fd = open (tmpl, O_WRONLY | O_CREAT, 0660);

    if (fd >= 0) {
      errno = save_errno;
      return fd;
    } else if (errno != EEXIST)
      return -1;
  }
  
  /* We got out of the loop because we ran out of combinations to try.  */
  errno = EEXIST;
  return -1;
}
