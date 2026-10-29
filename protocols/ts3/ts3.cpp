#include "ts3.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "port.h"

using namespace ts3c;

enum : uint8_t { VOICE, WHISPER, CMD, CMDLOW, PING, PONG, ACK, ACKLOW, INIT };
enum : uint8_t { UE = 0x80, CP = 0x40, NP = 0x20, FR = 0x10 };
enum { PLAIN, FAKE, REAL };

static const uint8_t FAKE_KEY[] = "c:\\windows\\syste";
static const uint8_t FAKE_NONCE[] = "m\\firewall32.cpl";
static const size_t C2S_HDR = 13, S2C_HDR = 11, MAX_PKT = 500;

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8, p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = (uint8_t)v; }

// ---------------- escaping ----------------

int ts3_escape(const char *in, char *out, size_t cap) {
    static const char from[] = "\\/ |\a\b\f\n\r\t\v", to[] = "\\/sp" "abfnrtv";
    size_t o = 0;
    for (; *in; in++) {
        const char *f = *in ? strchr(from, *in) : nullptr;
        if (o + 2 >= cap) return -1;
        if (f) out[o++] = '\\', out[o++] = to[f - from];
        else out[o++] = *in;
    }
    out[o] = 0;
    return (int)o;
}

bool ts3_arg(const char *cmd, const char *key, char *out, size_t cap) {
    static const char from[] = "\\/sp" "abfnrtv", to[] = "\\/ |\a\b\f\n\r\t\v";
    size_t kl = strlen(key);
    const char *p = strchr(cmd, ' ');  // skip command name
    while (p && *p == ' ') {
        p++;
        if (!strncmp(p, key, kl) && (p[kl] == '=' || p[kl] == ' ' || p[kl] == '|' || !p[kl])) {
            p += kl + (p[kl] == '=');
            size_t o = 0;
            for (; *p && *p != ' ' && *p != '|' && o + 1 < cap; p++) {
                const char *f;
                if (*p == '\\' && p[1] && (f = strchr(from, p[1]))) out[o++] = to[f - from], p++;
                else out[o++] = *p;
            }
            out[o] = 0;
            return true;
        }
        while (*p && *p != ' ' && *p != '|') p++;
        if (*p == '|') break;
    }
    return false;
}

// ---------------- lifecycle ----------------

bool Ts3Client::connect(const Ts3Config &cfg) {
    cfg_ = cfg;
    sock_ = port_udp_open(cfg.host, cfg.port);
    if (sock_ < 0) {
        port_log("ts3: cannot open UDP to %s:%u", cfg.host, cfg.port);
        st_ = Closed;
        return false;
    }
    memset(out_pid_, 0, sizeof out_pid_);
    memset(out_gen_, 0, sizeof out_gen_);
    memset(in_next_, 0, sizeof in_next_);
    memset(in_gen_, 0, sizeof in_gen_);
    memset(pend_, 0, sizeof pend_);
    memset(held_, 0, sizeof held_);
    memset(frag_, 0, sizeof frag_);
    out_pid_[CMD] = 1;  // command 0 is the clientinitiv inside Init4
    have_params_ = false;
    clid_ = 0;
    st_ = Init;
    last_rx_ = port_millis();

    uint32_t now = port_unix_time();
    version_ = now - 1356998400u;
    port_random(random0_, 4);
    uint8_t b[21] = {0};
    put32(b, version_);
    b[4] = 0;
    put32(b + 5, now);
    memcpy(b + 9, random0_, 4);
    init_step_ = 1;
    init_first_ = port_millis();
    send_init(b, sizeof b);
    return true;
}

void Ts3Client::fail(const char *why) {
    port_log("ts3: closed: %s", why);
    st_ = Closed;
    if (sock_ >= 0) port_udp_close(sock_);
    sock_ = -1;
}

void Ts3Client::disconnect(const char *reason) {
    if (st_ == Connected) {
        char esc[128], cmd[200];
        ts3_escape(reason, esc, sizeof esc);
        snprintf(cmd, sizeof cmd, "clientdisconnect reasonid=8 reasonmsg=%s", esc);
        send_command(cmd);
        uint32_t t0 = port_millis();
        auto pending = [&] { for (auto &p : pend_) if (p.used) return true; return false; };
        while (sock_ >= 0 && pending() && port_millis() - t0 < 1000) poll(50);
    }
    if (sock_ >= 0) fail(reason);
}

