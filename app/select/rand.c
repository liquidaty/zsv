// demo_random_bw_1_and_100(): this is a poor random number generator. you probably
// will want to use a better one
static double demo_random_bw_1_and_100(void) {
#ifdef HAVE_ARC4RANDOM_UNIFORM
  return (long double)(arc4random_uniform(1000000)) / 10000;
#else
  double max = 100.0;
  unsigned int n;
#ifdef HAVE_RAND_S
  unsigned int tries = 0;
  while (rand_s(&n) && tries++ < 10)
    ;
  return (double)n / ((double)UINT_MAX + 1) * max;
#else
  unsigned int umax = ~0;
  n = rand();
  return (double)n / ((double)(umax) + 1) * max;
#endif
#endif
}
