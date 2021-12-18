/*
 * Copyright (C) 2021 Tai Chi Minh Ralph Eastwood
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_UTILS_COMPILER
#define ZSV_UTILS_COMPILER

#ifndef LIKELY
#define LIKELY(x) __builtin_expect(x, 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(x, 0)
#endif

#endif