void Ts3Client::poll(uint32_t timeout_ms) {
    if (sock_ < 0) return;
    static uint8_t buf[MAX_PKT + 16];
    for (int r = port_udp_recv(sock_, buf, sizeof buf, timeout_ms); r > 0 && sock_ >= 0;
         r = port_udp_recv(sock_, buf, sizeof buf, 0))
        handle(buf, (size_t)r);
    if (sock_ >= 0) tick();
}

void Ts3Client::tick() {
    uint32_t now = port_millis();
    if (init_len_ && now >= init_next_) {
        if (now - init_first_ > 30000) return fail("init handshake timeout");
        port_udp_send(sock_, init_buf_, init_len_);
        init_next_ = now + 1000;
    }
    for (auto &p : pend_) {
        if (!p.used || now < p.next) continue;
        if (now - p.first > 30000) return fail("packet never acknowledged");
        port_udp_send(sock_, p.buf, p.len);
        p.rto = p.rto * 2 > 4000 ? 4000 : p.rto * 2;
        p.next = now + p.rto;
    }
    if (st_ == Connected && now - last_ping_ >= 1000) {
        last_ping_ = now;
        send_packet(PING, 0, nullptr, 0, PLAIN, false);
    }
    if (now - last_rx_ > 30000) fail("server timeout");
}

// ---------------- sending ----------------

void Ts3Client::key_nonce(bool c2s, uint8_t type, uint16_t pid, uint32_t gen, uint8_t key[16],
                          uint8_t nonce[16]) {
    uint8_t t[70], h[32];
    t[0] = c2s ? 0x31 : 0x30;
    t[1] = type;
    put32(t + 2, gen);
    memcpy(t + 6, iv_, 64);
    sha256(t, 70, h);
    memcpy(key, h, 16);
    memcpy(nonce, h + 16, 16);
    key[0] ^= pid >> 8;
    key[1] ^= pid & 0xff;
}

bool Ts3Client::send_packet(uint8_t type, uint8_t flags, const uint8_t *d, size_t n, int crypt,
                            bool reliable) {
    if (sock_ < 0 || n > MAX_PKT - C2S_HDR) return false;
    uint8_t b[MAX_PKT];
    uint16_t pid = out_pid_[type];
    uint32_t gen = out_gen_[type];
    put16(b + 8, pid);
    put16(b + 10, clid_);
    b[12] = type | flags | (crypt == PLAIN ? UE : 0);
    if (n) memcpy(b + C2S_HDR, d, n);
    if (crypt == PLAIN) {
        if (have_params_) memcpy(b, mac_, 8);
        else memset(b, 0, 8);
    } else if (crypt == FAKE) {
        eax_encrypt(FAKE_KEY, FAKE_NONCE, b + 8, 5, b + C2S_HDR, n, b);
    } else {
        uint8_t k[16], nc[16];
        key_nonce(true, type, pid, gen, k, nc);
        eax_encrypt(k, nc, b + 8, 5, b + C2S_HDR, n, b);
    }
    if (++out_pid_[type] == 0) out_gen_[type]++;
    size_t len = C2S_HDR + n;
    if (reliable) {
        Pending *slot = nullptr;
        for (auto &p : pend_) if (!p.used) { slot = &p; break; }
        if (!slot) return fail("send queue full"), false;
        uint32_t now = port_millis();
        *slot = Pending{true, type, pid, (uint16_t)len, now, now + 1000, 1000, {}};
        memcpy(slot->buf, b, len);
    }
    port_udp_send(sock_, b, len);
    return true;
}

bool Ts3Client::send_command(const char *cmd) {
    if (st_ != Connected) return false;
    return send_cmd(cmd, REAL);
}

bool Ts3Client::send_cmd(const char *cmd, int crypt) {
    // ponytail: splits without QuickLZ compression (valid, just more packets).
    size_t n = strlen(cmd), max = MAX_PKT - C2S_HDR;
    size_t count = (n + max - 1) / max;
    if (count == 0) count = 1;
    for (size_t i = 0; i < count; i++) {
        size_t off = i * max, len = n - off < max ? n - off : max;
        uint8_t fl = NP | (count > 1 && (i == 0 || i == count - 1) ? FR : 0);
        if (!send_packet(CMD, fl, (const uint8_t *)cmd + off, len, crypt, true)) return false;
    }
    return true;
}

