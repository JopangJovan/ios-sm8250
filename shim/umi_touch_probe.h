/* Local, opt-in pre-XNU diagnostic. No firmware/flash or UFS writes.
 * Reuses the applied clean RPMh helper, retaining its whitelist/timeout checks.
 * Test owns only touch SDA36/SCL37/reset38/AVDD69 until reboot to fastboot.
 */
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_size_t;
static vm_offset_t ml_io_map(vm_offset_t p, vm_size_t n) { (void)n; return p; }
static void delay(unsigned us)
{
    uint64_t end = now() + (rsr("cntfrq_el0") * us + 999999) / 1000000;
    while (now() < end) { }
}
static int probe_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int probe_log(const char *fmt, ...) { (void)fmt; return 0; }
#define strcmp probe_strcmp
#define printf probe_log
#define ARM64_BOARD_CONFIG_SWEET 1
#define ARM64_BOARD_CONFIG_UMI 1
#include "out/umi-touch-rpmh.inc"
#undef printf
#undef strcmp

static uintptr_t tp_pad(unsigned n) { return 0x0f500000UL + n * 0x1000UL; }
static void tp_write(uintptr_t p, uint32_t v)
{
    wr32(p, v);
    __asm__ volatile("dsb sy" ::: "memory");
}
static unsigned tp_get(unsigned n) { return rd32(tp_pad(n) + 4) & 1; }
/* Open drain: never drive SDA/SCL high. Release uses GPIO input+pull-up. */
static void tp_line(unsigned n, bool high)
{
    uintptr_t p = tp_pad(n);
    uint32_t cfg = rd32(p) & ~0x3ffu;
    if (!high) tp_write(p + 4, rd32(p + 4) & ~2u);
    tp_write(p, cfg | (high ? 3u : 0x200u));
    delay(5);
}
static void tp_output(unsigned n, bool high)
{
    uintptr_t p = tp_pad(n);
    tp_write(p + 4, (rd32(p + 4) & ~2u) | (high ? 2u : 0));
    tp_write(p, (rd32(p) & ~0x3ffu) | 0x200u);
}
static int tp_clock(void)
{
    tp_line(37, true);
    for (unsigned i = 0; i < 1000; ++i) {
        if (tp_get(37)) return 0;
        delay(1);
    }
    return -1;
}
static int tp_start(void)
{
    tp_line(36, true);
    if (tp_clock() || !tp_get(36)) return -1;
    tp_line(36, false); tp_line(37, false);
    return 0;
}
static void tp_stop(void)
{
    tp_line(36, false); (void)tp_clock(); tp_line(36, true);
}
static int tp_send(uint8_t b)
{
    for (unsigned i = 0; i < 8; ++i) {
        tp_line(36, (b & 0x80) != 0); b <<= 1;
        if (tp_clock()) return -1;
        tp_line(37, false);
    }
    tp_line(36, true);
    if (tp_clock()) return -1;
    int err = tp_get(36) ? -1 : 0;
    tp_line(37, false);
    return err;
}
static int tp_xfer(const uint8_t *tx, unsigned nt, uint8_t *rx, unsigned nr)
{
    int err = tp_start();
    if (!err) err = tp_send(0x92);
    for (unsigned i = 0; !err && i < nt; ++i) err = tp_send(tx[i]);
    if (!err && nr) {
        err = tp_start();
        if (!err) err = tp_send(0x93);
    }
    for (unsigned i = 0; !err && i < nr; ++i) {
        uint8_t b = 0;
        tp_line(36, true);
        for (unsigned j = 0; j < 8; ++j) {
            if (tp_clock()) { err = -1; break; }
            b = (b << 1) | tp_get(36); tp_line(37, false);
        }
        rx[i] = b;
        if (!err) {
            tp_line(36, i + 1 == nr);
            err = tp_clock(); tp_line(37, false);
        }
    }
    tp_stop();
    return err;
}
#if defined(UMI_PANEL_SYNC_PROBE)
#include "umi_panel_sync_probe.h"
#endif
#include "umi_touch_supply_probe.h"
static void umi_touch_probe(void)
{
    con_clear(BLUE);
    con_str("UMI TOUCH", "pre-XNU direct GPIO diagnostic; no iOS boot");
    tp_boost_snapshot();
#if defined(UMI_BOOST_ENABLE_PROBE)
    int boosterr = tp_boost_enable_probe();
    con_hex("BOOST SET", boosterr);
    tp_boost_snapshot();
    if (boosterr) { delay_ms(15000); return; }
#endif
    tp_supply_snapshot("L1 PRIOR");
    int err = sweet_rpmh_vreg_enable("ldoc1", 1800);
    con_hex("VDD vote errno", err);
    tp_supply_snapshot("L1 AFTER");
    if (err) { delay_ms(15000); return; }
    tp_output(38, false); tp_output(69, true); delay_ms(20);
    tp_line(36, true); tp_line(37, true);
    for (unsigned i = 0; i < 9; ++i) { tp_line(37, false); (void)tp_clock(); }
    tp_stop(); tp_output(38, true); delay_ms(100);
    uint8_t chipcmd[] = {0xfa, 0x20, 0, 0, 0}, id[4] = {0};
    err = tp_xfer(chipcmd, sizeof(chipcmd), id, sizeof(id));
    con_hex2("CHIP", "errno=", err, "id=", (id[0] << 8) | id[1]);
    if (err || id[0] != 0x36 || id[1] != 0x39) { delay_ms(15000); return; }
    tp_write(tp_pad(39), (rd32(tp_pad(39)) & ~0x3ffu) | 0xc3u);
    uint8_t fifo = 0x86, packet[8], batch[31 * 8];
    /* Drain fresh reset status before applying the captured recovery sequence. */
    for (unsigned i = 0; i < 32; ++i) {
        if (tp_xfer(&fifo, 1, packet, 8) || !packet[0]) break;
    }
    uint8_t scan[] = {0xa0, 0, 1};
    uint8_t gestures[] = {0xa2, 3, 0, 0, 0, 3};
    err = tp_xfer(scan, 3, NULL, 0);
#if defined(UMI_FTS_COLD_REPLAY)
    /* Real cold-probe trace, unlike stm_fts_cmd F1: fts_init_sensing()
     * CONFIG_TOUCHSCREEN_FOD stops scan for WAIT_AFTER_SENSEOFF (50ms).
     * C1 05 is fts_read_touchmode_data(), read-only, not a calibration write. */
    con_str("PROFILE", "captured TWRP cold init (50ms scan off)");
    if (!err) err = tp_xfer(gestures, 6, NULL, 0);
    scan[2] = 0;
    if (!err) err = tp_xfer(scan, 3, NULL, 0);
    delay_ms(53);
    scan[2] = 1;
    if (!err) err = tp_xfer(scan, 3, NULL, 0);
    uint8_t modecmd[] = {0xc1, 5}, mode[7] = {0};
    if (!err) err = tp_xfer(modecmd, 2, mode, 7);
    if (!err) con_hex2("MODE", "first4=", ((uint32_t)mode[0]<<24) |
        ((uint32_t)mode[1]<<16) | ((uint32_t)mode[2]<<8) | mode[3],
        "last3=", ((uint32_t)mode[4]<<16) | ((uint32_t)mode[5]<<8) | mode[6]);
#else
    uint8_t feature[] = {0xa2, 0, 0};
    for (unsigned i = 0; !err && i < 3; ++i) {
        feature[1] = i; err = tp_xfer(feature, 3, NULL, 0);
    }
    if (!err) err = tp_xfer(scan, 3, NULL, 0);
    if (!err) err = tp_xfer(gestures, 6, NULL, 0);
#endif
    con_hex("INIT errno", err);
    if (err) { delay_ms(15000); return; }
    uint8_t infocmd[] = {0xa6, 0, 0}, info[208];
    err = tp_xfer(infocmd, 3, info, sizeof(info));
    unsigned raw = err ? 0 : info[0x88] | (info[0x89] << 8);
    if (err || info[0] != 0xa5 || !raw || raw > 0xffc0) {
        con_str("RAW", "invalid sysinfo; stopping"); delay_ms(15000); return;
    }
    con_hex2("CONFIG", "cfg=", info[0x14] | (info[0x15] << 8), "raw=", raw);
    con_str("TEST", "SWIPE for 30 seconds; observe RAW changes");
    unsigned events = 0, fingers = 0, errors = 0;
    unsigned changes = 0, samples = 0;
    uint32_t previous = 0;
    uint64_t end = now() + rsr("cntfrq_el0") * 30, report = now(), sample = now();
#if defined(UMI_PANEL_SYNC_PROBE)
    uint64_t sync_at = now() + rsr("cntfrq_el0") * 10;
    bool sync_done = false;
#endif
    while (now() < end) {
#if defined(UMI_PANEL_SYNC_PROBE)
        if (!sync_done && now() >= sync_at) {
            con_hex2("BEFORE", "samples=", samples, "changes=", changes);
            int syncerr = tp_panel_sync();
            con_hex("SYNC ERR", syncerr);
            if (syncerr) { delay_ms(15000); return; }
            con_str("AFTER", "TSP SYNC replayed; swipe now");
            sync_done = true;
            changes = samples = 0;
        }
#endif
        if (!tp_get(39)) {
            err = tp_xfer(&fifo, 1, packet, 8);
            if (err) ++errors;
            else if (packet[0]) {
                unsigned remaining = packet[7] & 31;
                if (remaining && tp_xfer(&fifo, 1, batch, remaining * 8)) {
                    ++errors; remaining = 0;
                }
                for (unsigned i = 0; i <= remaining; ++i) {
                    const uint8_t *p = i ? batch + (i - 1) * 8 : packet;
                    if (!p[0]) continue;
                    ++events;
                    if (p[0] == 0x13 || p[0] == 0x23 || p[0] == 0x33) {
                        ++fingers;
                        if (fingers <= 3)
                            con_hex2("FINGER", "x=", ((p[3] & 15) << 8) | p[2],
                                "y=", (p[4] << 4) | (p[3] >> 4));
                    }
                }
            }
        }
        if (now() >= sample) {
            uint8_t framecmd[] = {0xa6, raw >> 8, raw}, frame[64];
            if (tp_xfer(framecmd, 3, frame, sizeof(frame))) ++errors;
            else {
                uint32_t hash = 2166136261u;
                for (unsigned i = 0; i < sizeof(frame); ++i)
                    hash = (hash ^ frame[i]) * 16777619u;
                if (samples && hash != previous) ++changes;
                previous = hash; ++samples;
                if (samples == 1) {
                    con_hex2("RAW DATA", "word0=", frame[0] | (frame[1] << 8),
                        "word1=", frame[2] | (frame[3] << 8));
                    int min=32767, max=-32768;
                    for (unsigned i=0; i<sizeof(frame); i+=2) {
                        int v=(int16_t)(frame[i] | (frame[i+1] << 8));
                        if (v<min) min=v;
                        if (v>max) max=v;
                    }
                    con_hex2("RAW RANGE", "min=", min, "max=", max);
                }
            }
            sample = now() + rsr("cntfrq_el0") / 4;
        }
        if (now() >= report) {
            con_hex2("COUNTS", "events=", events, "fingers=", fingers);
            con_hex2("BUS", "errors=", errors, "irq=", tp_get(39));
            con_hex2("RAW", "hash=", previous, "changes=", changes);
            report = now() + rsr("cntfrq_el0") * 10;
        }
        delay_ms(8);
    }
    con_str("DONE", "Photograph these counters. Rebooting in 15 seconds.");
    con_hex2("FINAL", "events=", events, "fingers=", fingers);
    con_hex("ERRORS", errors);
    con_hex2("RAW END", "samples=", samples, "changes=", changes);
    delay_ms(15000);
}
