#pragma once

// Per-hardware-version board configuration.
//
// HARDWARE_VERSION is supplied as a compile definition by the build system
// (see the top-level CMakeLists.txt). Build a specific version with:
//
//     idf.py -DHARDWARE_VERSION=2 build
//
// Only the values that genuinely differ between board revisions belong here;
// everything else stays in Kconfig / sdkconfig.defaults.

#ifndef HARDWARE_VERSION
#error "HARDWARE_VERSION is not defined; pass -DHARDWARE_VERSION=<n> to the build"
#endif

#if HARDWARE_VERSION == 1

#define BOARD_PB_PIN 13
#define BOARD_LR_PIN 17
#define BOARD_LG_PIN 18
#define BOARD_MICROPHONE_WS_PIN 14
#define BOARD_MICROPHONE_SCK_PIN 16
#define BOARD_MICROPHONE_DATA_PIN 15
#define BOARD_SPEAKER_WS_PIN 3
#define BOARD_SPEAKER_SCK_PIN 2
#define BOARD_SPEAKER_DATA_PIN 1

// Version 1 picks up RF interference from its own WiFi radio, so clamp the
// transmit power to 44 (~11 dBm). 0 means leave the radio at its default.
#define BOARD_WIFI_MAX_TX_POWER 44

#elif HARDWARE_VERSION == 2

#define BOARD_PB_PIN 13
#define BOARD_LR_PIN 17
#define BOARD_LG_PIN 18
#define BOARD_MICROPHONE_WS_PIN 14
#define BOARD_MICROPHONE_SCK_PIN 16
#define BOARD_MICROPHONE_DATA_PIN 15
#define BOARD_SPEAKER_WS_PIN 3
#define BOARD_SPEAKER_SCK_PIN 2
#define BOARD_SPEAKER_DATA_PIN 1

// Version 2 reworked the board to avoid the WiFi interference, so the radio
// runs at full (default) power.
#define BOARD_WIFI_MAX_TX_POWER 0

#else

#error "HARDWARE_VERSION must be 1 or 2"

#endif
