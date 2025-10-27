#include <windows.h>
#include <io.h>     // For _open_osfhandle, _fdopen
#include <fcntl.h>  // For _O_xxx flags
#include <stdio.h>  // For FILE*, fopen modes, fclose etc.
#include <wchar.h>  // For wchar_t
#include <stdlib.h> // For malloc, free

#include "io.h"

FILE *zsv_fopen_longpath(const char *utf8_path, const char *mode) {
  wchar_t *wide_path_prefixed;
  DWORD dwDesiredAccess = 0;
  DWORD dwShareMode = FILE_SHARE_READ; // Example default
  DWORD dwCreationDisposition = 0;
  int c_runtime_flags = 0;
  if (zsv_pathToPrefixedWidePath(utf8_path, &wide_path_prefixed))
    return NULL;

  // --- 2. Determine CreateFileW params and _open_osfhandle flags from mode string ---
  if (strchr(mode, 'w')) {
    dwDesiredAccess |= GENERIC_WRITE;
    dwCreationDisposition = CREATE_ALWAYS; // Overwrite or create
    c_runtime_flags |= _O_WRONLY;
  } else if (strchr(mode, 'a')) {
    dwDesiredAccess |= GENERIC_WRITE | FILE_APPEND_DATA; // Or just GENERIC_WRITE and seek later
    dwCreationDisposition = OPEN_ALWAYS;                 // Append or create
    c_runtime_flags |= _O_WRONLY | _O_APPEND;
  } else { // Default to read
    dwDesiredAccess |= GENERIC_READ;
    dwCreationDisposition = OPEN_EXISTING;
    c_runtime_flags |= _O_RDONLY;
  }

  if (strchr(mode, '+')) { // Read and write modes
    dwDesiredAccess |= GENERIC_READ | GENERIC_WRITE;
    c_runtime_flags &= ~(_O_RDONLY | _O_WRONLY); // Clear read/write only flags
    c_runtime_flags |= _O_RDWR;
    if (!strchr(mode, 'w')) {              // If it wasn't 'w+', it implies 'r+' or 'a+'
      dwCreationDisposition = OPEN_ALWAYS; // Make sure it opens if exists
    }
  }

  if (strchr(mode, 'b')) {
    c_runtime_flags |= _O_BINARY;
  } else {
    c_runtime_flags |= _O_TEXT;
  }

  // --- 3. Call CreateFileW ---
  HANDLE hFile = CreateFileW(wide_path_prefixed, dwDesiredAccess, dwShareMode,
                             NULL, // Default security attributes
                             dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);

  free(wide_path_prefixed); // Don't need the string anymore

  if (hFile == INVALID_HANDLE_VALUE) {
    // Handle CreateFileW error (use GetLastError())
    return NULL;
  }

  // --- 4. Convert HANDLE to C file descriptor ---
  int fd = _open_osfhandle((intptr_t)hFile, c_runtime_flags);
  if (fd == -1) {
    // Handle _open_osfhandle error
    CloseHandle(hFile); // Crucial: Close handle if fd conversion fails!
    return NULL;
  }

  // --- 5. Convert file descriptor to FILE* stream ---
  FILE *fp = _fdopen(fd, mode);
  if (fp == NULL) {
    // Handle _fdopen error
    // Note: If _fdopen fails, the C runtime might or might not have
    // taken ownership and closed the fd/handle. It's safer to assume
    // it might not have, but closing the original hFile here is risky
    // as _fdopen might have associated it. Usually, _close(fd) is
    // appropriate here IF _fdopen fails, as _close *should* release the handle.
    _close(fd); // Close the C descriptor; this should close the underlying handle.
    return NULL;
  }

  // --- 6. Success ---
  return fp;
  // Remember: fclose(fp) will clean up fd and hFile later.
}

#ifdef ZSV_DIRS_FOPEN_LONGPATH_TEST

#include "win/io.c"

