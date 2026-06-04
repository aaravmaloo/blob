#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#include <process.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#endif

// SHA-256 implementation
#define ROTRIGHT(word,bits) (((word) >> (bits)) | ((word) << (32-(bits))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) | ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    for ( ; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
    uint32_t i = ctx->datalen;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56)
            ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64)
            ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i]      = (uint8_t)(ctx->state[0] >> (24 - i * 8));
        hash[i + 4]  = (uint8_t)(ctx->state[1] >> (24 - i * 8));
        hash[i + 8]  = (uint8_t)(ctx->state[2] >> (24 - i * 8));
        hash[i + 12] = (uint8_t)(ctx->state[3] >> (24 - i * 8));
        hash[i + 16] = (uint8_t)(ctx->state[4] >> (24 - i * 8));
        hash[i + 20] = (uint8_t)(ctx->state[5] >> (24 - i * 8));
        hash[i + 24] = (uint8_t)(ctx->state[6] >> (24 - i * 8));
        hash[i + 28] = (uint8_t)(ctx->state[7] >> (24 - i * 8));
    }
}

// HMAC-SHA256 implementation
static void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32]) {
    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    uint8_t temp_key[32];

    if (key_len > 64) {
        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, temp_key);
        key = temp_key;
        key_len = 32;
    }

    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);

    for (int i = 0; i < 64; i++) {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5c;
    }

    uint8_t inner_hash[32];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, out);
}

// ChaCha20 implementation
#define ROTL(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
#define QR(a,b,c,d) ( \
    a += b, d ^= a, d = ROTL(d,16), \
    c += d, b ^= c, b = ROTL(b,12), \
    a += b, d ^= a, d = ROTL(d, 8), \
    c += d, b ^= c, b = ROTL(b, 7))

static void chacha20_block(uint32_t out[16], const uint32_t in[16]) {
    int i;
    for (i = 0; i < 16; ++i) out[i] = in[i];
    for (i = 0; i < 10; ++i) {
        QR(out[0], out[4], out[8], out[12]);
        QR(out[1], out[5], out[9], out[13]);
        QR(out[2], out[6], out[10], out[14]);
        QR(out[3], out[7], out[11], out[15]);
        QR(out[0], out[5], out[10], out[15]);
        QR(out[1], out[6], out[11], out[12]);
        QR(out[2], out[7], out[8], out[13]);
        QR(out[3], out[4], out[9], out[14]);
    }
    for (i = 0; i < 16; ++i) out[i] += in[i];
}

static void chacha20_crypt(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t *data, size_t len) {
    uint32_t ctx[16];
    uint32_t block[16];
    uint8_t keystream[64];
    size_t i, j;

    ctx[0] = 0x61707865;
    ctx[1] = 0x3320646e;
    ctx[2] = 0x79622d32;
    ctx[3] = 0x6b206574;

    for (i = 0; i < 8; ++i) {
        ctx[4 + i] = ((uint32_t)key[i*4]) |
                     ((uint32_t)key[i*4 + 1] << 8) |
                     ((uint32_t)key[i*4 + 2] << 16) |
                     ((uint32_t)key[i*4 + 3] << 24);
    }

    ctx[12] = counter;
    for (i = 0; i < 3; ++i) {
        ctx[13 + i] = ((uint32_t)nonce[i*4]) |
                      ((uint32_t)nonce[i*4 + 1] << 8) |
                      ((uint32_t)nonce[i*4 + 2] << 16) |
                      ((uint32_t)nonce[i*4 + 3] << 24);
    }

    for (i = 0; i < len; i += 64) {
        chacha20_block(block, ctx);
        for (j = 0; j < 16; ++j) {
            keystream[j*4]     = (uint8_t)(block[j]);
            keystream[j*4 + 1] = (uint8_t)(block[j] >> 8);
            keystream[j*4 + 2] = (uint8_t)(block[j] >> 16);
            keystream[j*4 + 3] = (uint8_t)(block[j] >> 24);
        }
        size_t block_len = (len - i > 64) ? 64 : len - i;
        for (j = 0; j < block_len; ++j) {
            data[i + j] ^= keystream[j];
        }
        ctx[12]++;
    }
}

