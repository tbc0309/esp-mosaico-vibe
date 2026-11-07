#include "app.h"
namespace bridge {
void updateStatus(JsonObject output) {
    output["enabled"] = false;
    output["state"] = "disabled";
}
bool updateRequest(bool, const std::string &, std::string &error) {
    error = "Use ESP-Mosaico Vibe Mode to install .irisfw updates";
    return false;
}
}
