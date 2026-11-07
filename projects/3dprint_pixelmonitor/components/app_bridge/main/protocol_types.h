#pragma once
#include <stdbool.h>
#include <stdint.h>

// Printer-report data only: no touchscreen, movement or legacy UI configuration.
#define XTOUCH_STANDARD_AMS_UNITS 4
#define XTOUCH_STANDARD_AMS_SLOTS 16
#define XTOUCH_HT_AMS_UNITS 8
#define XTOUCH_MIXED_AMS_UNIT 12
#define XTOUCH_MAX_AMS_UNITS 13
#define XTOUCH_AMS_SLOTS_PER_UNIT 4
#define XTOUCH_MAX_AMS_SLOTS 52

typedef struct BambuTelemetry {
    int mc_left_time,mc_print_percent,current_layer,total_layers;
    bool current_layer_valid;
    double chamber_temper,chamber_target_temper;
    bool chamber_valid,chamber_target_valid,chamber_edit_known,chamber_edit_supported;
    int print_stage_current,mc_print_stage,mc_print_sub_stage,mc_print_line_number,gcode_file_prepare_percent;
    char task_id[32],obj_subtask_id[32],project_id_[32],profile_id_[32],subtask_id_[32],subtask_name[32],gcode_file[128];
    int plate_index;
    bool plate_index_valid;
    bool ams,is_ams_need_update;
    int ams_status_main,ams_status_sub,ams_humidity;
    bool ams_status_valid, ams_current_valid, ams_target_valid;
    long ams_version,ams_exist_bits;
    uint64_t tray_exist_bits,tray_is_bbl_bits,tray_read_done_bits,tray_reading_bits,ams_remain_valid;
    int m_tray_now,m_tray_tar;
    uint8_t mixed_ams_protocol_id;
    uint32_t ams_tray_color[XTOUCH_MAX_AMS_SLOTS];
    bool ams_tray_color_valid[XTOUCH_MAX_AMS_SLOTS];
    char ams_tray_material[XTOUCH_MAX_AMS_SLOTS][12];
    uint8_t ams_remain_percent[XTOUCH_MAX_AMS_SLOTS];
    uint16_t ams_nozzle_min[XTOUCH_MAX_AMS_SLOTS],ams_nozzle_max[XTOUCH_MAX_AMS_SLOTS];
    uint8_t ams_kind[XTOUCH_MAX_AMS_UNITS],ams_module_kind[XTOUCH_MAX_AMS_UNITS],ams_bound_extruder[XTOUCH_MAX_AMS_UNITS];
    uint16_t ams_humidity_valid,ams_temperature_valid,ams_dry_valid;
    uint8_t ams_humidity_percent[XTOUCH_MAX_AMS_UNITS];
    int16_t ams_temperature_deci[XTOUCH_MAX_AMS_UNITS];
    uint8_t ams_dry_status[XTOUCH_MAX_AMS_UNITS],ams_dry_target[XTOUCH_MAX_AMS_UNITS],ams_dry_hours[XTOUCH_MAX_AMS_UNITS];
    uint16_t ams_dry_minutes[XTOUCH_MAX_AMS_UNITS];
    bool external_present,external_multi_protocol;
    uint8_t external_color_valid,external_materials;
    uint32_t external_color,external_color2;
    uint8_t nozzle_count,active_nozzle,nozzle_loaded_valid;
    int16_t nozzle_loaded[2];
} BambuTelemetry;

#ifdef __cplusplus
extern "C" {
#endif
extern BambuTelemetry bambuStatus;
#ifdef __cplusplus
}
#endif