// PBKDF2 stretching - 20,000 rounds for SOTA security
static void pbkdf2_sha256_simple(const char *pass, size_t pass_len, const uint8_t salt[16], uint8_t out_key[32]) {
    uint8_t temp[32];
    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, salt, 16);
    sha256_update(&ctx, (const uint8_t*)pass, pass_len);
    sha256_final(&ctx, temp);

    for (int i = 1; i < 20000; ++i) {
        sha256_init(&ctx);
        sha256_update(&ctx, temp, 32);
        sha256_final(&ctx, temp);
    }

    memcpy(out_key, temp, 32);
}

static void bin_to_hex(const uint8_t *bin, size_t len, char *hex) {
    for (size_t i = 0; i < len; ++i) {
        sprintf(hex + (i * 2), "%02x", bin[i]);
    }
}

static size_t hex_to_bin(const char *hex, uint8_t *bin, size_t max_len) {
    size_t len = strlen(hex);
    if (len % 2 != 0) return 0;
    size_t bin_len = len / 2;
    if (bin_len > max_len) bin_len = max_len;

    for (size_t i = 0; i < bin_len; ++i) {
        unsigned int val;
        sscanf(hex + (i * 2), "%02x", &val);
        bin[i] = (uint8_t)val;
    }
    return bin_len;
}

static int constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len) {
    int result = 0;
    for (size_t i = 0; i < len; i++) {
        result |= a[i] ^ b[i];
    }
    return result == 0;
}

static void generate_entropy(uint8_t *buf, size_t len) {
    size_t done = 0;
#ifndef _WIN32
    FILE *f = fopen("/dev/urandom", "r");
    if (f) {
        done = fread(buf, 1, len, f);
        fclose(f);
    }
#endif
    if (done < len) {
        unsigned int seed = (unsigned int)time(NULL);
#ifdef _WIN32
        seed ^= (unsigned int)_getpid();
#else
        seed ^= (unsigned int)getpid();
#endif
        srand(seed);
        for (size_t i = done; i < len; ++i) {
            buf[i] = rand() % 256;
        }
    }
}

