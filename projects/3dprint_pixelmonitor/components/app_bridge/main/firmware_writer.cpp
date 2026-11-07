#include "firmware_writer.h"
#include "update_policy.h"
#include "board.h"
#include <algorithm>

namespace bridge {
std::atomic<bool> FirmwareWriter::busy_{false};
FirmwareWriter::FirmwareWriter(size_t size,std::string checksum,std::string version):total_(size),checksum_(std::move(checksum)),version_(std::move(version)){
    owned_=!busy_.exchange(true);
    if(!owned_){fail("ota_busy");return;}
    partition_=esp_ota_get_next_update_partition(nullptr);
    if(!partition_||size<header_.size()||size>partition_->size){fail("image_too_large");return;}
    if(!checksum_.empty()&&!update_policy::checksum(checksum_)){fail("invalid_image");return;}
    if(psa_crypto_init()!=PSA_SUCCESS||psa_hash_setup(&hash_,PSA_ALG_SHA_256)!=PSA_SUCCESS)fail("no_memory");
}
FirmwareWriter::~FirmwareWriter(){
    if(started_)esp_ota_abort(handle_);
    psa_hash_abort(&hash_);
    if(owned_)busy_=false;
}
bool FirmwareWriter::write(const uint8_t *data,size_t size){
    if(!ready())return false;
    if(size>total_-received_)return fail("invalid_image");
    if(psa_hash_update(&hash_,data,size)!=PSA_SUCCESS)return fail("invalid_image");
    received_+=size;
    if(headerSize_<header_.size()){
        size_t copied=std::min(size,header_.size()-headerSize_);
        std::copy_n(data,copied,header_.data()+headerSize_);headerSize_+=copied;data+=copied;size-=copied;
        if(headerSize_<header_.size())return true;
        if(!update_policy::header(header_.data(),header_.size(),board::chipId,board::projectName,version_.c_str()))return fail("wrong_project_image");
        if(esp_ota_begin(partition_,total_,&handle_)!=ESP_OK)return fail("ota_busy");
        started_=true;
        if(esp_ota_write(handle_,header_.data(),header_.size())!=ESP_OK)return fail("flash_write_failed");
    }
    if(size&&esp_ota_write(handle_,data,size)!=ESP_OK)return fail("flash_write_failed");
    return true;
}
bool FirmwareWriter::finish(){
    if(!ready()||!started_||received_!=total_)return fail("upload_interrupted");
    uint8_t digest[32]{};size_t length=0;
    if(psa_hash_finish(&hash_,digest,sizeof(digest),&length)!=PSA_SUCCESS||length!=32)return fail("invalid_image");
    if(!checksum_.empty()){
        static constexpr char hex[]="0123456789abcdef";std::string actual;
        for(uint8_t byte:digest){actual+=hex[byte>>4];actual+=hex[byte&15];}
        if(actual!=checksum_)return fail("checksum_mismatch");
    }
    auto result=esp_ota_end(handle_);started_=false;
    if(result!=ESP_OK||esp_ota_set_boot_partition(partition_)!=ESP_OK)return fail("invalid_image");
    return true;
}
}
