#include "crypto.h"

#include <cstdio>
#include <cstring>

#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/bignum.h>
#include <mbedtls/cipher.h>
#include <mbedtls/cmac.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>

#include "port.h"

extern "C" {
#include "ge.h"
#include "sc.h"
}

namespace ts3c {

static int rng(void *, unsigned char *out, size_t n) {
    port_random(out, n);
    return 0;
}

void sha1(const void *d, size_t n, uint8_t out[20]) { mbedtls_sha1((const uint8_t *)d, n, out); }
void sha256(const void *d, size_t n, uint8_t out[32]) { mbedtls_sha256((const uint8_t *)d, n, out, 0); }
void sha512(const void *d, size_t n, uint8_t out[64]) { mbedtls_sha512((const uint8_t *)d, n, out, 0); }

// ---- EAX: tag = OMAC0(nonce) ^ OMAC1(ad) ^ OMAC2(ciphertext) ----

static void omac(const uint8_t key[16], uint8_t t, const uint8_t *m, size_t n, uint8_t out[16]) {
    // ponytail: 512-byte stack buffer, longer input is silently truncated; TS3 packets are <= 500 bytes.
    uint8_t buf[16 + 512] = {0};
    buf[15] = t;
    if (n > 512) n = 512;
    memcpy(buf + 16, m, n);
    mbedtls_cipher_cmac(mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB), key, 128, buf,
                        16 + n, out);
}

static void ctr(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t len) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);
    uint8_t counter[16], stream[16];
    size_t off = 0;
    memcpy(counter, iv, 16);
    mbedtls_aes_crypt_ctr(&aes, len, &off, counter, stream, data, data);
    mbedtls_aes_free(&aes);
}

static void eax_tag(const uint8_t key[16], const uint8_t n[16], const uint8_t *ad, size_t adlen,
                    const uint8_t *c, size_t len, uint8_t tag[8]) {
    uint8_t h[16], ct[16];
    omac(key, 1, ad, adlen, h);
    omac(key, 2, c, len, ct);
    for (int i = 0; i < 8; i++) tag[i] = n[i] ^ h[i] ^ ct[i];
}

void eax_encrypt(const uint8_t key[16], const uint8_t nonce[16], const uint8_t *ad, size_t adlen,
                 uint8_t *data, size_t len, uint8_t tag[8]) {
    uint8_t n[16];
    omac(key, 0, nonce, 16, n);
    ctr(key, n, data, len);
    eax_tag(key, n, ad, adlen, data, len, tag);
}

bool eax_decrypt(const uint8_t key[16], const uint8_t nonce[16], const uint8_t *ad, size_t adlen,
                 uint8_t *data, size_t len, const uint8_t tag[8]) {
    uint8_t n[16], t[8];
    omac(key, 0, nonce, 16, n);
    eax_tag(key, n, ad, adlen, data, len, t);
    uint8_t diff = 0;
    for (int i = 0; i < 8; i++) diff |= t[i] ^ tag[i];
    if (diff) return false;
    ctr(key, n, data, len);
    return true;
}

// ---- Edwards25519 helpers (ref10) ----

static const uint8_t ROOT_KEY[32] = {
    0xcd, 0x0d, 0xe2, 0xae, 0xd4, 0x63, 0x45, 0x50, 0x9a, 0x7e, 0x3c, 0xfd, 0x8f, 0x68, 0xb3, 0xdc,
    0x75, 0x55, 0xb2, 0x9d, 0xcc, 0xec, 0x73, 0xcd, 0x18, 0x75, 0x0f, 0x99, 0x38, 0x12, 0x40, 0x8a,
};

// ref10 only decodes to -P; flipping the sign bit first gives P.
static bool pt_load(ge_p3 *p, const uint8_t b[32]) {
    uint8_t t[32];
    memcpy(t, b, 32);
    t[31] ^= 0x80;
    return ge_frombytes_negate_vartime(p, t) == 0;
}

// ponytail: variable-time scalar mult, fine for a PoC; swap in a constant-time ladder for production.
static void pt_mul(uint8_t out[32], const uint8_t s[32], const ge_p3 *p) {
    static const uint8_t zero[32] = {0};
    ge_p2 r;
    ge_double_scalarmult_vartime(&r, s, p, zero);
    ge_tobytes(out, &r);
}

