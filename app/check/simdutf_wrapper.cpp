// simdutf_wrapper.cpp - C++ wrapper, exposes C ABI

#include "../external/simdutf/simdutf.h"
#include "../external/simdutf/simdutf.cpp"

extern "C" int simdutf_is_valid_utf8(const char *buf, size_t len) {
    // simdutf::validate_utf8 is part of the public API :contentReference[oaicite:1]{index=1}
    bool ok = simdutf::validate_utf8(buf, len);
    return ok ? 1 : 0;
}

