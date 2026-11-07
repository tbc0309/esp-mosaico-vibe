#pragma once
#include <array>
#include <atomic>
#include <string>
#include <esp_ota_ops.h>
#include <psa/crypto.h>

namespace bridge {
// Shared by uploaded BINs and confirmed online updates. Only one writer may own OTA.
class FirmwareWriter {
public:
    FirmwareWriter(size_t size,std::string sha256={},std::string version={});
    ~FirmwareWriter();
    FirmwareWriter(const FirmwareWriter &)=delete;
    FirmwareWriter &operator=(const FirmwareWriter &)=delete;
    bool write(const uint8_t *data,size_t size);
    bool finish();
    const char *error()const{return error_;}
    bool ready()const{return !*error_;}
private:
    bool fail(const char *error){error_=error;return false;}
    static std::atomic<bool> busy_;
    bool owned_=false,started_=false;
    const esp_partition_t *partition_=nullptr;
    esp_ota_handle_t handle_=0;
    size_t total_=0,received_=0,headerSize_=0;
    std::array<uint8_t,288> header_{};
    psa_hash_operation_t hash_=PSA_HASH_OPERATION_INIT;
    std::string checksum_,version_;
    const char *error_="";
};
}
