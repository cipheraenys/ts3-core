// PC test client. Same core the device runs; audio here is a test tone in, WAV file out.
//   ts3cli <host> [-p port] [-n nick] [-c channel] [-i identity.txt] [-t seconds]
//          [--tone seconds] [--rx out.wav] [-L hashcash_level] [--say "channel chat text"]
#include <opus/opus.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "port.h"
#include "ts3.h"

static bool load_identity(const char *path, ts3c::Identity &id) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char hex[65];
    unsigned long long off;
    bool ok = fscanf(f, "%64s %llu", hex, &off) == 2 && strlen(hex) == 64;
    fclose(f);
    for (int i = 0; ok && i < 32; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        id.d[i] = (uint8_t)v;
    }
    id.offset = off;
    return ok;
}

static void save_identity(const char *path, const ts3c::Identity &id) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (uint8_t b : id.d) fprintf(f, "%02x", b);
    fprintf(f, " %llu\n", (unsigned long long)id.offset);
    fclose(f);
}

struct Rx {
    FILE *wav = nullptr;
    uint32_t samples = 0, frames = 0;
    std::map<uint16_t, OpusDecoder *> dec;
};

static void wav_header(FILE *f, uint32_t samples) {
    uint32_t data = samples * 2, riff = 36 + data, rate = 48000, byte_rate = 96000, fmt_len = 16;
    uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f), fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f), fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
}

static void on_voice(void *u, uint16_t clid, uint8_t codec, const uint8_t *opus, size_t len) {
    Rx &rx = *(Rx *)u;
    if (len == 0) {
        port_log("rx: clid %u released PTT (%u frames so far)", clid, rx.frames);
        return;
    }
    if (codec != 4 && codec != 5) return;
    OpusDecoder *&d = rx.dec[clid];
    int err;
    if (!d) d = opus_decoder_create(48000, 1, &err);
    static int16_t pcm[5760];
    int n = opus_decode(d, opus, (int)len, pcm, 5760, 0);
    if (n <= 0) return;
    if (rx.frames++ == 0) port_log("rx: voice from clid %u", clid);
    if (rx.wav) fwrite(pcm, 2, n, rx.wav), rx.samples += n;
}

static void on_command(void *, const char *cmd) {
    port_log("cmd: %.140s%s", cmd, strlen(cmd) > 140 ? "..." : "");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <host> [-p port] [-n nick] [-c channel] [-i identity.txt] [-t secs]"
                        " [--tone secs] [--rx out.wav] [-L level]\n", argv[0]);
        return 2;
    }
    Ts3Config cfg;
    cfg.host = argv[1];
    const char *idpath = "identity.txt", *rxpath = nullptr, *say = nullptr;
    int run_s = 15, level = 8;
    double tone_s = 0;
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string a = argv[i];
        const char *v = argv[i + 1];
        if (a == "-p") cfg.port = (uint16_t)atoi(v);
        else if (a == "-n") cfg.nickname = v;
        else if (a == "-c") cfg.channel = v;
        else if (a == "-i") idpath = v;
        else if (a == "-t") run_s = atoi(v);
        else if (a == "--tone") tone_s = atof(v);
        else if (a == "--rx") rxpath = v;
        else if (a == "-L") level = atoi(v);
        else if (a == "--say") say = v;
    }

    if (!load_identity(idpath, cfg.identity)) {
        port_log("creating identity %s (hashcash level %d)...", idpath, level);
        if (!ts3c::id_create(cfg.identity) || !ts3c::id_improve(cfg.identity, level)) return 1;
        save_identity(idpath, cfg.identity);
    }

    Rx rx;
    if (rxpath && (rx.wav = fopen(rxpath, "wb"))) wav_header(rx.wav, 0);
    Ts3Client c;
    c.user = &rx;
    c.on_voice = on_voice;
    c.on_command = on_command;
    if (!c.connect(cfg)) return 1;

    int err;
    OpusEncoder *enc = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &err);
    uint32_t t0 = port_millis(), tx_start = 0, tx_next = 0, sent = 0;
    double phase = 0;
    bool tx_done = tone_s <= 0;
    while (c.state() != Ts3Client::Closed && port_millis() - t0 < (uint32_t)run_s * 1000) {
        c.poll(5);
        uint32_t now = port_millis();
        if (say && c.state() == Ts3Client::Connected) {  // channel chat message
            char esc[256], cmd[320];
            ts3_escape(say, esc, sizeof esc);
            snprintf(cmd, sizeof cmd, "sendtextmessage targetmode=2 target=1 msg=%s", esc);
            c.send_command(cmd);
            say = nullptr;
        }
        if (c.state() != Ts3Client::Connected || tx_done) continue;
        if (!tx_start) tx_start = tx_next = now + 1000;  // talk 1 s after joining
        if (now < tx_next) continue;
        if (now - tx_start >= tone_s * 1000) {
            c.send_voice(nullptr, 0);  // end of transmission
            port_log("tx: sent %u frames, released PTT", sent);
            tx_done = true;
            continue;
        }
        int16_t pcm[960];  // 20 ms @ 48 kHz
        for (int16_t &s : pcm) s = (int16_t)(8000 * sin(phase)), phase += 2 * 3.14159265358979 * 440 / 48000;
        uint8_t out[400];
        int n = opus_encode(enc, pcm, 960, out, sizeof out);
        if (n > 0 && c.send_voice(out, n)) sent++;
        tx_next += 20;
    }
    c.disconnect("bye");
    if (rx.wav) {
        wav_header(rx.wav, rx.samples);
        fclose(rx.wav);
        port_log("rx: wrote %u samples (%.1f s) to %s", rx.samples, rx.samples / 48000.0, rxpath);
    }
    opus_encoder_destroy(enc);
    return 0;
}
