#pragma once
// SDK-scoped GNU compatibility for the unmodified Microsoft WRL headers.
#include <windows.h>
#include <weakreference.h>
#include <roapi.h>
#include <new.h>
#include <type_traits>
#include <utility>
#include <intrin.h>
#ifndef _Interlocked_operand_
#define _Interlocked_operand_
#endif
#define InterlockedCompareExchangeNoFence InterlockedCompareExchange
#define InterlockedIncrementNoFence InterlockedIncrement
#define InterlockedDecrementRelease InterlockedDecrement
#define _ARM_BARRIER_ISH 0xB
#ifndef _Deref_pre_z_
#define _Deref_pre_z_ __attribute__((annotate("_Deref_pre_z_")))
#endif
#ifndef JSCRIPT_E_CANTEXECUTE
#define JSCRIPT_E_CANTEXECUTE _HRESULT_TYPEDEF_(0x89020001L)
#endif
namespace Windows::Foundation {
inline HRESULT RegisterActivationFactories(HSTRING* ids, PFNGETACTIVATIONFACTORY* callbacks, UINT32 count, RO_REGISTRATION_COOKIE* cookie) {
  return RoRegisterActivationFactories(ids, callbacks, count, cookie);
}
inline void RevokeActivationFactories(RO_REGISTRATION_COOKIE cookie) {
  RoRevokeActivationFactories(cookie);
}
}
