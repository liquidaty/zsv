// x86_64-w64-mingw32-gcc -O3 -mavx2 -mpopcnt -o count-balanced-quotes-x86_64.exe count-balanced-quotes-x86_64.c

#include <stdio.h>
#include <stdint.h>
#include <immintrin.h> // AVX2 header
#include <fcntl.h>     // For file mode constants
#include <io.h>        // For _setmode

#define BUF_SIZE (256 * 1024)

uint64_t process_buffer(const uint8_t *data, size_t len, int *inside_quote_global) {
    uint64_t total_count = 0;
    size_t i = 0;
    
    // Global state: 0 if currently outside quotes, ~0ULL (all 1s) if inside
    uint64_t quote_state = *inside_quote_global ? ~0ULL : 0ULL;

    // Pre-load constants into registers
    __m256i v_newline = _mm256_set1_epi8('\n');
    __m256i v_quote   = _mm256_set1_epi8('"');

    // Process 64 bytes at a time (Two 32-byte AVX2 vectors)
    for (; i + 64 <= len; i += 64) {
        // 1. Load data
        __m256i b0 = _mm256_loadu_si256((const __m256i*)(data + i));
        __m256i b1 = _mm256_loadu_si256((const __m256i*)(data + i + 32));

        // 2. Compare and Movemask
        // _mm256_movemask_epi8 creates a 32-bit integer directly from the vector
        uint32_t n_mask0 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(b0, v_newline));
        uint32_t n_mask1 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(b1, v_newline));

        uint32_t q_mask0 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(b0, v_quote));
        uint32_t q_mask1 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(b1, v_quote));

        // 3. Combine into 64-bit integers
        // Note: x86 is Little Endian, so the first 32 bytes are the lower bits
        uint64_t newlines = (uint64_t)n_mask0 | ((uint64_t)n_mask1 << 32);
        uint64_t quotes   = (uint64_t)q_mask0 | ((uint64_t)q_mask1 << 32);

        // 4. Prefix XOR (Same logic as ARM version)
        // Calculates "inside quote" status relative to the start of this block
        uint64_t internal_mask = quotes;
        internal_mask ^= (internal_mask << 1);
        internal_mask ^= (internal_mask << 2);
        internal_mask ^= (internal_mask << 4);
        internal_mask ^= (internal_mask << 8);
        internal_mask ^= (internal_mask << 16);
        internal_mask ^= (internal_mask << 32);

        // 5. Apply carry-over state
        uint64_t final_inside_mask = internal_mask ^ quote_state;

        // 6. Count valid newlines (newlines & NOT inside)
        // _mm_popcnt_u64 is the hardware instruction for population count
        total_count += _mm_popcnt_u64(newlines & ~final_inside_mask);

        // 7. Update global state
        if (_mm_popcnt_u64(quotes) & 1) {
            quote_state = ~quote_state;
        }
    }

    *inside_quote_global = (quote_state != 0);

    // Tail handling
    for (; i < len; i++) {
        if (data[i] == '"') *inside_quote_global = !(*inside_quote_global);
        else if (data[i] == '\n' && !(*inside_quote_global)) total_count++;
    }

    return total_count;
}

int main() {
    // Force stdin to binary mode (Essential for Windows/MinGW)
    _setmode(_fileno(stdin), _O_BINARY);

    // Align buffer to 32 bytes for optimal AVX load performance
    static uint8_t buffer[BUF_SIZE] __attribute__((aligned(32)));
    uint64_t total_newlines = 0;
    int inside_quote = 0;
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, BUF_SIZE, stdin)) > 0) {
        total_newlines += process_buffer(buffer, bytes_read, &inside_quote);
    }

    printf("%llu\n", total_newlines);
    return 0;
}
