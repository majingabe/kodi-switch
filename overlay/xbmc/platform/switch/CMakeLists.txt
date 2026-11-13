set(SOURCES PlatformSwitch.cpp
            SwitchStorageProvider.cpp
            SwitchNetwork.cpp
            SwitchSystemInfo.cpp
            SwitchSpeech.cpp
            SwitchCompat.cpp
            AESinkSwitch.cpp
            MemUtils.cpp)
set(HEADERS PlatformSwitch.h
            AESinkSwitch.h)

core_add_library(platform_switch)
