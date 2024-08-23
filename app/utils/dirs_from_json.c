#include <yajl_helper/yajl_helper.h>
#include <zsv/utils/file.h>

struct zsv_dir_from_json_ctx {
  const char *filepath_prefix;
  unsigned char buff[4096];
  size_t content_start;
  FILE *out;
  char *out_filepath;
  struct jv_to_json_ctx jctx;
  zsv_jq_handle zjq;

  int err;
  unsigned char in_obj : 1;
  unsigned char do_check : 1;
  unsigned char dry : 1;
  unsigned char _ : 5;
};

static void zsv_dir_from_json_close_out(struct zsv_dir_from_json_ctx *ctx) {
  if (ctx->zjq) {
    zsv_jq_finish(ctx->zjq);
    zsv_jq_delete(ctx->zjq);
    ctx->zjq = NULL;
  }
  if (ctx->out) {
    fclose(ctx->out);
    ctx->out = NULL;
    free(ctx->out_filepath);
    ctx->out_filepath = NULL;
  }
}

static int zsv_dir_from_json_map_key(yajl_helper_t yh, const unsigned char *s, size_t len) {
  if (yajl_helper_level(yh) == 1 && len) { // new property file entry
    struct zsv_dir_from_json_ctx *ctx = yajl_helper_ctx(yh);

    char *fn = NULL;
    if (ctx->filepath_prefix)
      asprintf(&fn, "%s%c%.*s", ctx->filepath_prefix, FILESLASH, (int)len, s);
    else
      asprintf(&fn, "%.*s", (int)len, s);

    // if we have any backslashes, replace with fwd slash
    if (fn)
      for (int i = 0, j = strlen(fn); i < j; i++)
        if (fn[i] == '\\')
          fn[i] = '/';
    if (!fn) {
      errno = ENOMEM;
      perror(NULL);
    } else if (ctx->do_check) {
      // we just want to check if the destination file exists
      if (access(fn, F_OK) != -1) { // it exists
        ctx->err = errno = EEXIST;
        perror(fn);
      }
    } else if (ctx->dry) { // just output the name of the file
      printf("%s\n", fn);
    } else if (zsv_mkdirs(fn, 1)) {
      fprintf(stderr, "Unable to create directories for %s\n", fn);
    } else if (!((ctx->out = fopen(fn, "wb")))) {
      perror(fn);
    } else {
      ctx->out_filepath = fn;
      fn = NULL;

      // if it's a JSON file, use a jq filter to pretty-print
      if (strlen(ctx->out_filepath) > 5 &&
          !zsv_stricmp((const unsigned char *)ctx->out_filepath + strlen(ctx->out_filepath) - 5,
                       (const unsigned char *)".json")) {
        ctx->jctx.write1 = zsv_jq_fwrite1;
        ctx->jctx.ctx = ctx->out;
        ctx->jctx.flags = JV_PRINT_PRETTY | JV_PRINT_SPACE1;
        enum zsv_jq_status jqstat;
        ctx->zjq = zsv_jq_new((const unsigned char *)".", jv_to_json_func, &ctx->jctx, &jqstat);
        if (!ctx->zjq) {
          fprintf(stderr, "zsv_jq_new: unable to open for %s\n", ctx->out_filepath);
          zsv_dir_from_json_close_out(ctx);
        }
      }
    }
    free(fn);
  }
  return 1;
}

static int zsv_dir_from_json_start_obj(yajl_helper_t yh) {
  if (yajl_helper_level(yh) == 2) {
    struct zsv_dir_from_json_ctx *ctx = yajl_helper_ctx(yh);
    ctx->in_obj = 1;
    ctx->content_start = yajl_get_bytes_consumed(yajl_helper_yajl(yh)) - 1;
  }
  return 1;
}

// zsv_dir_from_json_flush(): return err
static int zsv_dir_from_json_flush(yajl_handle yajl, struct zsv_dir_from_json_ctx *ctx) {
  if (ctx->zjq) {
    size_t current_position = yajl_get_bytes_consumed(yajl);
    if (current_position <= ctx->content_start)
      fprintf(stderr, "Error! zsv_dir_from_json_flush unexpected current position\n");
    else
      zsv_jq_parse(ctx->zjq, ctx->buff + ctx->content_start, current_position - ctx->content_start);
    ctx->content_start = 0;
  }
  return 0;
}

static int zsv_dir_from_json_end_obj(yajl_helper_t yh) {
  if (yajl_helper_level(yh) == 1) { // just finished level 2
    struct zsv_dir_from_json_ctx *ctx = yajl_helper_ctx(yh);
    zsv_dir_from_json_flush(yajl_helper_yajl(yh), yajl_helper_ctx(yh));
    zsv_dir_from_json_close_out(ctx);
    ctx->in_obj = 0;
  }
  return 1;
}

