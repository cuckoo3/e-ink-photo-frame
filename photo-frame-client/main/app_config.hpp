#pragma once
#include "sdkconfig.h"

class AppConfig {
public:
    inline static constexpr const char* MULTICAST_IP = CONFIG_APP_MULTICAST;
    inline static constexpr uint16_t UDP_PORT        = CONFIG_APP_UDP_PORT;
	
	inline static constexpr uint8_t MAX_FILE_COUNT	= 15;
	inline static constexpr uint8_t MAX_FILENAME_LEN = 32;	// Fixed 32-byte slot per filename
	inline static constexpr const char* STORAGE_PATH = "/data";
	
	inline static constexpr const char* TIME_ZONE = "EST5EDT,M3.2.0,M11.1.0";
};
