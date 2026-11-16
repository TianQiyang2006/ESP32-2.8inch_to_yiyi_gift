#pragma once
#include <cstring>
namespace CaptivePortal {
static const char * const Address = "http://192.168.4.1/";
static const char * const ProbePaths[] = {
    "/generate_204", "/gen_204", "/connectivitycheck/generate_204",
    "/hotspot-detect.html", "/library/test/success.html",
    "/connecttest.txt", "/ncsi.txt", "/fwlink", "/canonical.html", "/success.txt"
};
inline bool redirectUnknown(const char *path, bool get) {
    return get && std::strncmp(path, "/api/", 5) != 0;
}
}