int main(int argc, char *argv[]) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  For writing: %s w <absolute_filepath> [content_to_write]\n", argv[0]);
    fprintf(stderr, "  For reading: %s r <absolute_filepath>\n", argv[0]);
    fprintf(stderr, "\nArguments:\n");
    fprintf(stderr, "  mode: 'w' (write - overwrites/creates) or 'r' (read)\n");
    fprintf(stderr, "  absolute_filepath: The full, absolute path to the file (can exceed MAX_PATH).\n");
    fprintf(stderr, "                     Use quotes if the path contains spaces.\n");
    fprintf(stderr, "  content_to_write (optional): Text to write to the file if mode is 'w'.\n");
    fprintf(stderr, "                             If omitted in write mode, default text is written.\n");
    fprintf(stderr, "\nExample (Write):\n");
    fprintf(stderr, "  %s w \"C:\\Temp\\Long Path Folder\\very_long_filename_that_works.txt\" \"Hello Long World!\"\n",
            argv[0]);
    fprintf(stderr, "Example (Read):\n");
    fprintf(stderr, "  %s r \"\\\\?\\C:\\Temp\\Long Path Folder\\very_long_filename_that_works.txt\"\n", argv[0]);
    return 1;
  }

  const char *mode_arg = argv[1];
  const char *filepath_arg = argv[2];
  const char *content_arg = (argc == 4 && strcmp(mode_arg, "w") == 0) ? argv[3] : NULL;
  const char *default_content = "Default content written by fopen_longpath test.\n";

  char file_mode[4] = ""; // e.g., "rb", "wt", etc. (Max 3 chars + null)

  if (strcmp(mode_arg, "w") == 0) {
    strcpy(file_mode, "w"); // Use text mode by default for simplicity
  } else if (strcmp(mode_arg, "r") == 0) {
    strcpy(file_mode, "r"); // Use text mode by default
  } else {
    fprintf(stderr, "Error: Invalid mode '%s'. Use 'r' or 'w'.\n", mode_arg);
    return 1;
  }

  printf("Attempting to open file: %s (Mode: %s)\n", filepath_arg, file_mode);

  FILE *fp = fopen_longpath(filepath_arg, file_mode);

  if (fp == NULL) {
    // fopen_longpath should have printed specific errors
    fprintf(stderr, "Main: Failed to open file using fopen_longpath.\n");
    return 1;
  }

  printf("Main: File opened successfully.\n");
  int operation_status = 0; // 0 for success, non-zero for error

  // --- Perform Read or Write ---
  if (strcmp(mode_arg, "w") == 0) {
    const char *content_to_write = (content_arg != NULL) ? content_arg : default_content;
    printf("Main: Writing content: \"%s\"\n", content_to_write);
    if (fputs(content_to_write, fp) == EOF) {
      perror("Main: Error writing to file");
      operation_status = 1;
    } else {
      // Add a newline if user content didn't end with one
      if (content_to_write[strlen(content_to_write) - 1] != '\n') {
        if (fputc('\n', fp) == EOF) {
          perror("Main: Error writing newline to file");
          operation_status = 1;
        }
      }
      printf("Main: Write operation successful.\n");
    }

  } else { // Mode is 'r'
    printf("Main: Reading file content:\n---\n");
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
      printf("%s", buffer); // Print line including its newline
    }
    if (ferror(fp)) {
      perror("Main: Error reading from file");
      operation_status = 1;
    } else {
      printf("\n---\nMain: Read operation finished.\n");
      // Reaching EOF without ferror is success
    }
  }

  // --- Close the file ---
  printf("Main: Closing file.\n");
  if (fclose(fp) != 0) {
    perror("Main: Error closing file");
    // Continue, but report overall failure if write/read also failed
    if (operation_status == 0)
      operation_status = 1;
  }

  return operation_status; // 0 if all successful, 1 if any error occurred
}

#endif // ZSV_DIRS_FOPEN_LONGPATH_TEST
