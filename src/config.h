#ifndef CONFIG_H
#define CONFIG_H

// Max message size
#define MESSAGE_MAX_LEN 1024

// Max length of Connection String
#define CONN_STR_MAX_LEN 512

// Set to true to force WiFiManager to previously saved settings
#define RESET_CONFIG    false

// Use the following to control logging output channel & verbosity
// See https://github.com/thijse/Arduino-Log/blob/master/README.md
#define LOG_LEVEL   LOG_LEVEL_VERBOSE

// Uncomment this to get periodic Free Heap dump through Log.notice()
#define ENABLE_FREE_HEAP_LOGGING

// Uncomment this to enable logging from WiFiManager
#define ENABLE_WIFI_MANAGER_LOGGING true

// Uncomment this to get transport tracing from the Azure IoT C-SDK
// #define SDK_LOG_TRACING

#endif /* CONFIG_H */