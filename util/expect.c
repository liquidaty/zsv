#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/*
 * This utility allows us to wait for a particular output from zsv sheet.
 *
 * It avoids needing to call sleep with a set time and instead we specify the output
 * we wish to see and a timeout. If the timeout is exceeded then it fails and prints
 * the output that was last seen and saves it to a file.
 *
 * It repeatedly calls tmux capture-pane and compares the contents with the expected file.
 *
 * There is a wrapper for this utility which is used in the Makefiles scripts/test-expect.sh
 */

int main(int argc, char **argv) {
  if (argc != 5) {
    fprintf(stderr, "Usage: %s <expected> <pane> <actual> <timeout>\n", argv[0]);
    return 1;
  }

  char *expected = argv[1];
  char *pane = argv[2];
  char *actual = argv[3];
  char *timeout_s = argv[4];

  char *endptr;
  double timeout = strtod(timeout_s, &endptr);

  if (endptr == timeout_s) {
    perror("strtod");
    return 1;
  }

  // Read the contents of the file
  FILE *file = fopen(expected, "r");
  if (file == NULL) {
    perror("fopen");
    return 1;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    perror("fseek");
    return 1;
  }

  long file_size = ftell(file);
  if (file_size < 0) {
    perror("ftell");
    return 1;
  }

  rewind(file);

  char *expected_output = malloc(file_size + 1);
  if (expected_output == NULL) {
    perror("malloc");
    return 1;
  }

  size_t bytes_read = fread(expected_output, 1, file_size, file);
  if (bytes_read != (size_t)file_size) {
    if (ferror(file)) {
      perror("fread");
      return 1;
    }
  }

  expected_output[file_size] = '\0';

  if (fclose(file) != 0) {
    perror("fclose");
    return 1;
  }

  struct timespec start_time;
  if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
    perror("clock_gettime");
    return 1;
  }

  struct timespec current_time;
  char *expected_input = malloc(file_size + 1);
  if (expected_input == NULL) {
    perror("malloc");
    return 1;
  }

  double elapsed_time;
  char command[256];
  snprintf(command, sizeof(command), "tmux capture-pane -t %s -p", pane);

  while (1) {
    // Run tmux capture-pane -p
    FILE *pipe = popen(command, "r");
    if (pipe == NULL) {
      perror("popen");
      return 1;
    }

    size_t len = 0;
    for (int i = 0; i < 10 && len < (size_t)file_size; i++) {
      len += fread(expected_input + len, 1, file_size - len, pipe);
      if (ferror(pipe)) {
        perror("fread");
        return 1;
      }
      usleep(1);
    }

    if (pclose(pipe) != 0) {
      return 1;
    }

    // Check if the timeout has expired
    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
      perror("clock_gettime");
      return 1;
    }

    elapsed_time =
      (current_time.tv_sec - start_time.tv_sec) + (current_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

    // Check if the output matches the expected output
    if (strcmp(expected_input, expected_output) == 0) {
      break;
    }

    if (elapsed_time > timeout) {
      fprintf(stderr, "Timeout expired after %.2f seconds\n", elapsed_time);
      fprintf(stderr, "Last output that failed to match:\n%s\n", expected_input);

      FILE *output = fopen(actual, "w");
      if (output == NULL) {
        perror("fopen");
        return 1;
      }

      fprintf(output, "%s", expected_input);
      fclose(output);

      return 1;
    }

    // Sleep for a short period of time before trying again
    struct timespec sleep_time = {0, 10000000}; // 10 milliseconds
    if (nanosleep(&sleep_time, NULL) != 0) {
      perror("nanosleep");
      return 1;
    }
  }

  printf("%.2f\n", elapsed_time);

  return 0;
}
