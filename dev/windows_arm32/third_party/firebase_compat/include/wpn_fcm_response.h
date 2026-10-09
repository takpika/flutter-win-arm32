#ifndef WPN_FCM_RESPONSE_H
#define WPN_FCM_RESPONSE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* V1 stays 104 bytes/alignment 4. V2 embeds that prefix without changing its
 * layout; pass &v2.base with base.size=sizeof(v2) to existing *_ex functions. */
typedef struct wpn_fcm_response {
    uint32_t size;
    int32_t http_status;
    char backend_error[96];
} wpn_fcm_response;
typedef struct wpn_fcm_response_v2 {
    wpn_fcm_response base;
    int64_t retry_at_ms; /* UTC milliseconds, -1 when absent/invalid. */
} wpn_fcm_response_v2;
static inline int wpn_fcm_response_valid(const wpn_fcm_response *response) {
    return response&&(response->size==sizeof(wpn_fcm_response)||response->size==sizeof(wpn_fcm_response_v2));
}
static inline void wpn_fcm_response_reset(wpn_fcm_response *response) {
    if(!wpn_fcm_response_valid(response))return;
    const uint32_t size=response->size;memset(response,0,size);response->size=size;
    if(size==sizeof(wpn_fcm_response_v2)) {
        const int64_t absent=-1;memcpy((char*)response+offsetof(wpn_fcm_response_v2,retry_at_ms),&absent,sizeof(absent));
    }
}
static inline int64_t wpn_fcm_response_retry_at(const wpn_fcm_response *response) {
    int64_t at=-1;
    if(response&&response->size==sizeof(wpn_fcm_response_v2))
        memcpy(&at,(const char*)response+offsetof(wpn_fcm_response_v2,retry_at_ms),sizeof(at));
    return at;
}
static inline void wpn_fcm_response_assign(wpn_fcm_response *out,const wpn_fcm_response_v2 *source) {
    if(!wpn_fcm_response_valid(out)||!source)return;
    const uint32_t size=out->size;
    memcpy(out,source,size);out->size=size; // Preserve a V1 caller's allocation size.
}
#endif
