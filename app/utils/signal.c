/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>

volatile sig_atomic_t zsv_signal_interrupted = 0;

#ifdef _WIN32
#include <windows.h>

static int consoleHandler(DWORD signal) {
  if(signal == CTRL_C_EVENT) {
    if(!zsv_signal_interrupted) {
      zsv_signal_interrupted = 1;
      fclose(stdin);
      return 1;
    }
  }
  return 0;
}

void zsv_handle_ctrl_c_signal() {
  if(!SetConsoleCtrlHandler(consoleHandler, 1))
    fprintf(stderr, "Warning: unable to set signal handler\n");
}

#else

// handle signal interrupts: first Ctrl-C sets interrupt
// second reverts to default Ctrl-C behavior

static void INThandler(int sig) {
  signal(sig, SIG_IGN); // ignore
  if(!zsv_signal_interrupted) {
    zsv_signal_interrupted = 1;
    signal(SIGINT, NULL); // restore default handler
  }
}

void zsv_handle_ctrl_c_signal() {
  // sigaction only ADDs the handler instead of REPLACING it
  // so we will use signal() instead
  signal(SIGINT, INThandler);
}

#endif
