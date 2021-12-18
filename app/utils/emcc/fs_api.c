#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
  
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <zsv/utils/emcc/fs_api.h>

struct fs_data {
  FILE *f;
};

EMSCRIPTEN_KEEPALIVE
fshandle fsopen(const char *fn, const char *mode) {
  struct fs_data *d = calloc(1, sizeof(*d));
  if(d)
    d->f = fopen(fn, mode);
  return d;
}

EMSCRIPTEN_KEEPALIVE
int fsclose(fshandle h){
  if(h) {
    int rc = fclose(h->f);
    free(h);
    return rc;
  }
  return 1;
}

EMSCRIPTEN_KEEPALIVE
size_t fswrite(const void *ptr, size_t n, size_t m, fshandle h) {
  return fwrite(ptr, n, m, h->f);
}

EMSCRIPTEN_KEEPALIVE
void stdflush(int addNewline) {
  fflush(stdout);
  if(addNewline)
    fprintf(stdout, "\n");

  fflush(stderr);
  if(addNewline)
    fprintf(stderr, "\n");
}

/*
EMSCRIPTEN_KEEPALIVE
size_t bytesum(const char *fn) {
  size_t sum = 0;
  FILE *f = fopen(fn, "rb");
  if(f) {
    char buff[1024];
    size_t bytes_read;
    while((bytes_read = fread(buff, 1, sizeof(buff), f))) {
      for(size_t i = 0; i < bytes_read; i++) {
        sum += buff[i];
        sum = sum % 1000000000;
      }
    }
    fclose(f);
  }
  return sum;
}
*/

EMSCRIPTEN_KEEPALIVE
size_t fsread(void *ptr, size_t n, size_t m, fshandle h) {
  return fread(ptr, n, m, h->f);
}

EMSCRIPTEN_KEEPALIVE
int fsrm(const char *fn) {
  return unlink(fn);
}

EMSCRIPTEN_KEEPALIVE
int fsmkdir(const char *path) {
#ifdef _WIN32
  return mkdir(path);
#endif
  return mkdir(path, S_IRWXU);
}

EMSCRIPTEN_KEEPALIVE
int fsprint(const char *s) {
  fwrite(s, 1, strlen(s), stdout);
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int fsprinterr(const char *s) {
  fwrite(s, 1, strlen(s), stderr);
  fprintf(stderr, "printed fsprinterr\n");
  return 0;
}


/**
 * return single string of entries delimited by newline
 * caller must free()
 */
EMSCRIPTEN_KEEPALIVE
char *fsls(const char *dir) {
  struct dirent *de;  // Pointer for directory entry
  // opendir() returns a pointer of DIR type. 
  if(!dir || !*dir)
    dir = ".";
  DIR *dr = opendir(dir);
  if(!dr)
    fprintf(stderr, "Could not open current directory" );
  else {
    size_t total_mem = 0;
    struct tmp_str_list {
      struct tmp_str_list *next;
      char *c;
    };
    struct tmp_str_list *head = NULL, **tail = &head;
    while ((de = readdir(dr)) != NULL) { // see http://pubs.opengroup.org/onlinepubs/7990989775/xsh/readdir.html
      struct tmp_str_list *tmp = malloc(sizeof(*tmp));
      if(tmp) {
        tmp->next = NULL;
        tmp->c = strdup(de->d_name);
        if(!tmp->c)
          free(tmp);
        else {
          total_mem += (1 + strlen(tmp->c));
          *tail = tmp;
          tail = &tmp->next;
        }
      }
    }

    char *final_result = NULL;
    if(total_mem && (final_result = malloc(total_mem + 1))) {
      size_t offset = 0;
      for(struct tmp_str_list *tmp = head, *next; tmp; tmp = next) {
        next = tmp->next;
        size_t len = strlen(tmp->c);
        memcpy(final_result + offset, tmp->c, len);
        offset += len + 1;
        final_result[offset-1] = '\n';
        free(tmp->c);
        free(tmp);
      }
      final_result[total_mem-1] = '\0';
    }
  
    closedir(dr);    
    return final_result;
  }
  return NULL;
}
