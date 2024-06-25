/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h> // for close()
#include <fcntl.h> // open

#include <zsv/utils/dirs.h>
#include <zsv/utils/file.h>

/**
 * Get a temp file name. The returned value, if any, will have been allocated
 * on the heap, and the caller should `free()`
 *
 * @param prefix string with which the resulting file name will be prefixed
 */
#if defined(_WIN32) || defined(WIN32) || defined(WIN)
#include <windows.h>

char *zsv_get_temp_filename(const char *prefix) {
  TCHAR lpTempPathBuffer[MAX_PATH];
  DWORD dwRetVal = GetTempPath(MAX_PATH,          // length of the buffer
                         lpTempPathBuffer);       // buffer for path
  if(dwRetVal > 0 && dwRetVal < MAX_PATH) {
    char szTempFileName[MAX_PATH];
    UINT uRetVal = GetTempFileName(lpTempPathBuffer, // directory for tmp files
                                   TEXT(prefix),     // temp file name prefix
                                   0,                // create unique name
                                   szTempFileName);  // buffer for name
    if(uRetVal > 0)
      return strdup(szTempFileName);
  }
  return NULL;
}
#else

char *zsv_get_temp_filename(const char *prefix) {
  char *s = NULL;
  char *tmpdir = getenv("TMPDIR");
  if(!tmpdir)
    tmpdir = ".";
  asprintf(&s, "%s/%s_XXXXXXXX", tmpdir, prefix);
  if(!s) {
    const char *msg = strerror(errno);
    fprintf(stderr, "%s%c%s: %s\n", tmpdir, FILESLASH, prefix, msg ? msg : "Unknown error");
  } else {
    int fd = mkstemp(s);
    if(fd > 0) {
      close(fd);
      return s;
    }
    free(s);
  }
  return NULL;
}

#endif

/**
 * Temporarily redirect a FILE * (e.g. stdout / stderr) to a temp file
 * temp_filename and bak are set as return values
 * caller must free temp_filename
 *
 * @param old_fd file descriptor of file to dupe e.g. fileno(stdout);
 * @return fd needed to pass on to zsv_redirect_file_from_temp
 */
#if defined(_WIN32) || defined(__FreeBSD__)
# include <sys/stat.h> // S_IRUSR S_IWUSR
#endif

int zsv_redirect_file_to_temp(FILE *f, const char *tempfile_prefix,
                              char **temp_filename) {
  int new_fd;
  int old_fd = fileno(f);
  fflush(f);
  int bak = dup(old_fd);
  *temp_filename = zsv_get_temp_filename(tempfile_prefix);

  new_fd = open(*temp_filename, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);

  dup2(new_fd, old_fd);
  close(new_fd);
  return bak;
}

/**
 * Restore a FILE * that was redirected by zsv_redirect_file_to_temp()
 */
void zsv_redirect_file_from_temp(FILE *f, int bak, int old_fd) {
  fflush(f);
  dup2(bak, old_fd);
  close(bak);
}

#if defined(_WIN32) || defined(WIN32) || defined(WIN)
int zsv_file_exists(const char* filename) {
  DWORD attributes = GetFileAttributes(filename);
  return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
}
#else
# include <sys/stat.h> // S_IRUSR S_IWUSR

int zsv_file_exists(const char* filename) {
  struct stat buffer;
  if(stat(filename, &buffer) == 0) {
    char is_dir = buffer.st_mode & S_IFDIR ? 1 : 0;
    if(!is_dir)
      return 1;
  }
  return 0;
}
#endif

/**
 * Copy a file, given source and destination paths
 * On error, output error message and return non-zero
 */
int zsv_copy_file(const char *src, const char *dest) {
  // create one or more directories if needed
  if(zsv_mkdirs(dest, 1)) {
    fprintf(stderr, "Unable to create directories needed for %s\n", dest);
    return -1;
  }

  // copy the file
  int err = 0;
  FILE *fsrc = fopen(src, "rb");
  if(!fsrc)
    err = errno ? errno : -1, perror(src);
  else {
    FILE *fdest = fopen(dest, "wb");
    if(!fdest)
      err = errno ? errno : -1, perror(dest);
    else {
      err = zsv_copy_file_ptr(fsrc, fdest);
      if(err)
        perror(dest);
      fclose(fdest);
    }
    fclose(fsrc);
  }
  return err;
}

/**
 * Copy a file, given source and destination FILE pointers
 * Return error number per errno.h
 */
int zsv_copy_file_ptr(FILE *src, FILE *dest) {
  int err = 0;
  char buffer[4096];
  size_t bytes_read;
  while((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
    if(fwrite(buffer, 1, bytes_read, dest) != bytes_read) {
      err = errno ? errno : -1;
      break;
    }
  }
  return err;
}

size_t zsv_dir_len_basename(const char *filepath, const char **basename) {
  for(size_t len = strlen(filepath); len; len--) {
    if(filepath[len-1] == '/' || filepath[len-1] == '\\') {
      *basename = filepath + len;
      return len - 1;
    }
  }

  *basename = filepath;
  return 0;
}

int zsv_file_readable(const char *filename, int *err, FILE **f_out) {
  FILE *f;
  int rc;
  if(err)
    *err = 0;
  // to do: use fstat()
  if((f = fopen(filename, "rb")) == NULL) {
    rc = 0;
    if(err)
      *err = errno;
    else
      perror(filename);
  } else {
    rc = 1;
    if(f_out)
      *f_out = f;
    else
      fclose(f);
  }
  return rc;
}

/**
 * Function that is the same as `fwrite()`, but can be used as a callback
 * argument to `zsv_set_scan_filter()`
 */
size_t zsv_filter_write(void *FILEp, unsigned char *buff, size_t bytes_read) {
  fwrite(buff, 1, bytes_read, (FILE *)FILEp);
  return bytes_read;
}