static void get_password(const char *prompt, char *buf, size_t max_len) {
    printf("%s", prompt);
    fflush(stdout);
#ifdef _WIN32
    size_t i = 0;
    while (i + 1 < max_len) {
        int c = _getch();
        if (c == '\r' || c == '\n') {
            break;
        }
        if (c == '\b' || c == 127) {
            if (i > 0) {
                i--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (c >= 32 && c <= 126) {
            buf[i++] = (char)c;
            printf("*");
            fflush(stdout);
        }
    }
    buf[i] = '\0';
    printf("\n");
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    if (fgets(buf, (int)max_len, stdin)) {
        buf[strcspn(buf, "\r\n")] = '\0';
    } else {
        buf[0] = '\0';
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
#endif
}

#define MAGIC_HEADER_V1 "--- BLOB CRYPT V1 ---"
#define MAGIC_HEADER_V2 "--- BLOB CRYPT V2 ---"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: could not open note file %s\n", path);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        fclose(f);
        fprintf(stderr, "Error: invalid file size\n");
        return 1;
    }

    char *content = malloc(fsize + 1);
    if (!content) {
        fclose(f);
        fprintf(stderr, "Error: memory allocation failed\n");
        return 1;
    }

    size_t read_bytes = fread(content, 1, fsize, f);
    content[read_bytes] = '\0';
    fclose(f);

    int is_locked_v2 = (strncmp(content, MAGIC_HEADER_V2, strlen(MAGIC_HEADER_V2)) == 0);
    int is_locked_v1 = (strncmp(content, MAGIC_HEADER_V1, strlen(MAGIC_HEADER_V1)) == 0);

    if (is_locked_v2 || is_locked_v1) {
        // UNLOCK FLOW
        char *p_magic = content + (is_locked_v2 ? strlen(MAGIC_HEADER_V2) : strlen(MAGIC_HEADER_V1));
        while (*p_magic == '\r' || *p_magic == '\n') p_magic++;

        char salt_hex[33];
        char nonce_hex[25];
        char hmac_hex[65] = "";
        char *ciphertext_hex = NULL;

        if (is_locked_v2) {
            if (sscanf(p_magic, "%32s\n%24s\n%64s", salt_hex, nonce_hex, hmac_hex) != 3) {
                fprintf(stderr, "Error: invalid V2 encrypted note format\n");
                free(content);
                return 1;
            }
            char *nl1 = strchr(p_magic, '\n');
            if (!nl1) { free(content); return 1; }
            nl1++;
            char *nl2 = strchr(nl1, '\n');
            if (!nl2) { free(content); return 1; }
            nl2++;
            char *nl3 = strchr(nl2, '\n');
            if (!nl3) { free(content); return 1; }
            ciphertext_hex = nl3 + 1;
        } else {
            if (sscanf(p_magic, "%32s\n%24s", salt_hex, nonce_hex) != 2) {
                fprintf(stderr, "Error: invalid V1 encrypted note format\n");
                free(content);
                return 1;
            }
            char *nl1 = strchr(p_magic, '\n');
            if (!nl1) { free(content); return 1; }
            nl1++;
            char *nl2 = strchr(nl1, '\n');
            if (!nl2) { free(content); return 1; }
            ciphertext_hex = nl2 + 1;
        }

        while (*ciphertext_hex == '\r' || *ciphertext_hex == '\n') ciphertext_hex++;
        size_t c_len = strlen(ciphertext_hex);
        while (c_len > 0 && (ciphertext_hex[c_len - 1] == '\r' || ciphertext_hex[c_len - 1] == '\n' || ciphertext_hex[c_len - 1] == ' ')) {
            ciphertext_hex[--c_len] = '\0';
        }

        uint8_t salt[16];
        uint8_t nonce[12];
        hex_to_bin(salt_hex, salt, 16);
        hex_to_bin(nonce_hex, nonce, 12);

        size_t cipher_bytes_len = c_len / 2;
        uint8_t *ciphertext = malloc(cipher_bytes_len + 1);
        if (!ciphertext) {
            fprintf(stderr, "Error: memory allocation failed\n");
            free(content);
            return 1;
        }
        hex_to_bin(ciphertext_hex, ciphertext, cipher_bytes_len);

        char password[256];
        get_password("Enter password to unlock: ", password, sizeof(password));

        uint8_t derived_key[32];
        // Dynamic stretching rounds: V2 uses 20k, V1 uses 1k
        if (is_locked_v2) {
            pbkdf2_sha256_simple(password, strlen(password), salt, derived_key);
        } else {
            // V1 PBKDF2 (1000 rounds)
            uint8_t temp[32];
            SHA256_CTX temp_ctx;
            sha256_init(&temp_ctx);
            sha256_update(&temp_ctx, salt, 16);
            sha256_update(&temp_ctx, (const uint8_t*)password, strlen(password));
            sha256_final(&temp_ctx, temp);
            for (int i = 1; i < 1000; ++i) {
                sha256_init(&temp_ctx);
                sha256_update(&temp_ctx, temp, 32);
                sha256_final(&temp_ctx, temp);
            }
            memcpy(derived_key, temp, 32);
        }

        uint8_t enc_key[32];
        uint8_t mac_key[32];

        if (is_locked_v2) {
            // Deriving subkeys
            hmac_sha256(derived_key, 32, (const uint8_t *)"enc_key", 7, enc_key);
            hmac_sha256(derived_key, 32, (const uint8_t *)"mac_key", 7, mac_key);

            // Verify HMAC before decrypting to prevent tampered writes
            uint8_t expected_hmac[32];
            hex_to_bin(hmac_hex, expected_hmac, 32);

            uint8_t calculated_hmac[32];
            hmac_sha256(mac_key, 32, ciphertext, cipher_bytes_len, calculated_hmac);

            if (!constant_time_compare(expected_hmac, calculated_hmac, 32)) {
                fprintf(stderr, "Incorrect password na.\n");
                free(ciphertext);
                free(content);
                return 1;
            }
        } else {
            memcpy(enc_key, derived_key, 32);
        }

        // Decrypt
        chacha20_crypt(enc_key, nonce, 0, ciphertext, cipher_bytes_len);
        ciphertext[cipher_bytes_len] = '\0';

        if (is_locked_v1) {
            // Fallback warning check for V1 non-authenticated format
            if (cipher_bytes_len > 0 && ciphertext[0] != '#') {
                fprintf(stderr, "Incorrect password na.\n");
                free(ciphertext);
                free(content);
                return 1;
            }
        }

        FILE *out = fopen(path, "wb");
        if (!out) {
            fprintf(stderr, "Error: could not write decrypted note to disk\n");
            free(ciphertext);
            free(content);
            return 1;
        }
        fwrite(ciphertext, 1, cipher_bytes_len, out);
        fclose(out);

        printf("Note unlocked successfully!\n");
        free(ciphertext);
    } else {
        // LOCK FLOW (Always lock in SOTA V2 format)
        char password[256];
        char confirm[256];

        get_password("Enter password to lock note: ", password, sizeof(password));
        if (strlen(password) == 0) {
            fprintf(stderr, "Error: password cannot be empty.\n");
            free(content);
            return 1;
        }
        get_password("Confirm password: ", confirm, sizeof(confirm));

        if (strcmp(password, confirm) != 0) {
            fprintf(stderr, "Error: passwords do not match.\n");
            free(content);
            return 1;
        }

        uint8_t salt[16];
        uint8_t nonce[12];
        generate_entropy(salt, 16);
        generate_entropy(nonce, 12);

        uint8_t derived_key[32];
        pbkdf2_sha256_simple(password, strlen(password), salt, derived_key);

        uint8_t enc_key[32];
        uint8_t mac_key[32];
        hmac_sha256(derived_key, 32, (const uint8_t *)"enc_key", 7, enc_key);
        hmac_sha256(derived_key, 32, (const uint8_t *)"mac_key", 7, mac_key);

        size_t plain_len = strlen(content);
        uint8_t *plain_bytes = (uint8_t *)content;

        // Encrypt with ChaCha20 using enc_key
        chacha20_crypt(enc_key, nonce, 0, plain_bytes, plain_len);

        // Sign ciphertext using mac_key
        uint8_t hmac_val[32];
        hmac_sha256(mac_key, 32, plain_bytes, plain_len, hmac_val);

        char salt_hex[33];
        char nonce_hex[25];
        char hmac_hex[65];
        bin_to_hex(salt, 16, salt_hex);
        bin_to_hex(nonce, 12, nonce_hex);
        bin_to_hex(hmac_val, 32, hmac_hex);

        char *ciphertext_hex = malloc(plain_len * 2 + 1);
        if (!ciphertext_hex) {
            fprintf(stderr, "Error: memory allocation failed\n");
            free(content);
            return 1;
        }
        bin_to_hex(plain_bytes, plain_len, ciphertext_hex);

        FILE *out = fopen(path, "wb");
        if (!out) {
            fprintf(stderr, "Error: could not write encrypted note to disk\n");
            free(ciphertext_hex);
            free(content);
            return 1;
        }

        fprintf(out, "%s\n%s\n%s\n%s\n%s\n", MAGIC_HEADER_V2, salt_hex, nonce_hex, hmac_hex, ciphertext_hex);
        fclose(out);

        printf("Note locked successfully!\n");
        free(ciphertext_hex);
    }

    free(content);
    return 0;
}
