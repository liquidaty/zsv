#include <pthread.h>

/**
 * see sqlite3_csv_vtab-mem.h for background info
 */
#if defined(_WIN32) || defined(_WIN64)
#include <process.h>
#else
#include <unistd.h>
#endif

struct sqlite3_zsv_data {
  struct sqlite3_zsv_data *next;
  pid_t pid;
  char *filename;
  struct zsv_opts opts;
  struct zsv_prop_handler custom_prop_handler;
};

pthread_mutex_t sqlite3_zsv_data_mutex;
struct sqlite3_zsv_data *sqlite3_zsv_data_g = NULL;

/**
 * Our shared memory structure should be locked for read/write
 */
static int sqlite3_zsv_data_lock(void) {
#ifndef NO_THREADING
  pthread_mutex_lock(&sqlite3_zsv_data_mutex);
#endif
  return 0;
}

static int sqlite3_zsv_data_unlock(void) {
#ifndef NO_THREADING
  pthread_mutex_unlock(&sqlite3_zsv_data_mutex);
#endif
  return 0;
}

static void sqlite3_zsv_data_delete(struct sqlite3_zsv_data *e) {
  if (e) {
    free(e->filename);
  }
  free(e);
}

void sqlite3_zsv_list_delete(void **list) {
  for (struct sqlite3_zsv_data *next, *e = *list; e; e = next) {
    next = e->next;
    sqlite3_zsv_data_delete(e);
  }
#ifndef NO_THREADING
  pthread_mutex_destroy(&sqlite3_zsv_data_mutex);
#endif
  *list = NULL;
}

static struct sqlite3_zsv_data *sqlite3_zsv_data_new(const char *filename, struct zsv_opts *opts,
                                                     struct zsv_prop_handler *custom_prop_handler) {
  if (!filename)
    return NULL;
  struct sqlite3_zsv_data *e = calloc(1, sizeof(*e));
  if (e) {
    e->pid = getpid();
    e->filename = strdup(filename);
    if (opts)
      e->opts = *opts;
    if (custom_prop_handler)
      e->custom_prop_handler = *custom_prop_handler;
    if (e->filename)
      return e;
  }
  sqlite3_zsv_data_delete(e);
  return NULL;
}

int sqlite3_zsv_list_add(const char *filename, struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler) {
  struct sqlite3_zsv_data **list = &sqlite3_zsv_data_g;
  struct sqlite3_zsv_data *e = sqlite3_zsv_data_new(filename, opts, custom_prop_handler);
  if (e) {
    struct sqlite3_zsv_data **next;
    if (sqlite3_zsv_data_lock()) {
      sqlite3_zsv_data_delete(e);
      return -1;
    } else {
      for (next = list; *next; next = &(*next)->next)
        ;
      *next = e;
      sqlite3_zsv_data_unlock();
      return 0;
    }
  }
  return ENOMEM;
}

static int sqlite3_zsv_data_cmp(struct sqlite3_zsv_data *x, const char *filename, pid_t pid) {
  return strcmp(x->filename, filename) && x->pid == pid;
}

struct sqlite3_zsv_data *sqlite3_zsv_data_find(const char *filename) {
  struct sqlite3_zsv_data *list = sqlite3_zsv_data_g;
  struct sqlite3_zsv_data *found = NULL;
  pid_t pid = getpid();
  if (!sqlite3_zsv_data_lock()) {
    for (struct sqlite3_zsv_data *e = list; e && !found; e = e->next) {
      if (!sqlite3_zsv_data_cmp(e, filename, pid))
        found = e;
    }
    if (sqlite3_zsv_data_unlock())
      fprintf(stderr, "Error unlocking sqlite3-csv-zsv shared mem lock\n");
  }
  return found;
}

int sqlite3_zsv_list_remove(const char *filename) {
  if (!filename)
    return 0;
  struct sqlite3_zsv_data **list = &sqlite3_zsv_data_g;
  struct sqlite3_zsv_data *found = NULL;
  pid_t pid = getpid();
  if (*list) {
    if (!sqlite3_zsv_data_cmp(*list, filename, pid)) {
      // found a match at the head of list
      found = *list;
      *list = found->next;
    } else {
      // look for a match somewhere after the first element
      for (struct sqlite3_zsv_data *prior = *list; prior->next != NULL; prior = prior->next) {
        if (!sqlite3_zsv_data_cmp(prior->next, filename, pid)) {
          found = prior->next;
          prior->next = prior->next->next;
          break;
        }
      }
    }
  }
  if (found) {
    sqlite3_zsv_data_delete(found);
    return 0;
  }
  return ENOENT; // not found
}
