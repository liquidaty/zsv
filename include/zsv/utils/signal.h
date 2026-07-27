/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_SIGNAL_H
#define ZSV_SIGNAL_H

// wasm has no signals: wasi-libc's <signal.h> is a hard #error unless this is
// defined. Set it here so this installed header can be included on wasi without
// the consumer having to know; linking still requires -lwasi-emulated-signal.
#if defined(__wasi__) && !defined(_WASI_EMULATED_SIGNAL)
#define _WASI_EMULATED_SIGNAL
#endif

#include <signal.h>

extern volatile sig_atomic_t zsv_signal_interrupted;

void zsv_handle_ctrl_c_signal(void);

#endif
