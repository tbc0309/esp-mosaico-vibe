#include "print_sound.h"
#include "bsp/esp_mosaico.h"
#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
enum class Alert : uint32_t { Warning=1, Fault, Complete };
struct Segment { unsigned frequency, milliseconds; };
constexpr Segment warning[]={{880,90},{0,70},{880,90}};
constexpr Segment fault[]={{660,100},{0,70},{660,100},{0,70},{660,100}};
constexpr Segment complete[]={{1200,80},{0,50},{1600,100}};
constexpr unsigned rate=BSP_AUDIO_DEFAULT_SAMPLE_RATE, channels=BSP_AUDIO_DEFAULT_CHANNELS;
TaskHandle_t workerHandle=nullptr;

bool writeSegment(esp_codec_dev_handle_t speaker, Segment segment) {
    int16_t buffer[64*channels];
    const unsigned total=rate*segment.milliseconds/1000;
    const unsigned ramp=rate/200;
    const float step=6.283185307f*segment.frequency/rate;
    for(unsigned start=0;start<total;start+=64) {
        const unsigned count=std::min(64u,total-start);
        for(unsigned i=0;i<count;++i) {
            const unsigned at=start+i;
            const float envelope=float(std::min({at,total-1-at,ramp}))/ramp;
            const auto sample=static_cast<int16_t>(std::sin(step*at)*3000*envelope);
            for(unsigned channel=0;channel<channels;++channel)
                buffer[i*channels+channel]=sample;
        }
        if(esp_codec_dev_write(speaker,buffer,count*channels*sizeof(int16_t))!=ESP_CODEC_DEV_OK)
            return false;
    }
    return true;
}

template<size_t N>
bool play(esp_codec_dev_handle_t speaker,const Segment (&sequence)[N]) {
    for(const auto segment:sequence)
        if(!writeSegment(speaker,segment)) return false;
    return writeSegment(speaker,{0,40});
}

void worker(void *) {
    for(;;) {
        uint32_t value=0;
        xTaskNotifyWait(0,UINT32_MAX,&value,portMAX_DELAY);
        auto speaker=bsp_audio_codec_speaker_init();
        esp_codec_dev_sample_info_t format{};
        format.sample_rate=rate;
        format.bits_per_sample=16;
        format.channel=channels;
        if(!speaker||esp_codec_dev_open(speaker,&format)!=ESP_CODEC_DEV_OK) {
            ESP_LOGW("print-sound","Speaker unavailable");
            continue;
        }
        esp_codec_dev_set_out_vol(speaker,45);
        const auto alert=static_cast<Alert>(value);
        const bool ok=alert==Alert::Fault?play(speaker,fault):
                      alert==Alert::Complete?play(speaker,complete):play(speaker,warning);
        esp_codec_dev_close(speaker);
        if(!ok) ESP_LOGW("print-sound","Playback failed");
    }
}
}

void printSoundPoll(pixel_screen::State state) {
    using pixel_screen::State;
    static State previous=State::Offline;
    if(state==State::Offline||state==previous) return;
    const bool baseline=previous==State::Offline;
    previous=state;
    if(baseline) return;
    Alert alert;
    switch(state) {
    case State::Complete: alert=Alert::Complete;break;
    case State::Error: alert=Alert::Fault;break;
    case State::Paused:
    case State::Cancelled: alert=Alert::Warning;break;
    default:return;
    }
    if(!workerHandle&&xTaskCreate(worker,"print-sound",4096,nullptr,1,&workerHandle)!=pdPASS)
        return;
    xTaskNotify(workerHandle,static_cast<uint32_t>(alert),eSetValueWithOverwrite);
}
