struct zsv_dir_foreach_to_json_ctx {
  struct zsv_dir_filter zsv_dir_filter;
  const unsigned char *parent_dir;
  struct jv_to_json_ctx jctx;
  zsv_jq_handle zjq;
  unsigned count; // number of files exported so far
  int err;
};

static int zsv_dir_foreach_to_json(struct zsv_foreach_dirent_handle *h, size_t depth) {
  struct zsv_dir_foreach_to_json_ctx *ctx = h->ctx;
  h->ctx = ctx->zsv_dir_filter.ctx;
  if(!ctx->zsv_dir_filter.filter(h, depth))
    h->no_recurse = 1; // skip this node (only matters if node is dir)
  else if(!h->is_dir) { // process this file
    char suffix = 0;
    if(strlen(h->parent_and_entry) > 5 && !zsv_stricmp((const unsigned char *)h->parent_and_entry + strlen(h->parent_and_entry) - 5, (const unsigned char *)".json"))
      suffix = 'j'; // json
    else if(strlen(h->parent_and_entry) > 4 && !zsv_stricmp((const unsigned char *)h->parent_and_entry + strlen(h->parent_and_entry) - 4, (const unsigned char *)".txt"))
      suffix = 't'; // text
    if(suffix) {
      // for now, only handle json or txt
      FILE *f = fopen(h->parent_and_entry, "rb");
      if(!f)
        perror(h->parent_and_entry);
      else {
        // create an entry for this file. the map key is the file name; its value is the file contents
        unsigned char *js = zsv_json_from_str((const unsigned char *)h->parent_and_entry + strlen((const char *)ctx->parent_dir) + 1);
        if(!js)
          errno = ENOMEM, perror(NULL);
        else if(*js) {
          if(ctx->count > 0)
            if(zsv_jq_parse(ctx->zjq, ",", 1))
              ctx->err = 1;
          if(!ctx->err) {
            ctx->count++;
            if(zsv_jq_parse(ctx->zjq, js, strlen((const char *)js)) || zsv_jq_parse(ctx->zjq, ":", 1))
              ctx->err = 1;
            else {
              switch(suffix) {
              case 'j': // json
                if(zsv_jq_parse_file(ctx->zjq, f))
                  ctx->err = 1;
                break;
              case 't': // txt
                // for now we are going to limit txt file values to 4096 chars and JSON-stringify it
                {
                  unsigned char buff[4096];
                  size_t n = fread(buff, 1, sizeof(buff), f);
                  unsigned char *txt_js = NULL;
                  if(n) {
                    txt_js = zsv_json_from_str_n(buff, n);
                    if(zsv_jq_parse(ctx->zjq, txt_js ? txt_js : (const unsigned char *)"null", txt_js ? strlen((const char *)txt_js) : 4))
                      ctx->err = 1;
                  }
                }
                break;
              }
            }
          }
        }
        free(js);
        fclose(f);
      }
    }
  }
  h->ctx = ctx;
  return 0;
}

/**
 * Convert directory contents into a single JSON file
 * Output schema is a dictionary where key = path and value = contents
 * Files named with .json suffix will be exported as JSON (content must be valid JSON)
 * Files named with any other suffix will be exported as a single string value (do not try with large files)
 *
 * @param parent_dir : directory to export
 * @param dest       : file path to output to, or NULL to output to stdout
 */
int zsv_dir_to_json(const unsigned char *parent_dir,
                    const unsigned char *output_filename,
                    struct zsv_dir_filter *zsv_dir_filter,
                    unsigned char verbose
                    ) {
  int err = 0;
  FILE *fdest = output_filename ? fopen((const char *)output_filename, "wb") : stdout;
  if(!fdest)
    err = errno, perror((const char *)output_filename);
  else {
    struct zsv_dir_foreach_to_json_ctx ctx = { 0 };
    ctx.zsv_dir_filter = *zsv_dir_filter;
    ctx.parent_dir = parent_dir;

    // use a jq filter to pretty-print
    ctx.jctx.write1 = zsv_jq_fwrite1;
    ctx.jctx.ctx = fdest;
    ctx.jctx.flags = JV_PRINT_PRETTY | JV_PRINT_SPACE1;
    enum zsv_jq_status jqstat;
    ctx.zjq = zsv_jq_new((const unsigned char *)".", jv_to_json_func, &ctx.jctx, &jqstat);
    if(!ctx.zjq)
      err = 1, fprintf(stderr, "zsv_jq_new\n");
    else {
      if(jqstat == zsv_jq_status_ok && zsv_jq_parse(ctx.zjq, "{", 1) == zsv_jq_status_ok) {
        // export each file
        zsv_foreach_dirent((const char *)parent_dir, ctx.zsv_dir_filter.max_depth, zsv_dir_foreach_to_json,
                           &ctx, verbose);
        if(!ctx.err && zsv_jq_parse(ctx.zjq, "}", 1))
          ctx.err = 1;
        if(!ctx.err && zsv_jq_finish(ctx.zjq))
          ctx.err = 1;
        zsv_jq_delete(ctx.zjq);
      }
      err = ctx.err;
    }
    fclose(fdest);
  }
  return err;
}
