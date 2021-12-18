/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_DIRS_H
#define ZSV_DIRS_H

/* Maximum length of file name */
#if !defined(FILENAME_MAX)
#   define FILENAME_MAX MAX_PATH
#endif

/* file slash */
#if !defined(FILESLASH)
# ifdef _WIN32
#  define FILESLASH '\\'
# else
#  define FILESLASH '/'
# endif
#endif

size_t get_config_dir(char* buff, size_t buffsize, const char *prefix);

size_t get_executable_path(char* buff, size_t buffsize);

size_t get_app_data_dir(char *buff, size_t buffsize, const char *prefix_or_env);

size_t get_temp_dir(char *buff, size_t buffsize);

#endif
