#pragma once
#include <stdint.h>

struct ChannelContext{
    bool online = false;
    int64_t next_ad{};
};

