#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> // unlink()

#include <errno.h>
#include <zsv/utils/cache.h>
#include <zsv/utils/jq.h>

#ifndef APPNAME
#define APPNAME "cache"
#endif

#include <zsv/utils/os.h>
#include <zsv/utils/err.h>
#include <zsv/utils/dirs.h>
#include <zsv/utils/file.h>

static const char *zsv_cache_type_name(enum zsv_cache_type t) {
  switch(t) {
  case zsv_cache_type_property:
    return ZSV_CACHE_PROPERTIES_NAME;
  case zsv_cache_type_tag:
    return "tag";
  default:
    return NULL;
  }
}

unsigned char *zsv_cache_filepath(const unsigned char *data_filepath,
                                  enum zsv_cache_type type, char create_dir,
                                  char temp_file) {
  if(!data_filepath || !*data_filepath)
    return NULL;

  const char *cache_filename_base = zsv_cache_type_name(type);
  if(!cache_filename_base) {
    zsv_printerr(ENOMEM, "Out of memory!");
    return NULL;
  }

  unsigned char *cache_filename;
  asprintf((char **)&cache_filename, "%s.json%s", cache_filename_base, temp_file ? ZSV_TEMPFILE_SUFFIX : "");

  unsigned char *s = cache_filename ? zsv_cache_path(data_filepath, cache_filename, 0) : NULL;
  if(s && create_dir) {
    char *last_slash_s = (char *)strrchr((void *)s, FILESLASH);
    int err = 0;

    // temporarily truncate the string so as to only leave its parent folder
    *last_slash_s = '\0';

    // ensure the parent dir exists
    if(!zsv_dir_exists((char *)s))
      err = zsv_mkdirs((char *)s, 0);
    if(err) {
      fprintf(stderr, "Unable to create cache directory %s\n", s);
      free(s);
      s = NULL;
    } else
      *last_slash_s = FILESLASH;
  }

  free(cache_filename);
  return s;
}

/*
 * print cache file to stdout
 */
int zsv_cache_print(const unsigned char *filepath, enum zsv_cache_type ctype,
                    const unsigned char *default_value) {
  int err = 0;
  // to do: parse the json rather than just blindly regurgitating the file
  unsigned char *cache_fn = zsv_cache_filepath(filepath, ctype, 0, 0);
  if(cache_fn) {
    FILE *f;
    if(zsv_file_readable((char *)cache_fn, &err, &f)) {
      char buff[1024];
      size_t bytes;
      while((bytes = fread(buff, 1, sizeof(buff), f)))
        fwrite(buff, 1, bytes, stdout);
      fclose(f);
    } else if(err == ENOENT) {
      if(default_value)
        printf("%s\n", default_value);
    } else {
      perror((const char *)cache_fn);
      if(!err) err = 1;
    }
  }
  free(cache_fn);
  return err;
}

/*
 * remove a cache file
 */

int zsv_cache_remove(const unsigned char *filepath, enum zsv_cache_type ctype) {
  int err = 0;
  unsigned char *fn = zsv_cache_filepath(filepath, ctype, 0, 0);
  if(!fn)
    err = ENOMEM;
  else if(zsv_file_readable((const char *)fn, &err, NULL)) {
    err = unlink((const char *)fn);
    if(err)
      perror((const char *)fn);
  } else if(err == ENOENT)
    err = 0; // file d.n. exist, nothing to do
  else
    perror((const char *)fn);
  free(fn);
  return err;
}


/*
 * modify a JSON cache file, write to tmp file, then replace the cache file
 */
