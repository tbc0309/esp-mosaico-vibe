#pragma once
#include <cstdint>
namespace bridge {
inline uint32_t networkRetryDelay(unsigned attempts){return attempts>=5?60000u:1000u<<(attempts>4?4:attempts);}
inline bool networkDue(uint32_t now,uint32_t deadline){return int32_t(now-deadline)>=0;}
}
