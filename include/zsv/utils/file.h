/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_FILE_H
#define ZSV_FILE_H

#ifndef LINEEND
# if defined(WIN32) || defined(_WIN64) || defined(_WIN32)
#  define LINEEND "\r\n"
# else
#  define LINEEND "\n"
# endif
#endif // LINEEND

char *zsv_get_temp_filename(const char *prefix);


#endif
