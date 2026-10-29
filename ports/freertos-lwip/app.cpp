// FreeRTOS app: crypto self-test, then a TS3 bot that
//  - echoes every voice frame it hears (RX path through the RTOS), and
//  - on "!talk" in channel chat, Opus-encodes a stored PCM clip on the RTOS and transmits it (TX path).
// On a radio, the clip becomes the mic and "!talk" becomes the PTT button.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <opus.h>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_DHCP.h"
}

#include "port.h"
#include "ts3.h"

#ifndef TS3_HOST
#define TS3_HOST "10.0.2.2"  // QEMU user networking: the host machine
#endif

int ts3_selftest_main();  // test/selftest.cpp, built with -Dmain=ts3_selftest_main
#ifdef TS3_CONFORMANCE_PORT
#include "port_conformance.h"
#endif

// 16 kHz mono s16le speech clip, linked in raw (stand-in for the microphone).
__asm__(".section .rodata.clip,\"a\"\n.balign 4\n.global clip_start\nclip_start:\n.incbin \"clip.raw\"\n"
        ".global clip_end\nclip_end:\n.previous\n");
extern "C" const int16_t clip_start[], clip_end[];

static const int kRate = 16000, kFrame = kRate / 50;  // 20 ms
static Ts3Client client;                              // ~81 KB, keep it off the task stack
static uint32_t echoed;
static bool talk_requested, talking, echoing;
static uint16_t my_channel;
static uint16_t bots[8];  // other "RTOS-*" clients: never echo them, or two bots ping-pong forever

static bool is_bot(uint16_t clid) {
    for (uint16_t b : bots) if (b == clid) return true;
    return false;
}

static void on_voice(void *, uint16_t clid, uint8_t codec, const uint8_t *opus, size_t len) {
    if (talking || is_bot(clid)) return;  // half duplex, like a radio
    if (len) {
        if (echoed++ % 50 == 0) port_log("echo: %u frames", echoed);
        echoing = true;
    } else if (!echoing) {
        return;  // only forward a PTT release for a burst we actually echoed
    } else {
        echoing = false;
    }
    client.send_voice(opus, len, codec);
}

static void say(const char *text) {
    char esc[256], cmd[320];
    ts3_escape(text, esc, sizeof esc);
    snprintf(cmd, sizeof cmd, "sendtextmessage targetmode=2 target=%u msg=%s", my_channel, esc);
    client.send_command(cmd);
}

static void on_command(void *, const char *cmd) {
    char v[64], nick[64];
    if (!strncmp(cmd, "notifycliententerview", 21) && ts3_arg(cmd, "clid", v, sizeof v) &&
        atoi(v) != client.client_id() && ts3_arg(cmd, "client_nickname", nick, sizeof nick) &&
        !strncmp(nick, "RTOS-", 5)) {
        for (uint16_t &b : bots) if (!b) { b = (uint16_t)atoi(v); break; }
        port_log("ignoring voice from other bot clid %s", v);
    }
    if (!strncmp(cmd, "notifycliententerview", 21) && ts3_arg(cmd, "clid", v, sizeof v) &&
        atoi(v) == client.client_id() && ts3_arg(cmd, "ctid", v, sizeof v)) {
        my_channel = (uint16_t)atoi(v);
        say("RTOS-Echo online (FreeRTOS, Cortex-M3). Bicara = aku pantulkan. Ketik !talk = aku yang bicara.");
    } else if (!strncmp(cmd, "notifytextmessage", 17) && ts3_arg(cmd, "msg", v, sizeof v) && !strcmp(v, "!talk")) {
        talk_requested = true;
    }
    if (!strncmp(cmd, "notifycliententerview", 21) || !strncmp(cmd, "notifyclientleftview", 20) ||
        !strncmp(cmd, "initserver", 10) || !strncmp(cmd, "notifytextmessage", 17))
        port_log("cmd: %.90s...", cmd);
}

static void ts3_task(void *) {
    uint32_t t0 = port_millis();
    ts3_selftest_main();
    port_log("selftest took %u ms on the emulated Cortex-M3", port_millis() - t0);
#ifdef TS3_CONFORMANCE_PORT
    // Built with `make CONF_PORT=<port>`; needs a UDP echo server on TS3_HOST:<port> (see ports/README.md).
    port_conformance_run(TS3_HOST, TS3_CONFORMANCE_PORT, nullptr);
#endif

    Ts3Config cfg;
    cfg.host = TS3_HOST;
    cfg.nickname = "RTOS-Echo";
    t0 = port_millis();
    if (!ts3c::id_create(cfg.identity) || !ts3c::id_improve(cfg.identity, 8)) {
        port_log("identity failed");
        for (;;) vTaskDelay(portMAX_DELAY);
    }
    port_log("identity ready in %u ms", port_millis() - t0);

    int err;
    OpusEncoder *enc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &err);
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(3));  // ponytail: tune per CPU; 0-10
    const int16_t *pcm = clip_start;
    const size_t total = (size_t)(clip_end - clip_start);

    client.on_voice = on_voice;
    client.on_command = on_command;
    for (;;) {
        port_log("ts3: connecting to %s:%u", cfg.host, cfg.port);
        if (!client.connect(cfg)) { vTaskDelay(pdMS_TO_TICKS(3000)); continue; }
        size_t pos = 0;
        uint32_t next = 0, enc_ms = 0, frames = 0;
        while (client.state() != Ts3Client::Closed) {
            client.poll(talking ? 2 : 10);
            if (talk_requested && !talking && client.state() == Ts3Client::Connected) {
                talk_requested = false, talking = true, pos = 0, enc_ms = 0, frames = 0;
                next = port_millis();
                port_log("ptt: talking (%u ms clip)", (unsigned)(total * 1000 / kRate));
            }
            if (!talking || port_millis() < next) continue;
            if (pos >= total) {
                client.send_voice(nullptr, 0);  // PTT released
                talking = false;
                port_log("ptt: released, %u frames, avg Opus encode %u.%02u ms/frame", frames,
                         enc_ms / frames, enc_ms * 100 / frames % 100);
                continue;
            }
            int16_t frame[kFrame] = {0};
            size_t n = total - pos < (size_t)kFrame ? total - pos : kFrame;
            memcpy(frame, pcm + pos, n * 2);
            uint8_t out[256];
            uint32_t te = port_millis();
            int len = opus_encode(enc, frame, kFrame, out, sizeof out);
            enc_ms += port_millis() - te;
            if (len > 0) client.send_voice(out, len), frames++;
            pos += kFrame;
            next += 20;
        }
        talking = false;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// Newer FreeRTOS+TCP requires this hook; the stock demo does not define it.
extern "C" eDHCPCallbackAnswer_t xApplicationDHCPHook_Multi(eDHCPCallbackPhase_t, struct xNetworkEndPoint *,
                                                            IP_Address_t *) {
    return eDHCPContinue;
}

// The stock demo's network-up hook calls this; we start the TS3 task instead of the TCP echo client.
extern "C" void vStartTCPEchoClientTasks_SingleTasks(uint16_t, UBaseType_t prio) {
    xTaskCreate(ts3_task, "ts3", 16384, nullptr, prio, nullptr);  // 64 KB stack (Opus encoder needs it)
}
