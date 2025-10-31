#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "./pcre2-8.h"

/**
 * @brief Reads an entire file into a new buffer.
 *
 * @param filename The name of the file to read.
 * @param out_size Pointer to a size_t to store the file size.
 * @return A new buffer containing the file contents (must be free'd) or NULL on error.
 */
char *read_file_to_buffer(const char *filename, size_t *out_size) {
  const char *tmp_filename = "./pcre2-8-test.temp";
  if (!strcmp(filename, "-")) { // write stdin to temp file
    FILE *tmp = fopen(tmp_filename, "wb");
    if (!tmp) {
      perror(tmp_filename);
      return NULL;
    }
    size_t sz;
    char buff[4096];
    while ((sz = fread(buff, 1, sizeof(buff), stdin)) > 0)
      fwrite(buff, 1, sz, tmp);
    fclose(tmp);
    filename = tmp_filename;
  }
  FILE *f = fopen(filename, "rb");
  if (f == NULL) {
    perror(filename);
    return NULL;
  }

  // Seek to end to find file size
  if (fseek(f, 0, SEEK_END) != 0) {
    perror("Error seeking in file");
    fclose(f);
    return NULL;
  }

  long file_size = ftell(f);
  if (file_size == -1) {
    perror("Error getting file size");
    fclose(f);
    return NULL;
  }
  rewind(f); // Go back to the start

  // Allocate buffer for file content + 1 for a null terminator
  char *buffer = malloc(file_size + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Error: Could not allocate memory for file.\n");
    fclose(f);
    return NULL;
  }

  // Read the file
  size_t bytes_read = fread(buffer, 1, file_size, f);
  if (bytes_read != (size_t)file_size) {
    fprintf(stderr, "Error reading file (read %zu, expected %ld).\n", bytes_read, file_size);
    fclose(f);
    free(buffer);
    return NULL;
  }

  fclose(f);
  buffer[file_size] = '\0'; // Null-terminate the string
  *out_size = (size_t)file_size;
  return buffer;
}

/**
 * @brief Main entry point.
 *
 * Takes a regex pattern and a filename as command-line arguments.
 */
int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <pattern> <filename, or - for stdin>\n", argv[0]);
    return 1;
  }

  const char *pattern_string = argv[1];
  const char *filename = argv[2];

  // Read file into memory
  size_t file_size = 0;
  char *file_content = read_file_to_buffer(filename, &file_size);
  if (file_content == NULL) {
    return 1; // Error already printed
  }

  // Compile the pattern (pass 0 for default options,
  // PCRE2_UTF and PCRE2_MULTILINE are added by the library)
  regex_handle_t *handle = zsv_pcre2_8_new(pattern_string, 0);
  if (handle == NULL) {
    fprintf(stderr, "Error compiling regex pattern.\n");
    free(file_content);
    return 1;
  }

  // Check for anchors
  if (zsv_pcre2_8_has_anchors(pattern_string)) {
    printf("Pattern contains ^ or $ line anchors.\n");
  } else {
    printf("Pattern does not contain line anchors.\n");
  }

  // Perform the match
  int result = zsv_pcre2_8_match(handle, file_content, file_size);

  if (result) {
    printf("Match\n");
  } else {
    printf("No Match\n");
  }

  // Clean up
  zsv_pcre2_8_delete(handle);
  free(file_content);

  return 0;
}
