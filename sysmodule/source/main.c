#include <switch.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INNER_HEAP_SIZE 0x8000
#define FAN_DEVICE_CODE_PWM 0x3D000001
#define CONFIG_PATH "sdmc:/atmosphere/config/system_settings.ini"
#define FAN_MAX_SEGMENTS 16
#define FAN_LOOP_SLEEP_NS 500000000LL
#define FAN_RELOAD_TICKS 4

typedef struct {
    s32 temp_min_milli_c;
    s32 temp_max_milli_c;
    s32 fan_min;
    s32 fan_max;
} FanSegment;

typedef struct {
    FanSegment segments[FAN_MAX_SEGMENTS];
    size_t count;
} FanTable;

typedef struct {
    FanTable handheld;
    FanTable console;
    u32 holdable_tskin_milli_c;
    u32 touchable_tskin_milli_c;
    bool enabled;
} FanConfig;

static bool g_omm_ready;
static bool g_tc_fan_control_disabled;
static bool g_tc_fan_control_was_enabled = true;

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void) {
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

static bool g_ts_ready;
static bool g_tc_ready;
static bool g_fan_ready;

void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    }

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        }
        setsysExit();
    }

    for (int i = 0; i < 100; i++) {
        rc = fsInitialize();
        if (R_SUCCEEDED(rc)) {
            break;
        }
        svcSleepThread(100000000LL);
    }
    if (R_FAILED(rc)) {
        svcSleepThread(5000000000LL);
        return;
    }

    for (int i = 0; i < 100; i++) {
        rc = fsdevMountSdmc();
        if (R_SUCCEEDED(rc)) {
            break;
        }
        svcSleepThread(100000000LL);
    }

    for (int i = 0; i < 50; i++) {
        rc = tsInitialize();
        if (R_SUCCEEDED(rc)) {
            g_ts_ready = true;
            break;
        }
        svcSleepThread(100000000LL);
    }

    for (int i = 0; i < 50; i++) {
        rc = tcInitialize();
        if (R_SUCCEEDED(rc)) {
            g_tc_ready = true;
            break;
        }
        svcSleepThread(100000000LL);
    }

    for (int i = 0; i < 50; i++) {
        rc = fanInitialize();
        if (R_SUCCEEDED(rc)) {
            g_fan_ready = true;
            break;
        }
        svcSleepThread(100000000LL);
    }

    g_omm_ready = R_SUCCEEDED(ommInitialize());
    smExit();
}

void __appExit(void) {
    if (g_tc_fan_control_disabled && g_tc_fan_control_was_enabled) {
        tcEnableFanControl();
        g_tc_fan_control_disabled = false;
    }

    if (g_omm_ready) {
        ommExit();
        g_omm_ready = false;
    }

    if (g_fan_ready) {
        fanExit();
        g_fan_ready = false;
    }

    if (g_tc_ready) {
        tcExit();
        g_tc_ready = false;
    }

    if (g_ts_ready) {
        tsExit();
        g_ts_ready = false;
    }

    fsdevUnmountAll();
    fsExit();
}

static char* trim(char* value) {
    while (*value && isspace((unsigned char)*value)) {
        value++;
    }

    char* end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    return value;
}

static const char* typed_value_payload(const char* value) {
    const char* bang = strchr(value, '!');
    if (bang) {
        value = bang + 1;
    }

    while (*value && isspace((unsigned char)*value)) {
        value++;
    }

    return value;
}

static bool parse_bool_enabled(const char* value) {
    value = typed_value_payload(value);
    return *value && strtoul(value, NULL, 0) != 0;
}

static bool parse_u32_value(const char* value, u32* out) {
    value = typed_value_payload(value);
    if (!*value) {
        return false;
    }

    *out = (u32)strtoul(value, NULL, 0);
    return true;
}

