// TS3 client app for Quectel EC600M (QuecOpen OCPU, ASR1606 / Cortex-R4).
// Radio behaviour: hold PTT -> mic -> Opus -> TS3 voice; incoming TS3 voice -> Opus -> speaker.
// Half duplex, like the vendor PoC app. Mirrors rtos/freertos_qemu/app.cpp but on real hardware.
//
// Verified against the EC600M QuecOpen C-SDK (LTE01R06..AIRTC):
//   ql_data_call.h  ql_start_data_call / ql_set_auto_connect
//   ql_power.h      ql_get_pwrkey_status / ql_pwrkey_register_irq
//   ql_audio.h      ql_codec_choose / ql_set_audio_path_receiver / ql_set_volume /
//                   ql_audio_record_open/read / ql_audio_track_open/write
//   ql_application.h  application_init(entry, name, stack_kib, prio)
// TODO(deploy): the APN/profile, codec enum, volume and PTT source are site-specific — set them.

#include <cstdio>
#include <cstring>

#include <opus.h>

extern "C" {
#include "ql_type.h"
#include "ql_rtos.h"
#include "ql_application.h"
#include "ql_data_call.h"
#include "ql_power.h"
#include "ql_audio.h"
}

#include "../../src/port.h"
#include "../../src/ts3.h"

#ifndef TS3_HOST
#define TS3_HOST "ts3.example.com"   // TODO(deploy): your TeamSpeak server (IP or host)
#endif
#define PROFILE_IDX 1                 // TODO(deploy): PDP profile; APN below
#define APN_NAME    "internet"        // TODO(deploy): carrier APN (Telkomsel, etc.)

static const int kRate = 16000, kFrame = kRate / 50;  // 16 kHz, 20 ms = 320 samples
static Ts3Client client;                              // ~81 KB, keep off the task stack
static OpusEncoder *enc;
static OpusDecoder *dec;
static volatile bool ptt_down;      // set from the pwrkey IRQ
static bool talking;
static audio_record_handle mic;
static audio_track_handle spk;

// Incoming voice: decode to PCM and push to the speaker. opus_decode output is bounded by the
// frame size we ask for; kFrame*6 covers the largest Opus frame (120 ms) safely.
static void on_voice(void *, uint16_t clid, uint8_t codec, const uint8_t *opus, size_t len) {
    (void)clid;
    if (talking) return;            // half duplex
    if (codec != 4 && codec != 5) return;
    if (len == 0) return;           // PTT release from peer
    static int16_t pcm[kFrame * 6];
    int n = opus_decode(dec, opus, (int)len, pcm, kFrame * 6, 0);
    if (n > 0) ql_audio_track_write(spk, (char *)pcm, (unsigned)n * 2);
}

static void on_pwrkey(void) { ptt_down = (ql_get_pwrkey_status() != 0); }  // TODO(deploy): side key?

static void ts3_entry(void *) {
    port_log("ts3 app start");
    if (ql_start_data_call(PROFILE_IDX, 0 /*IPv4*/, (char *)APN_NAME, nullptr, nullptr, 0) != 0) {
        port_log("data call failed");
        for (;;) ql_rtos_task_sleep_s(5);
    }

    ql_codec_choose(AUD_INTERNAL_CODEC, nullptr);
    ql_set_audio_path_receiver();
    ql_set_volume(AUDIOHAL_SPK_VOL_7);  // TODO(deploy): MUTE..VOL_9, tune for the speaker PA
    mic = ql_audio_record_open(kRate, 1, 0, nullptr);
    spk = ql_audio_track_open(kRate, 1, 0x100, nullptr);
    ql_pwrkey_register_irq(on_pwrkey);

    int err;
    enc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &err);
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(3));  // ponytail: tune per CPU budget
    dec = opus_decoder_create(kRate, 1, &err);

    Ts3Config cfg;
    cfg.host = TS3_HOST;
    cfg.nickname = "EC600M";
    if (!ts3c::id_create(cfg.identity) || !ts3c::id_improve(cfg.identity, 8)) {
        port_log("identity failed");
        for (;;) ql_rtos_task_sleep_s(5);
    }
    client.on_voice = on_voice;

    for (;;) {
        if (!client.connect(cfg)) { ql_rtos_task_sleep_s(3); continue; }
        while (client.state() != Ts3Client::Closed) {
            client.poll(talking ? 2 : 10);
            if (client.state() != Ts3Client::Connected) continue;

            if (ptt_down && !talking) talking = true;
            if (talking) {
                int16_t frame[kFrame];
                unsigned got = sizeof frame;
                // ql_audio_record_read returns 0 on success and fills *count with bytes read.
                if (ql_audio_record_read(mic, (char *)frame, &got) == 0 && got == sizeof frame) {
                    uint8_t out[256];
                    int n = opus_encode(enc, frame, kFrame, out, sizeof out);
                    if (n > 0) client.send_voice(out, n);
                }
                if (!ptt_down) {            // released
                    client.send_voice(nullptr, 0);
                    talking = false;
                }
            }
        }
        ql_rtos_task_sleep_s(3);
    }
}

// Register with the SDK loader (the _appRegTable_ seen in the vendor image). This macro declares
// a table entry in a dedicated section, so it MUST be at file scope, not inside a function.
// 32 KiB stack: the Opus encoder is the hungriest caller. TODO(deploy): tune after measuring.
application_init(ts3_entry, "ts3", 32, 0);
