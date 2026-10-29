// TS3 crypto primitives on top of mbedTLS 3.6 + orlp/ed25519 group math.
#pragma once
#include <cstddef>
#include <cstdint>

namespace ts3c {

void sha1(const void *d, size_t n, uint8_t out[20]);
void sha256(const void *d, size_t n, uint8_t out[32]);
void sha512(const void *d, size_t n, uint8_t out[64]);

// AES-128-EAX with an 8-byte tag, in place.
void eax_encrypt(const uint8_t key[16], const uint8_t nonce[16], const uint8_t *ad, size_t adlen,
                 uint8_t *data, size_t len, uint8_t tag[8]);
bool eax_decrypt(const uint8_t key[16], const uint8_t nonce[16], const uint8_t *ad, size_t adlen,
                 uint8_t *data, size_t len, const uint8_t tag[8]);

// Walk the license chain in initivexpand2 `l` from the TS root key.
// Out: the server's ephemeral key (compressed Edwards point).
bool license_derive(const uint8_t *l, size_t len, uint8_t out[32]);

// Ephemeral key for clientek, and shared IV/MAC (tsclientlib compute_iv_mac).
void ek_create(uint8_t priv[32], uint8_t pub[32]);
bool ek_shared(const uint8_t priv[32], const uint8_t server_ek[32], const uint8_t alpha[10],
               const uint8_t beta[54], uint8_t iv[64], uint8_t mac[8]);

// P-256 identity: private scalar + hashcash offset. Store both in flash.
struct Identity {
    uint8_t d[32];
    uint64_t offset;
};
bool id_create(Identity &id);
bool id_omega(const Identity &id, uint8_t *der, size_t *derlen);  // libtomcrypt-style DER pubkey
bool id_sign(const Identity &id, const uint8_t *data, size_t n, uint8_t *sig, size_t *siglen);
int hashcash_level(const char *omega_b64, uint64_t offset);
char *u64_dec(char out[21], uint64_t v);  // returns pointer into out
// Raises id.offset until the level is reached. Do this on a PC for high levels.
bool id_improve(Identity &id, int level);

bool p256_verify(const uint8_t *omega_der, size_t omlen, const uint8_t *data, size_t n,
                 const uint8_t *sig, size_t siglen);

// y = x^(2^level) mod n, all 64-byte big endian.
bool rsa_puzzle(const uint8_t x[64], const uint8_t n[64], uint32_t level, uint8_t y[64]);

// Returns output length, or -1.
int b64_encode(const uint8_t *in, size_t n, char *out, size_t cap);
int b64_decode(const char *in, size_t n, uint8_t *out, size_t cap);

// QuickLZ level 1/3 decompression. Returns size, or -1 on corrupt input / too small `cap`.
int qlz_decompress(const uint8_t *in, size_t inlen, uint8_t *out, size_t cap);

}  // namespace ts3c
