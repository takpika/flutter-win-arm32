/* Borrowed binary/UTF-8 views; valid only during the message callback. */
#ifndef WPN_FCM_MESSAGE_H
#define WPN_FCM_MESSAGE_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const unsigned char *data;
    size_t size;
} wpn_fcm_view;
typedef struct {
    wpn_fcm_view key, value;
} wpn_fcm_pair;
typedef struct {
    wpn_fcm_view id, from, to, category, collapse_key, persistent_id, raw_data;
    /* Inbound MCS timestamps are milliseconds. Keep the historical member name
     * as an alias, without growing or reordering the public message layout. */
    union { uint64_t sent_milliseconds; uint64_t sent_seconds; };
    uint64_t ttl_seconds;
    /* A 16 KiB stanza cannot contain more than 2730 minimal AppData records. */
    wpn_fcm_pair data[2731];
    size_t data_count;
} wpn_fcm_message;
/* Strict protobuf parsing. All AppData entries (including google.*) survive. */
#ifdef __cplusplus
extern "C" {
#endif
int wpn_fcm_parse_message(const void *protobuf, size_t size, wpn_fcm_message *message);
#ifdef __cplusplus
}
#endif
#endif