static int zsv_dir_from_json_process_value(yajl_helper_t yh, struct json_value *value) {
  if (yajl_helper_level(yh) == 1) { // just finished level 2
    struct zsv_dir_from_json_ctx *ctx = yajl_helper_ctx(yh);
    const unsigned char *jsstr;
    size_t len;
    json_value_default_string(value, &jsstr, &len);
    if (ctx->zjq) {
      unsigned char *js = len ? zsv_json_from_str_n(jsstr, len) : NULL;
      if (js)
        zsv_jq_parse(ctx->zjq, js, strlen((char *)js));
      else
        zsv_jq_parse(ctx->zjq, "null", 4);
      free(js);
    } else if (len && ctx->out)
      fwrite(jsstr, 1, len, ctx->out);
    zsv_dir_from_json_close_out(ctx);
  }
  return 1;
}

/**
 * Convert a JSON stream into file and directory contents
 * This function is the inverse of zsv_dir_to_json()
 * Output schema is a dictionary where key = path and value = contents
 * Files named with .json suffix will be exported as JSON (content must be valid JSON)
 * Files named with any other suffix will be exported as a single string value (do not try with large files)
 */
int zsv_dir_from_json(const unsigned char *target_dir, FILE *src,
                      unsigned int flags, // ZSV_DIR_FLAG_XX
                      unsigned char _verbose) {
  (void)(_verbose);
  int err = 0;
  int force = !!(flags & ZSV_DIR_FLAG_FORCE);
  int dry = !!(flags & ZSV_DIR_FLAG_DRY);
  char *tmp_fn = NULL; // only used if force = 0 and src == stdin
  if (!force) {
    // if input is stdin, we'll need to read it twice, so save it first
    // this isn't the most efficient way to do it, as it reads it 3 times
    // but it's easier and the diff is immaterial
    if (src == stdin) {
      src = NULL;
      tmp_fn = zsv_get_temp_filename("zsv_prop_XXXXXXXX");
      FILE *tmp_f;
      if (!tmp_fn) {
        err = errno = ENOMEM;
        perror(NULL);
      } else if (!(tmp_f = fopen(tmp_fn, "wb"))) {
        err = errno;
        perror(tmp_fn);
      } else {
        err = zsv_copy_file_ptr(stdin, tmp_f);
        fclose(tmp_f);
        if (!(src = fopen(tmp_fn, "rb"))) {
          err = errno;
          perror(tmp_fn);
        }
      }
    }
  }

  if (!err) {
    // we will run this loop either once (force) or twice (no force):
    // 1. check before running (no force)
    // 2. do the import
    char do_check = !force;
    if (do_check && !zsv_dir_exists((const char *)target_dir))
      do_check = 0;

    for (int i = do_check ? 0 : 1; i < 2 && !err; i++) {
      do_check = i == 0;

      size_t bytes_read;
      yajl_helper_t yh;
      struct zsv_dir_from_json_ctx ctx = {0};
      ctx.filepath_prefix = (const char *)target_dir;

      int (*start_obj)(yajl_helper_t yh) = NULL;
      int (*end_obj)(yajl_helper_t yh) = NULL;
      int (*process_value)(struct yajl_helper_parse_state *, struct json_value *) = NULL;

      if (do_check)
        ctx.do_check = do_check;
      else {
        ctx.dry = dry;
        if (!ctx.dry) {
          start_obj = zsv_dir_from_json_start_obj;
          end_obj = zsv_dir_from_json_end_obj;
          process_value = zsv_dir_from_json_process_value;
        }
      }

      yh = yajl_helper_new(32, start_obj, end_obj,                        // map start/end
                           zsv_dir_from_json_map_key, start_obj, end_obj, // array start/end
                           process_value, &ctx);
      if (!yh) {
        err = errno = ENOMEM;
        perror(NULL);
      } else {
        yajl_handle y = yajl_helper_yajl(yh);
        while ((bytes_read = fread(ctx.buff, 1, sizeof(ctx.buff), src)) > 0) {
          if (yajl_parse(y, ctx.buff, bytes_read) != yajl_status_ok)
            yajl_helper_print_err(y, ctx.buff, bytes_read);
          if (ctx.in_obj)
            zsv_dir_from_json_flush(y, &ctx);
        }
        if (yajl_complete_parse(y) != yajl_status_ok)
          yajl_helper_print_err(y, ctx.buff, bytes_read);

        if (ctx.out) { // e.g. if bad JSON and parse failed
          fclose(ctx.out);
          free(ctx.out_filepath);
        }
        yajl_helper_delete(yh);
      }

      if (ctx.err)
        err = ctx.err;
      if (i == 0) {
        rewind(src);
        if (errno) {
          err = errno;
          perror(NULL);
        }
      }
    }
  }
  if (tmp_fn) {
    unlink(tmp_fn);
    free(tmp_fn);
  }

  return err;
}
