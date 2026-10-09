/* Framework distribution ABI. Public C types never cross a C++ runtime. */
#ifndef WPN_RUNTIME_H
#define WPN_RUNTIME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Major in the high 16 bits, additive minor in the low 16 bits.
 * Breaking ABI changes require a new Framework identity and DLL name. */
#define WPN_RUNTIME_ABI_VERSION UINT32_C(0x00010005)
uint32_t wpn_runtime_abi_version(void);
#ifdef __cplusplus
}
#endif
#endif