bool Ts3Client::send_voice(const uint8_t *opus, size_t len, uint8_t codec) {
    if (st_ != Connected || len > MAX_PKT - C2S_HDR - 3) return false;
    uint8_t b[MAX_PKT];
    put16(b, out_pid_[VOICE]);
    b[2] = codec;
    if (len) memcpy(b + 3, opus, len);
    // ponytail: voice is sent unencrypted (server default "per channel", unencrypted).
    return send_packet(VOICE, 0, b, 3 + len, PLAIN, false);
}

void Ts3Client::send_ack(uint8_t type, uint16_t pid) {
    uint8_t b[2];
    put16(b, pid);
    uint8_t at = type == CMD ? ACK : ACKLOW;
    bool first = at == ACK && out_pid_[ACK] == 0 && out_gen_[ACK] == 0;
    send_packet(at, 0, b, 2, (!have_params_ || first) ? FAKE : REAL, false);
}

void Ts3Client::send_init(const uint8_t *body, size_t len) {
    memcpy(init_buf_, "TS3INIT1", 8);
    put16(init_buf_ + 8, 101);
    put16(init_buf_ + 10, 0);
    init_buf_[12] = INIT | UE;
    memcpy(init_buf_ + C2S_HDR, body, len);
    init_len_ = C2S_HDR + len;
    init_next_ = port_millis() + 1000;
    port_udp_send(sock_, init_buf_, init_len_);
}

// ---------------- receiving ----------------

uint32_t Ts3Client::in_gen(uint8_t type, uint16_t pid) {
    uint16_t next = in_next_[type];
    uint32_t g = in_gen_[type];
    if (pid < next && next - pid > 0x8000) return g + 1;  // pid already wrapped
    if (pid > next && pid - next > 0x8000 && g) return g - 1;  // late packet from before wrap
    return g;
}

void Ts3Client::handle(uint8_t *buf, size_t n) {
    if (n < S2C_HDR) return;
    uint16_t pid = be16(buf + 8);
    uint8_t type = buf[10] & 0x0f, flags = buf[10] & 0xf0;
    uint8_t *d = buf + S2C_HDR;
    size_t dn = n - S2C_HDR;
    if (type > INIT) return;
    last_rx_ = port_millis();
    if (type == INIT) return handle_init(d, dn);

    uint32_t gen = in_gen(type, pid);
    if (!(flags & UE)) {
        bool try_fake = !have_params_ || (type == ACK && pid <= 1) || (type == CMD && pid == 0 && gen == 0);
        bool ok = try_fake && eax_decrypt(FAKE_KEY, FAKE_NONCE, buf + 8, 3, d, dn, buf);
        if (!ok && have_params_) {
            uint8_t k[16], nc[16];
            key_nonce(false, type, pid, gen, k, nc);
            ok = eax_decrypt(k, nc, buf + 8, 3, d, dn, buf);
        }
        if (!ok) return;  // bad MAC: drop
    } else if (type == CMD || type == CMDLOW) {
        return;  // commands must be encrypted
    }

    if (type == CMD || type == CMDLOW) return handle_command_packet(type, pid, flags, d, dn);

    if ((uint16_t)(pid - in_next_[type]) < 0x8000) {
        in_next_[type] = pid + 1;
        in_gen_[type] = gen + (in_next_[type] == 0);
    }
    switch (type) {
        case PING: {
            uint8_t b[2];
            put16(b, pid);
            send_packet(PONG, 0, b, 2, PLAIN, false);
            break;
        }
        case ACK:
        case ACKLOW: {
            if (dn < 2) break;
            uint16_t id = be16(d);
            uint8_t ct = type == ACK ? CMD : CMDLOW;
            for (auto &p : pend_)
                if (p.used && p.type == ct && p.pid == id) p.used = false;
            if (st_ == Handshake && ct == CMD && id == 1) {  // clientek acknowledged
                st_ = Connecting;
                send_clientinit();
            }
            break;
        }
        case VOICE:
        case WHISPER:
            if (dn >= 5 && on_voice) on_voice(user, be16(d + 2), d[4], d + 5, dn - 5);
            break;
    }
}