static void pt_add(uint8_t out[32], const ge_p3 *a, const ge_p3 *b) {
    ge_cached c;
    ge_p1p1 t;
    ge_p3 r;
    ge_p3_to_cached(&c, b);
    ge_add(&t, a, &c);
    ge_p1p1_to_p3(&r, &t);
    ge_p3_tobytes(out, &r);
}

static void sc_from32(uint8_t out[32], const uint8_t in[32]) {
    uint8_t w[64] = {0};
    memcpy(w, in, 32);
    sc_reduce(w);
    memcpy(out, w, 32);
}

static const void *memchr0(const uint8_t *p, size_t n) { return memchr(p, 0, n); }

bool license_derive(const uint8_t *l, size_t len, uint8_t out[32]) {
    // ponytail: no not-before/not-after check; the device clock is often unset at boot.
    if (len < 1 || l[0] > 1) return false;
    uint8_t cur[32];
    memcpy(cur, ROOT_KEY, 32);
    size_t off = 1;
    int blocks = 0;
    while (off < len) {
        if (++blocks > 8) return false;
        const uint8_t *b = l + off;
        size_t rem = len - off;
        if (rem < 42 || b[0] != 0) return false;
        const uint8_t *body = b + 42;
        size_t brem = rem - 42, extra;
        const void *z;
        switch (b[33]) {
            case 0:  // intermediate: 4 unknown bytes + issuer\0
                if (brem < 5 || !(z = memchr0(body + 4, brem - 4))) return false;
                extra = 5 + ((const uint8_t *)z - (body + 4));
                break;
            case 2:  // TS3 server: type, max clients, issuer\0
                if (brem < 6 || !(z = memchr0(body + 5, brem - 5))) return false;
                extra = 6 + ((const uint8_t *)z - (body + 5));
                break;
            case 8: {  // TS5 server: type, count, [len, bytes]...
                if (brem < 2) return false;
                size_t pos = 2;
                for (int i = 0; i < body[1]; i++) {
                    if (pos >= brem) return false;
                    size_t pl = body[pos++];
                    if (pos + pl >= brem) return false;
                    pos += pl;
                }
                extra = pos;
                break;
            }
            case 32: extra = 0; break;  // ephemeral
            default: return false;
        }
        size_t blen = 42 + extra;
        uint8_t h[64], hs[32], m[32];
        sha512(b + 1, blen - 1, h);
        h[0] &= 248;
        h[31] &= 63;
        h[31] |= 64;
        sc_from32(hs, h);
        ge_p3 key, prod, parent;
        if (!pt_load(&key, b + 1) || !pt_load(&parent, cur)) return false;
        pt_mul(m, hs, &key);
        if (!pt_load(&prod, m)) return false;
        pt_add(cur, &prod, &parent);
        off += blen;
    }
    memcpy(out, cur, 32);
    return true;
}

void ek_create(uint8_t priv[32], uint8_t pub[32]) {
    uint8_t w[64];
    port_random(w, 64);
    sc_reduce(w);
    memcpy(priv, w, 32);
    ge_p3 a;
    ge_scalarmult_base(&a, priv);
    ge_p3_tobytes(pub, &a);
}

bool ek_shared(const uint8_t priv[32], const uint8_t server_ek[32], const uint8_t alpha[10],
               const uint8_t beta[54], uint8_t iv[64], uint8_t mac[8]) {
    ge_p3 p;
    if (!pt_load(&p, server_ek)) return false;
    uint8_t s[32], shared[32], h[20];
    sc_from32(s, priv);
    pt_mul(shared, s, &p);
    sha512(shared, 32, iv);
    for (int i = 0; i < 10; i++) iv[i] ^= alpha[i];
    for (int i = 0; i < 54; i++) iv[10 + i] ^= beta[i];
    sha1(iv, 64, h);
    memcpy(mac, h, 8);
    return true;
}

// ---- P-256 identity ----

