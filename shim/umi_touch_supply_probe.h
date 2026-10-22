/* PM8150L L1, SID5 peripheral40: observer EXT_READL only, matching the
 * existing key driver's proven v5 SPMI transport. No PMIC register writes.
 * This reads programmed state/status, not an electrical voltage measurement.
 */
static bool tp_supply_poisoned[2];
static int tp_supply_read_ppid(unsigned ppid, unsigned reg, uint8_t *out)
{
    if (ppid != 0x540 && ppid != 0x2c4) return -1;
    unsigned bank = ppid == 0x540 ? 0 : 1;
    if (tp_supply_poisoned[bank] || (reg != 0x08 && reg != 0x40 && reg != 0x41 &&
        reg != 0x44 && reg != 0x46)) return -1;
    uintptr_t core = 0x0c440000UL, cfg = 0x0c40a000UL;
    uint32_t version = rd32(core), count = rd32(core + 4) & 0x7ff;
    if (version < 0x50000000u || version >= 0x70000000u || !count || count > 512)
        return -2;
    unsigned apid = 512;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t map = rd32(core + 0x900 + i * 4);
        if (!map || ((map >> 8) & 0xfff) != ppid) continue;
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
    tp_supply_poisoned[bank] = true;
    return -5;
}
static int tp_supply_read(unsigned reg, uint8_t *out)
{
    return tp_supply_read_ppid(0x540, reg, out);
}
static void tp_boost_snapshot(void)
{
    /* Stock always-on vdd_boost_vreg: PM8150B GPIO5. Read only; its
     * relationship to touch AVDD is not established by the device tree. */
    uint8_t mode=0, output=0, enable=0, status=0;
    int err = tp_supply_read_ppid(0x2c4, 0x40, &mode);
    if (!err) err = tp_supply_read_ppid(0x2c4, 0x44, &output);
    if (!err) err = tp_supply_read_ppid(0x2c4, 0x46, &enable);
    if (!err) err = tp_supply_read_ppid(0x2c4, 0x08, &status);
    con_hex("BOOST ERR", err);
    if (!err) con_hex2("BOOST", "mode/out=", (mode << 8) | output,
        "en/stat=", (enable << 8) | status);
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
