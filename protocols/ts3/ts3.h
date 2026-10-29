// Minimal TeamSpeak 3 client: handshake, commands, ping, voice packets.
// Single-threaded: call poll() from one task, and send_*() from that same task.
// Audio codec is NOT in here — hand it Opus frames, get Opus frames back.
#pragma once
#include <cstddef>
#include <cstdint>

#include "crypto.h"

struct Ts3Config {
    const char *host;
    uint16_t port = 9987;
    const char *nickname = "PoC";
    const char *channel = "";           // name, or "/<cid>"
    const char *server_password = "";
    const char *channel_password = "";
    ts3c::Identity identity;            // persist this (create once with id_create + id_improve)
};

class Ts3Client {
public:
    enum State { Idle, Init, Handshake, Connecting, Connected, Closed };
    enum Codec : uint8_t { OpusVoice = 4, OpusMusic = 5 };

    void *user = nullptr;
    // Raw (escaped) command text, e.g. "notifycliententerview ...".
    void (*on_command)(void *user, const char *cmd) = nullptr;
    // len == 0 means the speaker released PTT.
    void (*on_voice)(void *user, uint16_t clid, uint8_t codec, const uint8_t *opus, size_t len) = nullptr;

    bool connect(const Ts3Config &cfg);
    void poll(uint32_t timeout_ms);
    bool send_command(const char *cmd);  // already escaped, see ts3_escape()
    bool send_voice(const uint8_t *opus, size_t len, uint8_t codec = OpusVoice);  // len 0 = end of TX
    void disconnect(const char *reason = "leaving");

    State state() const { return st_; }
    uint16_t client_id() const { return clid_; }

private:
    struct Pending {
        bool used;
        uint8_t type;
        uint16_t pid;
        uint16_t len;
        uint32_t first, next, rto;
        uint8_t buf[500];
    };
    struct Frag {
        bool active, compressed;
        size_t len;
    };

    void tick();
    void fail(const char *why);
    void send_init(const uint8_t *body, size_t len);
    void handle(uint8_t *buf, size_t n);
    void handle_init(const uint8_t *d, size_t n);
    void handle_command_packet(uint8_t type, uint16_t pid, uint8_t flags, const uint8_t *d, size_t n);
    void handle_command(char *cmd);
    void handle_initivexpand2(char *cmd);
    void send_clientinit();
    bool send_packet(uint8_t type, uint8_t flags, const uint8_t *d, size_t n, int crypt, bool reliable);
    bool send_cmd(const char *cmd, int crypt);
    void send_ack(uint8_t type, uint16_t pid);
    void key_nonce(bool c2s, uint8_t type, uint16_t pid, uint32_t gen, uint8_t key[16], uint8_t nonce[16]);
    uint32_t in_gen(uint8_t type, uint16_t pid);

    Ts3Config cfg_{};
    State st_ = Idle;
    int sock_ = -1;
    uint16_t clid_ = 0;
    uint32_t version_ = 0, last_rx_ = 0, last_ping_ = 0;
    uint8_t random0_[4]{}, alpha_[10]{}, init_step_ = 0;
    uint8_t init_buf_[500]{};
    size_t init_len_ = 0;
    uint32_t init_next_ = 0, init_first_ = 0;

    bool have_params_ = false;
    uint8_t iv_[64]{}, mac_[8]{};

    uint16_t out_pid_[9]{};
    uint32_t out_gen_[9]{};
    uint16_t in_next_[9]{};
    uint32_t in_gen_[9]{};

    static const int kPending = 16, kReorder = 8;
    Pending pend_[kPending]{};
    // Out-of-order command packets waiting for their turn, per Command/CommandLow.
    struct Held { bool used; uint16_t pid; uint8_t flags; uint16_t len; uint8_t buf[500]; } held_[2][kReorder]{};
    // ponytail: fixed reassembly buffers; raise if the server has huge channel lists.
    static const size_t kFragMax = 16384, kCmdMax = 32768;
    Frag frag_[2]{};
    uint8_t frag_buf_[2][kFragMax]{};
    char cmd_buf_[kCmdMax + 1]{};
};

// Escape a value for a command argument. Returns length, or -1 if it does not fit.
int ts3_escape(const char *in, char *out, size_t cap);
// Find `key=` in the first part of a command and unescape its value into out.
bool ts3_arg(const char *cmd, const char *key, char *out, size_t cap);