void Ts3Client::handle_init(const uint8_t *d, size_t n) {
    if (st_ != Init || n < 1) return;
    if (d[0] == 127) {  // server wants a restart
        port_log("ts3: init restart requested");
        st_ = Closed;
        port_udp_close(sock_);
        connect(cfg_);
        return;
    }
    if (d[0] == 1 && init_step_ == 1 && n >= 21) {
        uint8_t b[25];
        put32(b, version_);
        b[4] = 2;
        memcpy(b + 5, d + 1, 20);  // random1 + random0_r
        init_step_ = 3;
        send_init(b, sizeof b);
    } else if (d[0] == 3 && init_step_ == 3 && n >= 233) {
        const uint8_t *x = d + 1, *nn = d + 65, *random2 = d + 133;
        uint32_t level = be32(d + 129);
        uint8_t y[64];
        uint32_t t0 = port_millis();
        if (!rsa_puzzle(x, nn, level, y)) return fail("rsa puzzle failed");
        port_log("ts3: rsa puzzle level %u solved in %u ms", level, port_millis() - t0);

        port_random(alpha_, sizeof alpha_);
        uint8_t der[128];
        size_t dl;
        char a64[32], o64[200];
        if (!id_omega(cfg_.identity, der, &dl)) return fail("bad identity");
        b64_encode(alpha_, sizeof alpha_, a64, sizeof a64);
        b64_encode(der, dl, o64, sizeof o64);

        uint8_t b[MAX_PKT - C2S_HDR];
        size_t i = 0;
        put32(b, version_), i += 4;
        b[i++] = 4;
        memcpy(b + i, x, 64), i += 64;
        memcpy(b + i, nn, 64), i += 64;
        put32(b + i, level), i += 4;
        memcpy(b + i, random2, 100), i += 100;
        memcpy(b + i, y, 64), i += 64;
        // Not escaped on purpose: matches tsclientlib's Init4.
        int w = snprintf((char *)b + i, sizeof b - i, "clientinitiv alpha=%s omega=%s ot=1 ip", a64, o64);
        if (w < 0 || (size_t)w >= sizeof b - i) return fail("init4 too large");
        init_step_ = 0;
        st_ = Handshake;  // Init4 keeps resending until initivexpand2 arrives
        send_init(b, i + w);
    }
}

void Ts3Client::handle_command_packet(uint8_t type, uint16_t pid, uint8_t flags, const uint8_t *d, size_t n) {
    send_ack(type, pid);  // always ack, also duplicates
    int q = type == CMD ? 0 : 1;
    uint16_t diff = (uint16_t)(pid - in_next_[type]);
    if (diff != 0) {
        if (diff >= 0x8000) return;  // duplicate / old
        for (auto &h : held_[q]) if (h.used && h.pid == pid) return;
        for (auto &h : held_[q]) {
            if (h.used) continue;
            h.used = true, h.pid = pid, h.flags = flags, h.len = (uint16_t)n;
            memcpy(h.buf, d, n);
            return;
        }
        return;  // reorder buffer full: the server will resend
    }

    for (;;) {
        if (++in_next_[type] == 0) in_gen_[type]++;
        Frag &f = frag_[q];
        const uint8_t *data = d;
        size_t len = n;
        bool compressed = flags & CP, done = true;
        if (f.active || (flags & FR)) {
            if (!f.active) f.active = true, f.compressed = compressed, f.len = 0;
            if (f.len + n > kFragMax) return fail("fragmented command too large");
            memcpy(frag_buf_[q] + f.len, d, n);
            f.len += n;
            done = (flags & FR) && f.len != n;  // FR on a later packet closes it
            if (done) data = frag_buf_[q], len = f.len, compressed = f.compressed, f.active = false;
        }
        if (done) {
            int out;
            if (compressed) out = qlz_decompress(data, len, (uint8_t *)cmd_buf_, kCmdMax);
            else out = len <= kCmdMax ? (memcpy(cmd_buf_, data, len), (int)len) : -1;
            if (out < 0) return fail("bad command payload");
            cmd_buf_[out] = 0;
            handle_command(cmd_buf_);
            if (sock_ < 0) return;
        }
        Held *next = nullptr;
        for (auto &h : held_[q]) if (h.used && h.pid == in_next_[type]) { next = &h; break; }
        if (!next) return;
        next->used = false;
        static uint8_t tmp[MAX_PKT];
        memcpy(tmp, next->buf, next->len);
        d = tmp, n = next->len, flags = next->flags;
    }
}

void Ts3Client::handle_command(char *cmd) {
    if (!strncmp(cmd, "initivexpand2 ", 14)) return handle_initivexpand2(cmd);
    if (!strncmp(cmd, "initserver ", 11)) {
        char v[16];
        if (ts3_arg(cmd, "aclid", v, sizeof v)) clid_ = (uint16_t)atoi(v);
        for (auto &p : pend_) if (p.used && p.type == CMD && p.pid == 2) p.used = false;  // acks clientinit
        st_ = Connected;
        last_ping_ = port_millis();
        port_log("ts3: connected, clid=%u", clid_);
    } else if (st_ == Connecting && !strncmp(cmd, "error ", 6)) {
        char id[16], msg[128];
        if (ts3_arg(cmd, "id", id, sizeof id) && strcmp(id, "0")) {
            ts3_arg(cmd, "msg", msg, sizeof msg);
            port_log("ts3: server refused clientinit: %s (%s)", msg, id);
        }
    }
    if (on_command) on_command(user, cmd);
}

