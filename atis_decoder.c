/*
 * atis_decoder.c
 *
 * Decodes ATIS from raw signed 16-bit PCM on stdin.
 * ITU-R M.493: 10-bit DSC symbols, 1200 baud, 1300/2100 Hz FSK.
 * Sample rate: 24000 Hz (24000/1200 = 20 samples/bit, exact).
 *
 * Time diversity: each symbol transmitted twice (+5 symbol offset).
 * After locking on DX phasing symbol, alternate read/skip over ALL
 * symbols starting from the first data symbol (<=99) found.
 *
 * Build:  gcc -O2 -o atis_decoder atis_decoder.c -lm
 * Run:    rtl_fm -f 156.500M -M fm -s 24000 -g 40 -l 50 | \
 *         ./atis_decoder 127.0.0.1 5005
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SAMPLE_RATE  24000
#define BAUD_RATE    1200
#define SPB          (SAMPLE_RATE / BAUD_RATE)   /* 20 samples/bit */
#define MARK_HZ      1300.0f
#define SPACE_HZ     2100.0f
#define SYM_DX       125

/* ── Goertzel ────────────────────────────────────────────── */
static float goertzel(const int16_t *s, int n, float hz)
{
    float w = 2.0f * (float)M_PI * hz / SAMPLE_RATE;
    float c = 2.0f * cosf(w), q1 = 0, q2 = 0;
    for (int i = 0; i < n; i++) {
        float q = c*q1 - q2 + s[i]; q2 = q1; q1 = q;
    }
    return q1*q1 + q2*q2 - q1*q2*c;
}

/* ── ITU-R M.493 10-bit symbol decode ────────────────────── */
static int sym_decode(const uint8_t *b)
{
    int v = 0;
    for (int i = 0; i < 7; i++) v |= b[i] << i;
    int zeros = 7 - __builtin_popcount((unsigned)v & 0x7f);
    int chk   = (b[7] << 2) | (b[8] << 1) | b[9];
    return (chk == zeros) ? v : -1;
}

/* ── UDP ─────────────────────────────────────────────────── */
static void udp_send(const char *host, int port, const char *msg)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host, &a.sin_addr);
    sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&a, sizeof a);
    close(fd);
}

/* ── Bit ring buffer ─────────────────────────────────────── */
#define RING 256
static uint8_t ring[RING];
static int rwr = 0, rrd = 0, rcnt = 0;
static void rpush(int b) { ring[rwr++ % RING] = b; rcnt++; }
static int  rpop(void)   { rcnt--; return ring[rrd++ % RING]; }
static int  rpeek(int i) { return ring[(rrd + i) % RING]; }

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);

    int16_t sb[SPB];
    int sn = 0;

    enum { HUNT, LOCKED } state = HUNT;
    int sym_bit  = 0;   /* bit position within current symbol (LOCKED) */
    int data[16];       /* collected data symbols */
    int dcnt     = 0;
    int started  = 0;   /* have we seen the first data symbol? */
    int parity   = 0;   /* alternating read/skip counter */

    int16_t s;
    while (fread(&s, 2, 1, stdin) == 1) {
        sb[sn++] = s;
        if (sn < SPB) continue;
        sn = 0;

        int bit = goertzel(sb, SPB, MARK_HZ) >
                  goertzel(sb, SPB, SPACE_HZ) ? 1 : 0;
        rpush(bit);

        /* ── HUNT: slide 1 bit at a time looking for DX (125) ── */
        if (state == HUNT) {
            while (rcnt >= 10) {
                uint8_t raw[10];
                for (int i = 0; i < 10; i++) raw[i] = rpeek(i);
                if (sym_decode(raw) == SYM_DX) {
                    for (int i = 0; i < 10; i++) rpop();
                    state   = LOCKED;
                    sym_bit = 0;
                    dcnt    = 0;
                    started = 0;
                    parity  = 0;
                    break;
                }
                rpop();
            }
            continue;
        }

        /* ── LOCKED: collect exactly 10 bits per symbol ─────── */
        sym_bit++;
        if (sym_bit < 10) continue;
        sym_bit = 0;

        uint8_t raw[10];
        for (int i = 0; i < 10; i++) raw[i] = rpop();
        int v = sym_decode(raw);

        if (v < 0) {
            state   = HUNT;
            dcnt    = 0;
            started = 0;
            continue;
        }

        /* wait for first data symbol (<=99) before alternating */
        if (!started) {
            if (v > 99) continue;  /* still in preamble/FS area */
            started = 1;
            parity  = 0;           /* this first symbol is a READ */
        }

        /* alternate: even parity = READ, odd parity = SKIP */
        if (parity % 2 == 0) {
            if (v <= 99 && dcnt < 16)
                data[dcnt++] = v;
        }
        parity++;

        if (dcnt == 5) {
            char out[24];
            int len = 0;
            for (int i = 0; i < 5; i++)
                len += snprintf(out+len, sizeof(out)-len, "%02d", data[i]);
            out[len] = '\0';

            /* strip leading zero (odd-digit ATIS numbers) */
            char *p = (len == 10 && out[0] == '0') ? out+1 : out;

            fprintf(stdout, "ATIS: %s\n", p);
            fflush(stdout);

            char msg[32];
            snprintf(msg, sizeof msg, "%s\n", p);
            udp_send(host, port, msg);

            state   = HUNT;
            dcnt    = 0;
            started = 0;
        }
    }
    return 0;
}
