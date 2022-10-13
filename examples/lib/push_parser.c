#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zsv.h>

/** zsv push parser example
 *
 * This sample code shows how to use `zsv_parse_bytes()`
 * to push data through the parser, instead of using `zsv_parse_more()`
 * which pulls data from a stream
 *
 * When given a choice, is is often advantageous to use the pull
 * approach to can reduce the amount of in-memory copying
 *
 * However, in some circumstances push parsing is preferred or necessary
 *
 * In this example, we just count rows, but you could substitute in any
 * row handler you want
 **/

/**
 * Create a structure to hold our data while we parse
 * In this case, we are just going to keep track of row count
 */
struct push_parser_data {
  unsigned count;
};

/**
 * Our row handler function will take a pointer to our data
 * and increment its count by 1
 */
static void push_parser_row(void *dat) {
  struct push_parser_data *data = dat;
  data->count++;
}

/**
 * Main routine will output a help message if called with -h or --help,
 * otherwise will read from stdin and run through the CSV parser
 */
int main(int argc, const char *argv[]) {
  if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    printf("Usage: push_parser < myfile.csv\n\n");
    printf("Read from stdin and output the number of rows parsed.\n");
    printf("Uses 'push' parse method\n");
    return 0;
  }

  FILE *f = stdin; /* read from stdin */

  /**
   * create a vanilla parser
   */
  zsv_parser p = zsv_new(NULL);
  if(!p)
    fprintf(stderr, "Out of memory!");
  else {
    /**
     * Configure the parser to use our row handler, and to pass
     * it our data when it's called
     */
    struct push_parser_data d = { 0 };
    zsv_set_row_handler(p, push_parser_row);
    zsv_set_context(p, &d);

    /**
     * Allocate a buffer that we will fetch data from and pass to the parser.
     * In this example we use a heap buffer, but we could just as well
     * have allocated it on the stack
     */
    int chunk_size = 4096;
    unsigned char *buff = malloc(chunk_size);

    /**
     * Read and parse each chunk until the end of the stream is reached
     */
    while(1) {
      size_t bytes_read = fread(buff, 1, chunk_size, f);
      if(!bytes_read)
        break;
      zsv_parse_bytes(p, buff, bytes_read);
    }

    /**
     * Finish any remaining parsing
     */
    zsv_finish(p);

    /**
     * Clean up
     */
    zsv_delete(p);
    free(buff);

    /**
     * Print result
     */
    printf("Count: %u\n", d.count);
  }
}