void Ts3Client::handle_initivexpand2(char *cmd) {
    if (st_ != Handshake) return;
    init_len_ = 0;  // Init4 is acknowledged
    static char s[1024];
    uint8_t lic[600], beta[64], omega[128], proof[128], server_ek[32];
    int ln, bn, on, pn;
    if (!ts3_arg(cmd, "ot", s, sizeof s) || strcmp(s, "1")) return fail("server too old (no ot=1)");
    if (ts3_arg(cmd, "root", s, sizeof s)) port_log("ts3: warning: custom license root ignored");
    if (!ts3_arg(cmd, "l", s, sizeof s) || (ln = b64_decode(s, strlen(s), lic, sizeof lic)) < 1 ||
        !ts3_arg(cmd, "beta", s, sizeof s) || (bn = b64_decode(s, strlen(s), beta, sizeof beta)) != 54 ||
        !ts3_arg(cmd, "omega", s, sizeof s) || (on = b64_decode(s, strlen(s), omega, sizeof omega)) < 1 ||
        !ts3_arg(cmd, "proof", s, sizeof s) || (pn = b64_decode(s, strlen(s), proof, sizeof proof)) < 1)
        return fail("malformed initivexpand2");
    if (!p256_verify(omega, on, lic, ln, proof, pn)) return fail("server license signature invalid");
    if (!license_derive(lic, ln, server_ek)) return fail("cannot parse server license");

    uint8_t priv[32], pub[32];
    ek_create(priv, pub);
    if (!ek_shared(priv, server_ek, alpha_, beta, iv_, mac_)) return fail("bad server key");
    have_params_ = true;

    uint8_t signed_data[32 + 54], sig[80];
    size_t sl;
    memcpy(signed_data, pub, 32);
    memcpy(signed_data + 32, beta, 54);
    if (!id_sign(cfg_.identity, signed_data, sizeof signed_data, sig, &sl)) return fail("sign failed");

    char e64[64], p64[128], ee[96], pe[192], out[400];
    b64_encode(pub, 32, e64, sizeof e64);
    b64_encode(sig, sl, p64, sizeof p64);
    ts3_escape(e64, ee, sizeof ee);
    ts3_escape(p64, pe, sizeof pe);
    snprintf(out, sizeof out, "clientek ek=%s proof=%s", ee, pe);
    send_cmd(out, FAKE);  // command pid 1, fake-encrypted
}

void Ts3Client::send_clientinit() {
    auto esc = [](const char *in, char *out, size_t cap) { ts3_escape(in, out, cap); return out; };
    auto pw = [&](const char *p, char *out, size_t cap) {
        if (!*p) return (char *)(out[0] = 0, out);
        uint8_t h[20];
        char b[40];
        sha1(p, strlen(p), h);
        b64_encode(h, 20, b, sizeof b);
        return esc(b, out, cap);
    };
    char nick[96], chan[160], cpw[64], spw[64], off[21], cmd[1024];
    // Known-good client version + signature (from ts3j / tsdeclarations Versions.csv).
    snprintf(cmd, sizeof cmd,
             "clientinit client_nickname=%s client_version=3.?.?\\s[Build:\\s5680278000] "
             "client_platform=Windows client_input_hardware=1 client_output_hardware=1 "
             "client_default_channel=%s client_default_channel_password=%s client_server_password=%s "
             "client_meta_data client_version_sign=DX5NIYLvfJEUjuIbCidnoeozxIDRRkpq3I9vVMBmE9L2qnekOoBzSenkzsg2lC9CMv8K5hkEzhr2TYUYSwUXCg== "
             "client_key_offset=%s client_nickname_phonetic client_default_token hwid",
             esc(cfg_.nickname, nick, sizeof nick), esc(cfg_.channel, chan, sizeof chan),
             pw(cfg_.channel_password, cpw, sizeof cpw), pw(cfg_.server_password, spw, sizeof spw),
             u64_dec(off, cfg_.identity.offset));
    send_cmd(cmd, REAL);  // command pid 2
}
