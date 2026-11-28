#ifndef ZZS_EEPROM_SHIM_H
#define ZZS_EEPROM_SHIM_H
/* eeprom.h - Strider has no EEPROM (only Pang 3 and the QSound boards do). The calls below are
 * reachable only when PangEEP / Cps1Qs / CpsBootlegEEPROM are set, which never happens here.
 * ASCII only. */
inline static INT32 EEPROMRead(void) { return 0; }
inline static void  EEPROMWrite(INT32 bit, INT32 reset, INT32 clk) { (void)bit; (void)reset; (void)clk; }
inline static void  EEPROMScan(INT32 nAction, INT32 *pnMin) { (void)nAction; (void)pnMin; }
#endif
