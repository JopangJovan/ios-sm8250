/* PM8150L L1, SID5 peripheral40: observer EXT_READL only, matching the
 * existing key driver's proven v5 SPMI transport. No PMIC register writes.
 * This reads programmed state/status, not an electrical voltage measurement.
 */
static bool tp_supply_poisoned;
static int tp_supply_read(unsigned reg, uint8_t *out)
{
    if (tp_supply_poisoned || (reg != 0x08 && reg != 0x40 && reg != 0x41 &&
        reg != 0x44 && reg != 0x46)) return -1;
    uintptr_t core = 0x0c440000UL, cfg = 0x0c40a000UL;
    uint32_t version = rd32(core), count = rd32(core + 4) & 0x7ff;
    if (version < 0x50000000u || version >= 0x70000000u || !count || count > 512)
        return -2;
    unsigned apid = 512;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t map = rd32(core + 0x900 + i * 4);
        if (!map || ((map >> 8) & 0xfff) != 0x540) continue;
        if (apid == 512 || (rd32(cfg + 0x700 + i * 4) & 7) == 0) apid = i;
    }
    if (apid == 512) return -3;
    uintptr_t observer = 0x0e600000UL + apid * 0x80;
    tp_write(observer, (1u << 27) | (reg << 4));
    for (unsigned i = 0; i < 1000; ++i) {
        uint32_t status = rd32(observer + 8);
        if (status & 1) {
            if (status & 14) return -4;
            *out = rd32(observer + 0x18); return 0;
        }
        delay(1);
    }
    tp_supply_poisoned = true;
    return -5;
}
static void tp_supply_snapshot(const char *label)
{
    uint8_t lo=0, hi=0, mode=0, enable=0, status=0;
    int err = tp_supply_read(0x40, &lo);
    if (!err) err = tp_supply_read(0x41, &hi);
    if (!err) err = tp_supply_read(0x44, &mode);
    if (!err) err = tp_supply_read(0x46, &enable);
    if (!err) err = tp_supply_read(0x08, &status);
    con_hex2(label, "errno=", err, "set-mV=", lo | (hi << 8));
    if (!err) con_hex2("L1 STATE", "mode/en=", (mode << 8) | enable, "status=", status);
}
