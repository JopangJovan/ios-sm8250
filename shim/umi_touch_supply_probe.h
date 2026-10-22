/* PM8150L L1, SID5 peripheral40: observer EXT_READL only, matching the
 * existing key driver's proven v5 SPMI transport. Read-only by default;
 * UMI_BOOST_ENABLE_PROBE below explicitly enables one whitelisted GPIO write.
 * This reads programmed state/status, not an electrical voltage measurement.
 */
static bool tp_supply_poisoned[2];
static int tp_supply_read_ppid(unsigned ppid, unsigned reg, uint8_t *out)
{
    if (ppid != 0x540 && ppid != 0x2c4) return -1;
    unsigned bank = ppid == 0x540 ? 0 : 1;
    if (tp_supply_poisoned[bank] || (reg != 0x05 && reg != 0x08 && reg != 0x40 && reg != 0x41 &&
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

#if defined(UMI_BOOST_ENABLE_PROBE)
/* Sole opt-in PMIC write: SID2 GPIO5's output value, C444, 00 <-> 80.
 * Stock UMI DT marks this regulator always-on. No other PMIC writes allowed.
 * GPIO must already match measured MV/output/VIN1/enabled configuration.
 * Restore original low after diagnostic; timeout quarantines writes.
 */
static unsigned tp_boost_write_apid;
static bool tp_boost_write_poisoned, tp_boost_changed;
static int tp_boost_write_value(uint8_t value)
{
    if (tp_boost_write_poisoned || (value != 0 && value != 0x80)) return -1;
    uintptr_t channel = 0x0c600000UL + tp_boost_write_apid * 0x10000UL;
    tp_write(channel + 0x10, value);
    tp_write(channel, 0x44u << 4); /* EXT_WRITEL opcode0, one byte, C444 */
    for (unsigned i = 0; i < 1000; ++i) {
        uint32_t status = rd32(channel + 8);
        if (status & 1) {
            if (status & 14) { tp_boost_write_poisoned = true; return -2; }
            uint8_t actual=0;
            if (tp_supply_read_ppid(0x2c4, 0x44, &actual) || actual != value) {
                tp_boost_write_poisoned = true; return -3;
            }
            return 0;
        }
        delay(1);
    }
    tp_boost_write_poisoned = true;
    return -4;
}
static int tp_boost_enable_probe(void)
{
    uint8_t subtype=0, mode=0, vin=0, output=0, enable=0;
    if (tp_supply_read_ppid(0x2c4,5,&subtype) ||
        tp_supply_read_ppid(0x2c4,0x40,&mode) ||
        tp_supply_read_ppid(0x2c4,0x41,&vin) ||
        tp_supply_read_ppid(0x2c4,0x44,&output) ||
        tp_supply_read_ppid(0x2c4,0x46,&enable)) return -1;
    if (subtype != 0x11 || mode != 1 || vin != 1 || enable != 0x80 ||
        (output != 0 && output != 0x80)) return -2;
    if (output == 0x80) return 0;
    unsigned count = rd32(0x0c440004UL) & 0x7ff, matches=0;
    if (!count || count > 512) return -3;
    for (unsigned i=0; i<count; ++i) {
        uint32_t map=rd32(0x0c440900UL + i*4);
        if (map && ((map>>8)&0xfff)==0x2c4 &&
            (rd32(0x0c40a700UL + i*4)&7)==0) {
            tp_boost_write_apid=i; ++matches;
        }
    }
    if (matches != 1) return -4;
    int err=tp_boost_write_value(0x80);
    if (!err) { tp_boost_changed=true; delay_ms(5); }
    return err;
}
static void tp_boost_restore_probe(void)
{
    if (!tp_boost_changed) return;
    int err=tp_boost_write_value(0);
    con_hex("BRESTORE", err);
    delay_ms(1000);
}
#endif
