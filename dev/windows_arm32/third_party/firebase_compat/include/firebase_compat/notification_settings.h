#ifndef FIREBASE_COMPAT_NOTIFICATION_SETTINGS_H
#define FIREBASE_COMPAT_NOTIFICATION_SETTINGS_H
#include <stdint.h>
enum fcm_sdk_notification_field {
    FCM_SDK_NOTIFICATION_AUTHORIZATION=1u,
    FCM_SDK_NOTIFICATION_ALERT=2u, FCM_SDK_NOTIFICATION_BADGE=4u,
    FCM_SDK_NOTIFICATION_SOUND=8u, FCM_SDK_NOTIFICATION_ANNOUNCEMENT=16u,
    FCM_SDK_NOTIFICATION_CRITICAL_ALERT=32u, FCM_SDK_NOTIFICATION_CARPLAY=64u,
    FCM_SDK_NOTIFICATION_LOCK_SCREEN=128u, FCM_SDK_NOTIFICATION_CENTER=256u,
    FCM_SDK_NOTIFICATION_PREVIEWS=512u, FCM_SDK_NOTIFICATION_TIME_SENSITIVE=1024u,
    FCM_SDK_NOTIFICATION_SCHEDULED_DELIVERY=2048u, FCM_SDK_NOTIFICATION_APP_SETTINGS=4096u,
    FCM_SDK_NOTIFICATION_PROVISIONAL=8192u
};
enum fcm_sdk_notification_authorization {
    FCM_SDK_NOTIFICATION_UNSUPPORTED=-1,
    FCM_SDK_NOTIFICATION_DENIED=1, FCM_SDK_NOTIFICATION_AUTHORIZED=2
};
typedef struct fcm_sdk_notification_settings {
    uint32_t size;
    int32_t windows_setting; /* Native NotificationSetting, unknown preserved. */
    int32_t authorization;
    uint32_t supported_fields;
    uint32_t unsupported_requested_fields;
    uint32_t prompt_supported;
} fcm_sdk_notification_settings;
#endif