int zsv_modify_cache_file(const unsigned char *filepath,
                          enum zsv_cache_type ctype,
                          const unsigned char *json_value1,
                          const unsigned char *json_value2,
                          const unsigned char *filter
                          ) {
  unsigned char *cache_fn = zsv_cache_filepath((const unsigned char *)filepath,
                                               ctype, 0, 0);
  unsigned char *cache_tmp_fn = zsv_cache_filepath((const unsigned char *)filepath,
                                                   ctype, 1, 1);
  FILE *cache_data = NULL;
  if(!(cache_fn && cache_tmp_fn))
    return zsv_printerr(ENOMEM, "Out of memory!");

  cache_data = fopen((void *)cache_fn, "rb");
  int err = 0;
  if(!cache_data) {
    err = errno;
    if(err == ENOENT)
      err = 0;
    else { // file exists but could not be opened
      perror((const char *)cache_fn);
      return err;
    }
  }

  if(cache_data) {
    // check that we have at least 1 byte of data
    fseek(cache_data, 1, SEEK_SET);
    if(!ftell(cache_data)) { // empty file; will use default value of "{}"
      fclose(cache_data);
      cache_data = NULL;
    } else
      fseek(cache_data, 0, SEEK_SET);
  }

  // jq filter to apply to [current_properties, id, value]
  FILE *tmp = fopen((const char *)cache_tmp_fn, "wb");
  if(!tmp) {
    if(!(err = errno)) err = 1;
    perror((const char *)cache_tmp_fn);
  } else {
    struct jv_to_json_ctx ctx = { 0 };
    ctx.write1 = zsv_jq_fwrite1;
    ctx.ctx = tmp;
    ctx.flags = JV_PRINT_PRETTY | JV_PRINT_SPACE1;
    enum zsv_jq_status jqstat;
    void *jqh = zsv_jq_new(filter, jv_to_json_func, &ctx, &jqstat);
    if(jqstat || !jqh)
      err = zsv_printerr(-1 ,"Unable to initialize jq filter");
    else if(!(jqstat = zsv_jq_parse(jqh, "[", 1))) {
      if(cache_data)
        jqstat = zsv_jq_parse_file(jqh, cache_data);
      else
        jqstat = zsv_jq_parse(jqh, "{}", 2);
      if(!jqstat
         && !(jqstat = zsv_jq_parse(jqh, ",", 1))
         && !(jqstat = zsv_jq_parse(jqh, json_value1, strlen((void *)json_value1)))
         && !(jqstat = zsv_jq_parse(jqh, ",", 1))
         && !(jqstat = zsv_jq_parse(jqh, json_value2, strlen((void *)json_value2)))
         && !(jqstat = zsv_jq_parse(jqh, "]", 1))
         && !(jqstat = zsv_jq_finish(jqh))) {
        ;
      }
    }
    zsv_jq_delete(jqh);

    if(cache_data) {
      fclose(cache_data);
      cache_data = NULL;
    }
    fclose(tmp);

    if(!jqstat && zsv_replace_file(cache_tmp_fn, cache_fn)) {
      err = zsv_printerr(-1, "Unable to save %s: ", cache_fn);
      zsv_perror(NULL);
    }
  }

  if(cache_data)
    fclose(cache_data);
  free(cache_fn);
  free(cache_tmp_fn);
  return err;
}

/**
 * Returns the folder or file path to the cache for a given data file
 * Caller must free the returned result
 */
unsigned char *zsv_cache_path(const unsigned char *data_filepath,
                              const unsigned char *cache_filename, char temp_file) {
  if(!data_filepath)
    return NULL;
  const unsigned char *last_slash = (void *)strrchr((void *)data_filepath, '/');
  const unsigned char *last_backslash = (void *)strrchr((void *)data_filepath, '\\');
  const unsigned char *dir_end = (!last_slash && !last_backslash ? NULL :
                                  last_backslash > last_slash ? last_backslash :
                                  last_slash);
  char *s = NULL;
  char *filename_suffix = NULL;
  if(cache_filename)
    asprintf(&filename_suffix, "%c%s%s", FILESLASH, cache_filename,
             temp_file ? ZSV_TEMPFILE_SUFFIX : "");

  if(!dir_end) // file is in current dir
    asprintf(&s, ZSV_CACHE_DIR"%c%s%s", FILESLASH, data_filepath, filename_suffix ? filename_suffix : "");
  else if(dir_end[1]) {
    asprintf(&s, "%.*s%c"ZSV_CACHE_DIR"%c%s%s", (int)(dir_end - data_filepath),
             data_filepath, FILESLASH, FILESLASH, dir_end + 1, filename_suffix ? filename_suffix : "");
    for(int i = 0; s && s[i]; i++)
      if(s[i] != FILESLASH && (s[i] == '/' || s[i] == '\\'))
        s[i] = FILESLASH;
  }
  free(filename_suffix);
  return (unsigned char *)s;
}
