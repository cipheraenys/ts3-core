// Crypto self-test against tsclientlib's known-good vectors. Runs on PC and in the QEMU FreeRTOS demo; not yet run on EC600M.
#include <cassert>
#include <cstdio>
#include <cstring>

#include "crypto.h"
#include "port.h"

using namespace ts3c;

static int hex(const char *s, uint8_t *out) {
    int n = 0;
    for (unsigned v; *s;) {
        if (*s == ' ') { s++; continue; }
        sscanf(s, "%2x", &v);
        out[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

static int b64(const char *s, uint8_t *out, size_t cap) { return b64_decode(s, strlen(s), out, cap); }

int main() {
    // 1. Fake-key EAX: client's first ack (algorithms.rs test_fake_encrypt)
    {
        uint8_t pkt[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x06, 0, 0};
        eax_encrypt((const uint8_t *)"c:\\windows\\syste", (const uint8_t *)"m\\firewall32.cpl",
                    pkt + 8, 5, pkt + 13, 2, pkt);
        const uint8_t want[15] = {0xa4, 0x7b, 0x47, 0x94, 0xdb, 0xa9, 0x6a, 0xc5, 0, 0, 0, 0, 0x6, 0xfe, 0x18};
        assert(memcmp(pkt, want, 15) == 0);
        assert(eax_decrypt((const uint8_t *)"c:\\windows\\syste", (const uint8_t *)"m\\firewall32.cpl",
                           pkt + 8, 5, pkt + 13, 2, pkt));
        assert(pkt[13] == 0 && pkt[14] == 0);
    }
    // 2. License chain derivation (license.rs derive_public_key)
    uint8_t lic[256], ek[32];
    int ln = b64("AQA1hUFJiiSs0wFXkYuPUJVcDa6XCrZTcsvkB0Ffzz4CmwIITRXgCqeTYAcAAAAgQW5vbnltb3VzAAC4R+5mos+UQ/"
                 "KCbkpQLMI5WRp4wkQu8e5PZY4zU+/FlyAJwaE8CcJJ/A==", lic, sizeof lic);
    assert(ln > 0 && license_derive(lic, ln, ek));
    uint8_t want_ek[32];
    hex("40e950c461ba183a1eb7cbb19ac3d8d9c4d524db38f72d3d6675772ac59cc5c6", want_ek);
    assert(memcmp(ek, want_ek, 32) == 0);
    // 3. Shared IV from ephemeral key (algorithms.rs shared_iv31)
    {
        uint8_t priv[32], alpha[16], beta[64], iv[64], mac[8], want[64];
        hex("b04ea1d95c7264df0de8b36baa7ca15f7571f51fa054b55127088edd963d6e79", priv);
        assert(b64("Jkxq1wIvvhzaCA==", alpha, sizeof alpha) == 10);
        assert(b64("wU5T/MM6toW6Wge9th7VlTlzVZ9JDWypw2P9migfc25pjGP2Tj7Hm6rJpmKeHRr08Ch7BEAR", beta, sizeof beta) == 54);
        assert(ek_shared(priv, ek, alpha, beta, iv, mac));
        hex("7e34c4df0a5dbbacc92fd1a7d2486c2ea2f417978525 45cfc89219012b2d52842b2bdd98ffc97295 2123f3f66ada55d9"
            "d84a37e33b2d23fe38fd14ae06670916", want);
        assert(memcmp(iv, want, 64) == 0);
    }
    // 4. Real-key decrypt of a captured clientinit (algorithms.rs test_new_decrypt)
    {
        uint8_t seed[32], iv[64], beta[64], temp[70], kn[32];
        hex("C2456FCBFC2208AE442B7DE73A671BDA9309B200F2CD104908CD3AB07BDD58AD", seed);
        sha512(seed, 32, iv);
        assert(b64("I4onb0zMyAD6bd24QANDls40eOES7qmjonBFtt5wRWzAfIIQWTSxjEas6TGTZIJ8QSJNX+Pl", beta, sizeof beta) == 54);
        for (int i = 0; i < 54; i++) iv[10 + i] ^= beta[i];
        memset(temp, 0, sizeof temp);
        temp[0] = 0x31, temp[1] = 2;
        memcpy(temp + 6, iv, 64);
        sha256(temp, 70, kn);
        kn[1] ^= 2;  // packet id 2
        static uint8_t pkt[1024];
        int n = hex("2b982443ab38be6b00020000329abf64d4572e1349897b5e1e96fbc4a763a4c4ce1f64f0c1e3febd0a5f04a82ab1f2bc2344bb374fd16181beb8233b5b06944280470e9b6893290a1da0776ffcd89f3beec2ce23b9694930c09efaaea0d88a6895a08ede4d5cbfea61291fc553ac651f1e2bc1d2bd277a8bd9ab5386415579a9e56fac46d8b6b119f454bebd99179cd317dec60af205341d11f274d02bbacdd7e9773f72a426358ca1d39016dd95bde2409cd81bf99b340887e997ea982370c6790cf4d23150460820224766838ea4ec4d71dd102ede701ea0001f392623aa410dd9ab0e45874da82e29e6e370515ec30a37dd73f5a364c233ff014384beab5f1708c9f48dfba33a520f8fcdcef055789c54693c3fe72c5bfaca7cb4ca1fed77b8624660b8abc882f4b95b1284cb6dc55019c6082dd6dd146fa50383662d7298bef04ababaf1af80e15cd4c1f81326f085788e2918e00324147dce39b23db71326abc3de4b94df10f1531e9cce202bba71fa3ebeefd77b21fa3260a62e92eeee2183421d384a8c48777e2f9efbc58d4f442c5f0529c7c0e27e81b2b6b1b05eb8fa19256886248d553582dfd24c7cfab3c3f7317a5cebc6504b53fa0e86fc8c1100fc1d506fcf96caa76a7c0b6a27e577f2efdecd4070e847a559bf37d75bfdbe9e814c702426ce696d8645bc300b5f28f9e7f1ce", pkt);
        assert(eax_decrypt(kn, kn + 16, pkt + 8, 5, pkt + 13, n - 13, pkt));
        assert(memcmp(pkt + 13, "clientinit ", 11) == 0);
    }
    // 5. Server proof signature (crypto.rs p256_signature)
    {
        uint8_t l[256], key[128], sig[128];
        int a = b64("AQBM0LZCVmZ7CX/miewqdjOyuKa6kI78Fk43LoypifqOkAIOkvUAEn46gAcAAAAgQW5vbnltb3VzAABoruUa34pO9zy1Z5zIOmrkIO06lKg/"
                    "+mBrg6Mw1Rg4OyAPa7A3D2xY9w==", l, sizeof l);
        int b = b64("MEwDAgcAAgEgAiEA96WgYeYU8zoPqXJqicita+rR92FvnTlxYcUUyIDkQ6cCIE/KPo+ms3BEzN/HBR71BJ/Z1Fv8918mdDKLetbOGKWt", key, sizeof key);
        int c = b64("MEUCIQC+ececxC0NCcuCtrXHAO5h7qbh1s/TGP/AaHa6+wV38wIgV9wwSppEdGjwuH3ETAME9tDj3aNkNvL25i0ikF9vs8M=", sig, sizeof sig);
        assert(a > 0 && b > 0 && c > 0);
        assert(p256_verify(key, b, l, a, sig, c));
        l[5] ^= 1;
        assert(!p256_verify(key, b, l, a, sig, c));
    }
    // 6. Own identity: sign + verify roundtrip, hashcash
    {
        Identity id;
        uint8_t der[128], sig[80];
        size_t dn, sn;
        assert(id_create(id) && id_omega(id, der, &dn));
        assert(der[0] == 0x30 && der[2] == 0x03);
        assert(id_sign(id, (const uint8_t *)"hello", 5, sig, &sn));
        assert(p256_verify(der, dn, (const uint8_t *)"hello", 5, sig, sn));
        assert(id_improve(id, 8));
        char b[200];
        b64_encode(der, dn, b, sizeof b);
        assert(hashcash_level(b, id.offset) >= 8);
    }
    // 7. RSA puzzle: 3^(2^4) mod 1000003 = 43046721 mod 1000003
    {
        uint8_t x[64] = {0}, n[64] = {0}, y[64];
        x[63] = 3;
        n[61] = 0x0f, n[62] = 0x42, n[63] = 0x43;  // 1000003
        assert(rsa_puzzle(x, n, 4, y));
        uint32_t v = y[60] << 24 | y[61] << 16 | y[62] << 8 | y[63];
        assert(v == 43046721u % 1000003u);
        // Real servers use level 10000 (> mbedTLS max exponent): checks the chunked path.
        assert(rsa_puzzle(x, n, 10000, y));
        assert((uint32_t)(y[60] << 24 | y[61] << 16 | y[62] << 8 | y[63]) == 514882u);
        assert(rsa_puzzle(x, n, 4096, y));
        assert((uint32_t)(y[60] << 24 | y[61] << 16 | y[62] << 8 | y[63]) == 622377u);
    }
    // 8. u64 formatting (newlib-nano has no %llu)
    {
        char d[21];
        assert(!strcmp(u64_dec(d, 0), "0"));
        assert(!strcmp(u64_dec(d, 18446744073709551615ull), "18446744073709551615"));
    }
    // 9. QuickLZ (quicklz crate doc example)
    {
        const uint8_t c[] = {0x47, 0x17, 0, 0, 0, 0x0a, 0, 0, 0, 0, 0, 0, 0x80, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        uint8_t o[16];
        assert(qlz_decompress(c, sizeof c, o, sizeof o) == 10);
        for (int i = 0; i < 10; i++) assert(o[i] == i);
    }
    puts("selftest: all passed");
    return 0;
}
