// Reference vectors for zsv/utils/rng: pin the generator so seeded expected-output
// fixtures are verifiable against the published algorithms, not just "what the code printed".
// Exit status 0 on success; prints the failing check otherwise
#include <stdio.h>
#include <zsv/utils/rng.h>

static int fail(const char *what, uint64_t got, uint64_t want) {
  fprintf(stderr, "rng: %s: got %llx want %llx\n", what, (unsigned long long)got, (unsigned long long)want);
  return 1;
}

int main(void) {
  struct zsv_rng r;

  // xoshiro256** reference: state {1,2,3,4} -> 11520, 0, 1509978240, 1215971899390074240
  r.s[0] = 1, r.s[1] = 2, r.s[2] = 3, r.s[3] = 4;
  const uint64_t ref[] = {11520ULL, 0ULL, 1509978240ULL, 1215971899390074240ULL};
  for (int i = 0; i < 4; i++) {
    uint64_t v = zsv_rng_next(&r);
    if (v != ref[i])
      return fail("xoshiro256** reference output", v, ref[i]);
  }

  // splitmix64 reference: seed 0 -> first output 0xe220a8397b1dcdaf; zsv_rng_seed() fills s[0] with it
  zsv_rng_seed(&r, 0);
  if (r.s[0] != 0xe220a8397b1dcdafULL)
    return fail("splitmix64 reference output", r.s[0], 0xe220a8397b1dcdafULL);

  // bounded draws stay in range across the domain, including the rejection-heavy mask edge
  const uint64_t bounds[] = {1, 2, 3, 1000000, (1ULL << 63) + 1, UINT64_MAX};
  for (unsigned b = 0; b < sizeof(bounds) / sizeof(*bounds); b++)
    for (int i = 0; i < 100000; i++) {
      uint64_t v = zsv_rng_below(&r, bounds[b]);
      if (v >= bounds[b])
        return fail("zsv_rng_below out of range", v, bounds[b]);
    }
  if (zsv_rng_below(&r, 0) != 0)
    return fail("zsv_rng_below(0)", 1, 0);

  // seeding is deterministic and re-seeding resets the stream
  struct zsv_rng a, b;
  zsv_rng_seed(&a, 42);
  zsv_rng_seed(&b, 42);
  uint64_t x = zsv_rng_next(&a), y = zsv_rng_next(&b);
  if (x != y)
    return fail("same seed, same stream", x, y);
  zsv_rng_seed(&a, 42);
  if (zsv_rng_next(&a) != x)
    return fail("re-seed resets stream", 0, x);

  // entropy is not a constant
  uint64_t e1 = zsv_rng_entropy(), e2 = zsv_rng_entropy();
  if (e1 == e2)
    return fail("entropy repeated", e1, e2);
  return 0;
}
