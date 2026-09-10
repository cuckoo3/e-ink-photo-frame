#pragma once
#include "sdkconfig.h"

class AppConfig {
public:
    // 常數，編譯期替換，無額外記憶體佔用
    inline static constexpr const char* MULTICAST_IP = CONFIG_APP_MULTICAST;
    inline static constexpr uint16_t UDP_PORT        = CONFIG_APP_UDP_PORT;
};