static size_t der_int(uint8_t *o, const uint8_t *v, size_t n) {
    while (n > 1 && v[0] == 0) v++, n--;
    size_t i = 0;
    o[i++] = 0x02;
    o[i++] = (uint8_t)(n + (v[0] & 0x80 ? 1 : 0));
    if (v[0] & 0x80) o[i++] = 0;
    memcpy(o + i, v, n);
    return i + n;
}

static bool load_key(const Identity &id, mbedtls_ecp_keypair &kp) {
    return mbedtls_ecp_read_key(MBEDTLS_ECP_DP_SECP256R1, &kp, id.d, 32) == 0 &&
           mbedtls_ecp_keypair_calc_public(&kp, rng, nullptr) == 0;
}

bool id_create(Identity &id) {
    mbedtls_ecp_keypair kp;
    mbedtls_ecp_keypair_init(&kp);
    size_t n = 0;
    bool ok = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, &kp, rng, nullptr) == 0 &&
              mbedtls_ecp_write_key_ext(&kp, &n, id.d, 32) == 0 && n == 32;
    mbedtls_ecp_keypair_free(&kp);
    id.offset = 0;
    return ok;
}

bool id_omega(const Identity &id, uint8_t *der, size_t *derlen) {
    mbedtls_ecp_keypair kp;
    mbedtls_ecp_keypair_init(&kp);
    uint8_t q[65];
    size_t qn = 0;
    bool ok = load_key(id, kp) &&
              mbedtls_ecp_write_public_key(&kp, MBEDTLS_ECP_PF_UNCOMPRESSED, &qn, q, sizeof q) == 0 &&
              qn == 65;
    mbedtls_ecp_keypair_free(&kp);
    if (!ok) return false;
    // SEQUENCE { BIT STRING (1 bit, 0 = public only), INTEGER 32, INTEGER x, INTEGER y }
    uint8_t body[96];
    size_t i = 0;
    const uint8_t head[] = {0x03, 0x02, 0x07, 0x00, 0x02, 0x01, 0x20};
    memcpy(body, head, sizeof head);
    i += sizeof head;
    i += der_int(body + i, q + 1, 32);
    i += der_int(body + i, q + 33, 32);
    der[0] = 0x30;
    der[1] = (uint8_t)i;
    memcpy(der + 2, body, i);
    *derlen = i + 2;
    return true;
}

bool id_sign(const Identity &id, const uint8_t *data, size_t n, uint8_t *sig, size_t *siglen) {
    uint8_t h[32];
    sha256(data, n, h);
    mbedtls_ecp_keypair kp;
    mbedtls_ecp_keypair_init(&kp);
    bool ok = load_key(id, kp) &&
              mbedtls_ecdsa_write_signature(&kp, MBEDTLS_MD_SHA256, h, 32, sig, MBEDTLS_ECDSA_MAX_LEN,
                                            siglen, rng, nullptr) == 0;
    mbedtls_ecp_keypair_free(&kp);
    return ok;
}

// newlib-nano printf has no %llu, so format 64-bit numbers by hand.
char *u64_dec(char out[21], uint64_t v) {
    char *p = out + 20;
    *p = 0;
    do *--p = (char)('0' + v % 10); while (v /= 10);
    return p;
}

int hashcash_level(const char *omega_b64, uint64_t offset) {
    char s[256], d[21];
    int n = snprintf(s, sizeof s, "%s%s", omega_b64, u64_dec(d, offset));
    if (n < 0 || n >= (int)sizeof s) return 0;
    uint8_t h[20];
    sha1(s, n, h);
    int r = 0;
    for (uint8_t b : h) {
        if (b == 0) { r += 8; continue; }
        while (!(b & 1)) r++, b >>= 1;
        break;
    }
    return r;
}

bool id_improve(Identity &id, int level) {
    uint8_t der[128];
    size_t dn;
    char b64[200];
    if (!id_omega(id, der, &dn) || b64_encode(der, dn, b64, sizeof b64) < 0) return false;
    while (hashcash_level(b64, id.offset) < level) id.offset++;
    return true;
}

