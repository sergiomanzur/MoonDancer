/* Moonwalker native port: Musashi configuration for the MC68000 in a Sega
 * System 18 board with the Hitachi FD1094 encryption wrapper.
 *
 * Musashi is MIT licensed (Karl Stenerud).  This file replaces Musashi's
 * stock m68kconf.h; the differences from the defaults are:
 *   - MC68000 only
 *   - separate program/data read callbacks, so opcode fetches can be
 *     decrypted while data reads see the original ROM
 *   - CMPI.L / RTE callbacks for the FD1094 state machine
 *   - interrupt acknowledge callback, so the FD1094 enters its IRQ key state
 *   - accurate long predecrement writes
 */
#ifndef M68KCONF__HEADER
#define M68KCONF__HEADER

#define M68K_OPT_OFF             0
#define M68K_OPT_ON              1
#define M68K_OPT_SPECIFY_HANDLER 2

#ifndef M68K_COMPILE_FOR_MAME
#define M68K_COMPILE_FOR_MAME M68K_OPT_OFF
#endif

/* Only the MC68000. */
#define M68K_EMULATE_010          M68K_OPT_OFF
#define M68K_EMULATE_EC020        M68K_OPT_OFF
#define M68K_EMULATE_020          M68K_OPT_OFF
#define M68K_EMULATE_030          M68K_OPT_OFF
#define M68K_EMULATE_040          M68K_OPT_OFF

/* Instruction-stream reads go through the FD1094 decryption; data reads do not. */
#define M68K_SEPARATE_READS       M68K_OPT_ON

/* Real 68000 writes the high word first on -(An) long writes. */
#define M68K_SIMULATE_PD_WRITES   M68K_OPT_ON

/* FD1094 needs to know when an interrupt is acknowledged and when RTE runs. */
#define M68K_EMULATE_INT_ACK      M68K_OPT_ON
#define M68K_EMULATE_BKPT_ACK     M68K_OPT_OFF
#define M68K_EMULATE_TRACE        M68K_OPT_OFF
#define M68K_EMULATE_RESET        M68K_OPT_ON
#define M68K_CMPILD_HAS_CALLBACK  M68K_OPT_ON
#define M68K_RTE_HAS_CALLBACK     M68K_OPT_ON

#define M68K_TAS_HAS_CALLBACK     M68K_OPT_OFF
#define M68K_ILLG_HAS_CALLBACK    M68K_OPT_ON
#define M68K_TRAP_HAS_CALLBACK    M68K_OPT_OFF
#define M68K_EMULATE_FC           M68K_OPT_OFF
#define M68K_MONITOR_PC           M68K_OPT_OFF

#ifdef MW_TRACE
#define M68K_INSTRUCTION_HOOK     M68K_OPT_ON
#else
#define M68K_INSTRUCTION_HOOK     M68K_OPT_OFF
#endif
#define M68K_EMULATE_PREFETCH     M68K_OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR M68K_OPT_OFF
#define M68K_LOG_ENABLE           M68K_OPT_OFF
#define M68K_LOG_1010_1111        M68K_OPT_OFF
#define M68K_LOG_TRAP             M68K_OPT_OFF
#define M68K_EMULATE_PMMU         M68K_OPT_OFF
#define M68K_USE_64_BIT           M68K_OPT_ON

#endif /* M68KCONF__HEADER */
