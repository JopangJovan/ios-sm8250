/* Opt-in diagnostic for this verified Samsung J2 P2.1 panel only.
 * Replays its vendor DTS TSP SYNC initialization through the inherited DSI0
 * FIFO, following Qualcomm dsi_ctrl_hw_cmn_kickoff_fifo_command().
 * No DMA memory, PHY/clock reset, sleep/display-mode, flash or timing changes.
 * Save/restore command-path settings; abort on disabled/busy/timeout state.
 */
#define TP_DSI 0x0ae94000UL
static int tp_dsi_packet(const uint8_t *payload, unsigned n)
{
    uint32_t ctrl = rd32(TP_DSI + 4), ver = rd32(TP_DSI);
    if (ver != 0x20040000u || (ctrl & 5) != 5 || n < 1 || n > 10)
        return -1;
    uint64_t end = now() + rsr("cntfrq_el0") / 5;
    while (rd32(TP_DSI + 8) & 6) if (now() >= end) return -2;
    uint32_t dma = rd32(TP_DSI + 0x3c), trig = rd32(TP_DSI + 0x84);
    uint32_t tpg = rd32(TP_DSI + 0x15c), len = rd32(TP_DSI + 0x4c);
    if (tpg != 0) return -3; /* Never replace an inherited TPG operation. */
    uint32_t irq = rd32(TP_DSI + 0x110);
    /* Status bits are W1C; clear only DMA_DONE, preserve IRQ mask bits. */
    const uint32_t status_mask = 0x51115501u;
    uint32_t irq_masks = irq & ~status_mask;
    tp_write(TP_DSI + 0x110, irq_masks | 3u);
    tp_write(TP_DSI + 0x84, (trig & ~0x1000fu) | 4u); /* SW trigger */
    tp_write(TP_DSI + 0x15c, 0x30006u);
    uint8_t buf[16];
    for (unsigned i = 0; i < sizeof(buf); ++i) buf[i] = 0xff;
    buf[0] = n; buf[1] = 0; buf[2] = 0x39; buf[3] = 0xc0;
    for (unsigned i = 0; i < n; ++i) buf[4 + i] = payload[i];
    unsigned size = (n + 7) & ~3u;
    for (unsigned i = 0; i < size; i += 4)
        tp_write(TP_DSI + 0x17c, (uint32_t)buf[i] | ((uint32_t)buf[i+1] << 8) |
            ((uint32_t)buf[i+2] << 16) | ((uint32_t)buf[i+3] << 24));
    if ((size / 4) & 1) tp_write(TP_DSI + 0x17c, 0);
    tp_write(TP_DSI + 0x3c, (dma & ~0xc0000000u) | (1u << 28) | (1u << 26));
    tp_write(TP_DSI + 0x4c, size);
    tp_write(TP_DSI + 0x90, 1);
    end = now() + rsr("cntfrq_el0") / 10;
    int err = 0;
    while (!(rd32(TP_DSI + 0x110) & 1)) {
        if (now() >= end) { err = -4; break; }
    }
    con_hex2("DSI TX", "errno=", err, "status=", rd32(TP_DSI + 0x110));
    if (err) return err; /* uncertain engine: do not reset/reuse its FIFO */
    tp_write(TP_DSI + 0x110, irq_masks | 1u);
    tp_write(TP_DSI + 0x15c, 0);
    tp_write(TP_DSI + 0x1ec, 1); delay(1); tp_write(TP_DSI + 0x1ec, 0);
    tp_write(TP_DSI + 0x15c, tpg);
    tp_write(TP_DSI + 0x4c, len);
    tp_write(TP_DSI + 0x3c, dma);
    tp_write(TP_DSI + 0x84, trig);
    return 0;
}
static int tp_panel_sync(void)
{
    con_hex2("DSI0", "version=", rd32(TP_DSI), "ctrl=", rd32(TP_DSI + 4));
    uint8_t unlock[] = {0xf0,0x5a,0x5a}, lock[] = {0xf0,0xa5,0xa5};
    uint8_t sync[] = {0xb9,0,0,0x14,0x18,0,0,0,0x11,3};
    int err = tp_dsi_packet(unlock, sizeof(unlock));
    if (err) return err;
    err = tp_dsi_packet(sync, sizeof(sync));
    if (err) return err; /* timeout quarantines engine until reboot */
    return tp_dsi_packet(lock, sizeof(lock));
}
