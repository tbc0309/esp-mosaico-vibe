#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "timezone_data.h"

namespace bridge { namespace timezone {
// Resolve the current offset from explicit Windows transition schedules.
// This covers government exceptions that cannot be represented by one TZ string.
inline bool resolve(const char *name, int64_t epoch, char *out, size_t capacity) {
    using namespace timezone_data;
    if (!name || epoch < 1577836800 || epoch >= 4133980800LL) return false;
    size_t low=0, high=sizeof(zones)/sizeof(zones[0]);
    while(low<high){size_t mid=(low+high)/2;if(strcmp(zones[mid].name,name)<0)low=mid+1;else high=mid;}
    if(low==sizeof(zones)/sizeof(zones[0]) || strcmp(zones[low].name,name)) return false;
    const auto &r=rules[zones[low].rules];
    const auto *rows=transitions+r.start;
    if(epoch>=rows[r.count-1].epoch){snprintf(out,capacity,"%s",r.tail);return true;}
    low=0;high=r.count;
    while(low<high){size_t mid=(low+high)/2;if(rows[mid].epoch<=epoch)low=mid+1;else high=mid;}
    int offset=-rows[low?low-1:0].offset;
    unsigned absolute=offset<0?-offset:offset;
    snprintf(out,capacity,"UTC%s%u:%02u:%02u",offset<0?"-":"",absolute/3600,absolute/60%60,absolute%60);
    return true;
}
} }