// Minimal DER walker for the tomcrypt pubkey layout.
static bool der_next(const uint8_t *&p, const uint8_t *end, uint8_t &tag, const uint8_t *&v, size_t &n) {
    if (end - p < 2) return false;
    tag = *p++;
    n = *p++;
    if (n & 0x80) {
        int k = n & 0x7f;
        if (k > 2 || end - p < k) return false;
        n = 0;
        while (k--) n = (n << 8) | *p++;
    }
    if ((size_t)(end - p) < n) return false;
    v = p;
    p += n;
    return true;
}

static bool coord(const uint8_t *v, size_t n, uint8_t out[32]) {
    while (n > 32 && v[0] == 0) v++, n--;
    if (n > 32) return false;
    memset(out, 0, 32);
    memcpy(out + 32 - n, v, n);
    return true;
}

bool p256_verify(const uint8_t *omega_der, size_t omlen, const uint8_t *data, size_t n,
                 const uint8_t *sig, size_t siglen) {
    const uint8_t *p = omega_der, *end = omega_der + omlen, *v;
    uint8_t tag;
    size_t vn;
    if (!der_next(p, end, tag, v, vn) || tag != 0x30) return false;
    p = v;
    end = v + vn;
    uint8_t q[65];
    q[0] = 0x04;
    for (int i = 0; i < 4; i++) {
        if (!der_next(p, end, tag, v, vn)) return false;
        if (i == 2 && !coord(v, vn, q + 1)) return false;
        if (i == 3 && !coord(v, vn, q + 33)) return false;
    }
    uint8_t h[32];
    sha256(data, n, h);
    mbedtls_ecp_keypair kp;
    mbedtls_ecp_keypair_init(&kp);
    mbedtls_ecp_group grp;
    mbedtls_ecp_point pt;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&pt);
    bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_ecp_point_read_binary(&grp, &pt, q, 65) == 0 &&
              mbedtls_ecp_set_public_key(MBEDTLS_ECP_DP_SECP256R1, &kp, &pt) == 0 &&
              mbedtls_ecdsa_read_signature(&kp, h, 32, sig, siglen) == 0;
    mbedtls_ecp_point_free(&pt);
    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_keypair_free(&kp);
    return ok;
}

bool rsa_puzzle(const uint8_t x[64], const uint8_t n[64], uint32_t level, uint8_t y[64]) {
    if (level > 10000000) return false;
    mbedtls_mpi X, N, E, Y;
    mbedtls_mpi_init(&X), mbedtls_mpi_init(&N), mbedtls_mpi_init(&E), mbedtls_mpi_init(&Y);
    bool ok = mbedtls_mpi_read_binary(&Y, x, 64) == 0 && mbedtls_mpi_read_binary(&N, n, 64) == 0;
    // mbedTLS caps exponents at MBEDTLS_MPI_MAX_BITS, so square in chunks: x^(2^(a+b)) = (x^(2^a))^(2^b).
    for (uint32_t left = level; ok && left;) {
        uint32_t k = left > 4096 ? 4096 : left;
        ok = mbedtls_mpi_lset(&E, 0) == 0 && mbedtls_mpi_set_bit(&E, k, 1) == 0 &&
             mbedtls_mpi_copy(&X, &Y) == 0 && mbedtls_mpi_exp_mod(&Y, &X, &E, &N, nullptr) == 0;
        left -= k;
    }
    ok = ok && mbedtls_mpi_write_binary(&Y, y, 64) == 0;
    mbedtls_mpi_free(&X), mbedtls_mpi_free(&N), mbedtls_mpi_free(&E), mbedtls_mpi_free(&Y);
    return ok;
}

int b64_encode(const uint8_t *in, size_t n, char *out, size_t cap) {
    size_t o = 0;
    if (mbedtls_base64_encode((uint8_t *)out, cap, &o, in, n) != 0) return -1;
    return (int)o;
}

int b64_decode(const char *in, size_t n, uint8_t *out, size_t cap) {
    size_t o = 0;
    if (mbedtls_base64_decode(out, cap, &o, (const uint8_t *)in, n) != 0) return -1;
    return (int)o;
}

// ---- QuickLZ decompression (port of ReSpeak/quicklz, MIT/Apache-2.0) ----

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd24(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16; }