static s32 clamp_s32(s32 value, s32 min_value, s32 max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void add_segment(FanTable* table, s32 temp_min, s32 temp_max, s32 fan_min, s32 fan_max) {
    if (table->count >= FAN_MAX_SEGMENTS || temp_max < temp_min) {
        return;
    }

    FanSegment* segment = &table->segments[table->count++];
    segment->temp_min_milli_c = temp_min;
    segment->temp_max_milli_c = temp_max;
    segment->fan_min = clamp_s32(fan_min, 0, 255);
    segment->fan_max = clamp_s32(fan_max, 0, 255);
}

static bool parse_fan_table(const char* value, FanTable* out) {
    FanTable table = {0};
    s32 values[4] = {0};
    size_t value_count = 0;

    while (*value) {
        if (*value == '-' || isdigit((unsigned char)*value)) {
            char* end = NULL;
            const long parsed = strtol(value, &end, 10);
            if (end != value) {
                values[value_count++] = (s32)parsed;
                value = end;

                if (value_count == 4) {
                    add_segment(&table, values[0], values[1], values[2], values[3]);
                    value_count = 0;
                }
                continue;
            }
        }
        value++;
    }

    if (table.count == 0 || value_count != 0) {
        return false;
    }

    *out = table;
    return true;
}

#pragma pack(push, 1)
typedef struct {
    u32 magic;
    u32 version;
    s32 temp_milli_c;
    float fan_level;
    u64 timestamp_ns;
    u32 sysmodule_active;
} SphairaFanState;
#pragma pack(pop)

static bool ReadSocTemperatureMilliC(s32* out_temp_milli_c) {
    if (!out_temp_milli_c) return false;

    TsSession session;
    if (R_SUCCEEDED(tsOpenSession(&session, TsDeviceCode_LocationExternal))) {
        float temp_f = 0.0f;
        Result rc = tsSessionGetTemperature(&session, &temp_f);
        tsSessionClose(&session);
        if (R_SUCCEEDED(rc) && temp_f > 0.0f) {
            *out_temp_milli_c = (s32)(temp_f * 1000.0f);
            return true;
        }
    }

    if (R_SUCCEEDED(tsOpenSession(&session, TsDeviceCode_LocationInternal))) {
        float temp_f = 0.0f;
        Result rc = tsSessionGetTemperature(&session, &temp_f);
        tsSessionClose(&session);
        if (R_SUCCEEDED(rc) && temp_f > 0.0f) {
            *out_temp_milli_c = (s32)(temp_f * 1000.0f);
            return true;
        }
    }

    s32 temp_c = 0;
    if (R_SUCCEEDED(tsGetTemperature(TsLocation_External, &temp_c)) && temp_c > 0) {
        *out_temp_milli_c = temp_c * 1000;
        return true;
    }

    if (R_SUCCEEDED(tsGetTemperature(TsLocation_Internal, &temp_c)) && temp_c > 0) {
        *out_temp_milli_c = temp_c * 1000;
        return true;
    }

    s32 temp_milli = 0;
    if (R_SUCCEEDED(tsGetTemperatureMilliC(TsLocation_External, &temp_milli)) && temp_milli > 0) {
        *out_temp_milli_c = temp_milli;
        return true;
    }

    if (R_SUCCEEDED(tcGetSkinTemperatureMilliC(&temp_milli)) && temp_milli > 0) {
        *out_temp_milli_c = temp_milli;
        return true;
    }

    return false;
}

static void set_default_tables(FanConfig* config) {
    memset(config, 0, sizeof(*config));
    config->enabled = true;

    add_segment(&config->handheld, -1000000, 10000, 0, 0);
    add_segment(&config->handheld, 10000, 40000, 0, 51);
    add_segment(&config->handheld, 40000, 47000, 51, 51);
    add_segment(&config->handheld, 47000, 56000, 51, 153);
    add_segment(&config->handheld, 56000, 58000, 153, 255);
    add_segment(&config->handheld, 58000, 1000000, 255, 255);

    add_segment(&config->console, -1000000, 10000, 0, 0);
    add_segment(&config->console, 10000, 40000, 0, 51);
    add_segment(&config->console, 40000, 47000, 51, 51);
    add_segment(&config->console, 47000, 54000, 51, 153);
    add_segment(&config->console, 54000, 58000, 153, 255);
    add_segment(&config->console, 58000, 1000000, 255, 255);

    config->holdable_tskin_milli_c = 0xF230;
    config->touchable_tskin_milli_c = 0xFDE8;
}

static bool load_config(FanConfig* out) {
    FanConfig config;
    set_default_tables(&config);

    FILE* fp = fopen(CONFIG_PATH, "r");
    if (!fp) {
        return false;
    }

    char line[1024];
    bool in_tc = false;
    bool handheld_fwdbg_seen = false;
    bool console_fwdbg_seen = false;

    while (fgets(line, sizeof(line), fp)) {
        char* text = trim(line);
        if (!*text || *text == ';' || *text == '#') {
            continue;
        }

        if (*text == '[') {
            char* end = strchr(text, ']');
            if (!end) {
                in_tc = false;
                continue;
            }
            *end = '\0';
            in_tc = strcmp(text + 1, "tc") == 0;
            continue;
        }

        if (!in_tc) {
            continue;
        }

        char* equals = strchr(text, '=');
        if (!equals) {
            continue;
        }

        *equals = '\0';
        const char* key = trim(text);
        const char* value = trim(equals + 1);

        if (strcmp(key, "use_configurations_on_fwdbg") == 0) {
            config.enabled = parse_bool_enabled(value);
        } else if (strcmp(key, "tskin_rate_table_handheld_on_fwdbg") == 0) {
            FanTable parsed;
            if (parse_fan_table(value, &parsed)) {
                config.handheld = parsed;
                handheld_fwdbg_seen = true;
            }
        } else if (strcmp(key, "tskin_rate_table_handheld") == 0 && !handheld_fwdbg_seen) {
            FanTable parsed;
            if (parse_fan_table(value, &parsed)) {
                config.handheld = parsed;
            }
        } else if (strcmp(key, "tskin_rate_table_console_on_fwdbg") == 0) {
            FanTable parsed;
            if (parse_fan_table(value, &parsed)) {
                config.console = parsed;
                console_fwdbg_seen = true;
            }
        } else if (strcmp(key, "tskin_rate_table_console") == 0 && !console_fwdbg_seen) {
            FanTable parsed;
            if (parse_fan_table(value, &parsed)) {
                config.console = parsed;
            }
        } else if (strcmp(key, "holdable_tskin") == 0) {
            parse_u32_value(value, &config.holdable_tskin_milli_c);
        } else if (strcmp(key, "touchable_tskin") == 0) {
            parse_u32_value(value, &config.touchable_tskin_milli_c);
        }
    }

    fclose(fp);
    *out = config;
    return true;
}

static bool is_console_mode(void) {
    OmmOperationMode mode;
    if (g_omm_ready && R_SUCCEEDED(ommGetOperationMode(&mode))) {
        return mode == OmmOperationMode_Console;
    }

    return false;
}

static s32 evaluate_table(const FanTable* table, s32 temp_milli_c) {
    if (table->count == 0) {
        return 255;
    }

    for (size_t i = 0; i < table->count; i++) {
        const FanSegment* segment = &table->segments[i];
        if (temp_milli_c <= segment->temp_max_milli_c) {
            if (temp_milli_c <= segment->temp_min_milli_c ||
                segment->temp_max_milli_c == segment->temp_min_milli_c) {
                return segment->fan_min;
            }

            const s64 temp_span = (s64)segment->temp_max_milli_c - segment->temp_min_milli_c;
            const s64 fan_span = (s64)segment->fan_max - segment->fan_min;
            const s64 temp_offset = (s64)temp_milli_c - segment->temp_min_milli_c;
            return clamp_s32(segment->fan_min + (s32)((fan_span * temp_offset) / temp_span), 0, 255);
        }
    }

    return table->segments[table->count - 1].fan_max;
}

static float calculate_fan_level(const FanConfig* config, s32 temp_milli_c, bool console_mode) {
    const u32 safety_limit = console_mode ? config->touchable_tskin_milli_c : config->holdable_tskin_milli_c;
    if (temp_milli_c >= (s32)safety_limit) {
        return 1.0f;
    }

    const FanTable* table = console_mode ? &config->console : &config->handheld;
    return (float)evaluate_table(table, temp_milli_c) / 255.0f;
}

int main(int argc, char* argv[]) {
    FanConfig config;
    load_config(&config);
    if (!config.enabled) {
        return 0;
    }

    FanController controller;
    if (R_FAILED(fanOpenController(&controller, FAN_DEVICE_CODE_PWM))) {
        return 1;
    }

    if (g_tc_ready) {
        bool current_tc_fan_control_enabled = true;
        if (R_SUCCEEDED(tcIsFanControlEnabled(&current_tc_fan_control_enabled))) {
            g_tc_fan_control_was_enabled = current_tc_fan_control_enabled;
        }
        g_tc_fan_control_disabled = R_SUCCEEDED(tcDisableFanControl());
    }

    u32 reload_tick = 0;
    while (true) {
        if (++reload_tick >= FAN_RELOAD_TICKS) {
            FanConfig next_config;
            if (load_config(&next_config)) {
                if (!next_config.enabled) {
                    break;
                }
                config = next_config;
            }
            reload_tick = 0;
        }

        s32 temp_milli_c = 0;
        bool temp_ok = ReadSocTemperatureMilliC(&temp_milli_c);

        float fan_level = 1.0f;
        if (temp_ok) {
            fan_level = calculate_fan_level(&config, temp_milli_c, is_console_mode());
        }

        fanControllerSetRotationSpeedLevel(&controller, fan_level);

        float actual_level = fan_level;
        fanControllerGetRotationSpeedLevel(&controller, &actual_level);

        SphairaFanState state = {0};
        state.magic = 0x46414E53;
        state.version = 1;
        state.temp_milli_c = temp_milli_c;
        state.fan_level = actual_level;
        state.timestamp_ns = armTicksToNs(armGetSystemTick());
        state.sysmodule_active = 1;

        FILE* fp_state = fopen("sdmc:/switch/sphaira/fan_status.bin", "wb");
        if (fp_state) {
            fwrite(&state, sizeof(state), 1, fp_state);
            fclose(fp_state);
        }

        svcSleepThread(FAN_LOOP_SLEEP_NS);
    }

    SphairaFanState state = {0};
    state.magic = 0x46414E53;
    state.sysmodule_active = 0;
    FILE* fp_state = fopen("sdmc:/switch/sphaira/fan_status.bin", "wb");
    if (fp_state) {
        fwrite(&state, sizeof(state), 1, fp_state);
        fclose(fp_state);
    }

    fanControllerClose(&controller);
    return 0;
}