int qlz_decompress(const uint8_t *in, size_t inlen, uint8_t *out, size_t cap) {
    size_t ip = 0;
    auto need = [&](size_t k) { return ip + k <= inlen; };
    if (!need(1)) return -1;
    uint8_t flags = in[ip++];
    int level = (flags >> 2) & 3;
    if (level != 1 && level != 3) return -1;
    size_t hl = (flags & 2) ? 9 : 3;
    uint32_t comp, dec;
    if (hl == 3) {
        if (!need(2)) return -1;
        comp = in[ip], dec = in[ip + 1];
        ip += 2;
    } else {
        if (!need(8)) return -1;
        comp = le32(in + ip), dec = le32(in + ip + 4);
        ip += 8;
    }
    if (dec > cap || comp < hl) return -1;
    if (!(flags & 1)) {
        if (comp - hl != dec || !need(dec)) return -1;
        memcpy(out, in + ip, dec);
        return (int)dec;
    }
    // ponytail: 16 KB static table; not reentrant, one decompress at a time.
    static uint32_t tab[4096];
    memset(tab, 0, sizeof tab);
    size_t o = 0, hashed = 0;
    auto upd = [&](size_t a, size_t b) {
        for (size_t i = a; i < b; i++) tab[((rd24(out + i) >> 12) ^ rd24(out + i)) & 0xfff] = (uint32_t)i;
    };
    uint32_t control = 1;
    for (;;) {
        if (control == 1) {
            if (!need(4)) return -1;
            control = le32(in + ip);
            ip += 4;
        }
        if (control & 1) {
            control >>= 1;
            if (!need(1)) return -1;
            uint32_t next = in[ip++], len, src;
            if (level == 1) {
                if (!need(1)) return -1;
                uint32_t hash = (next >> 4) | (uint32_t)in[ip++] << 4;
                len = next & 0xf;
                if (len) len += 2;
                else {
                    if (!need(1)) return -1;
                    len = in[ip++];
                }
                if (len < 3) return -1;
                src = tab[hash];
            } else {
                uint32_t off;
                if ((next & 3) == 0) len = 3, off = next >> 2;
                else if ((next & 3) == 1) {
                    if (!need(1)) return -1;
                    len = 3, off = (next >> 2) | (uint32_t)in[ip++] << 6;
                } else if ((next & 3) == 2) {
                    if (!need(1)) return -1;
                    len = 3 + ((next >> 2) & 0xf), off = (next >> 6) | (uint32_t)in[ip++] << 2;
                } else if ((next & 0x7f) == 3) {
                    if (!need(3)) return -1;
                    uint32_t n2 = in[ip], n3 = in[ip + 1], n4 = in[ip + 2];
                    ip += 3;
                    len = 3 + ((next >> 7) | (n2 & 0x7f) << 1), off = (n2 >> 7) | n3 << 1 | n4 << 9;
                } else {
                    if (!need(2)) return -1;
                    len = 2 + ((next >> 2) & 0x1f), off = (next >> 7) | (uint32_t)in[ip] << 1 | (uint32_t)in[ip + 1] << 9;
                    ip += 2;
                }
                if (off > o) return -1;
                src = (uint32_t)(o - off);
            }
            if (src >= o || o + len > dec) return -1;
            for (uint32_t i = 0; i < len; i++) out[o + i] = out[src + i];  // may overlap forward
            o += len;
            if (level == 1) {
                upd(hashed, o + 1 - len);
                hashed = o;
            }
        } else if (o >= (dec > 10 ? dec : 10) - 10) {
            while (o < dec) {
                if (control == 1) {
                    if (!need(4)) return -1;
                    ip += 4;
                }
                control >>= 1;
                if (!need(1)) return -1;
                out[o++] = in[ip++];
            }
            break;
        } else {
            if (o + 1 > dec || !need(1)) return -1;
            out[o++] = in[ip++];
            control >>= 1;
            if (level == 1) {
                size_t e = o >= 2 ? o - 2 : 0;
                upd(hashed, e);
                if (e > hashed) hashed = e;
            }
        }
    }
    return (int)o;
}

}  // namespace ts3c
