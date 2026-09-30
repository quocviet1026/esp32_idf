/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* main/main.c
 *
 * Boot sequencer - doc app_config tu NVS, chon DUNG 1 implementation cua
 * transport_if_t (xem transport.h) theo cfg.mode, roi giao toan bo phan con
 * lai cho no. main.c KHONG con biet gi ve MQTT hay AT-command LoRaWAN nua -
 * chi goi qua transport->start()/publish()/is_ready(), dung tinh than
 * "transport abstraction" (interface = struct con tro ham, chon 1 lan luc
 * boot dua theo cau hinh NVS, khong dung DIP switch vat ly).
 *
 * main.c la noi DUY NHAT quyet dinh "gui loai ban tin gi, bao lau 1 lan" -
 * xem s_schedule_wifi_mqtt[]/s_schedule_lorawan[] ben duoi. transport chi la
 * CO CHE gui (khong biet gi ve lich trinh), giong cach coreMQTT/Azure IoT SDK
 * khong he co khai niem "interval" trong ham publish/send cua ho - ung dung
 * tu goi khi nao tuy y.
 *
 * cli_console_start() LUON chay DAU TIEN, bat ke mode - day la duong phuc
 * hoi duy nhat neu cau hinh da luu bi sai ma khong can reflash lai qua USB.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "driver/uart.h"

#include "app_config.h"
#include "cli_console.h"
#include "sensor_sim.h"
#include "transport.h"
#include "ota_manager.h"

static const char *TAG = "main";

static const transport_if_t *s_transport;

/* Chi dung khi cfg.sleep_enabled == true - xem giai thich day du o
 * app_main()/on_transport_ready() ben duoi. Tao TRUOC transport->start(),
 * "give" tu on_transport_ready() (chay trong context task cua esp-mqtt hoac
 * rak3172_rx_task, khong phai ISR - xSemaphoreGive() thuong la du, khong can
 * ban FromISR). */
static SemaphoreHandle_t s_ready_sem;

/* Ngan sach thoi gian toi da duoc THUC (tinh tu transport->start()) truoc khi
 * bo qua chu ky publish nay va quay lai ngu - PHAI ngan hon
 * ROLLBACK_CONFIRM_TIMEOUT_MS (60s, transport_wifi_mqtt.c) de nhanh "khong ngu
 * neu con PENDING_VERIFY" o app_main() luon thang truoc esp_timer watchdog do,
 * tranh 2 timeout dua nhau (rac ket qua tuy thuoc jitter). LoRaWAN can ngan
 * sach dai hon vi AT+JOIN=1:0:10:8 co the mat toi ~80s worst-case (10s x 8 lan
 * retry) neu vai lan thu dau that bai. */
#define AWAKE_TIMEOUT_WIFI_MQTT_MS 45000
#define AWAKE_TIMEOUT_LORAWAN_MS   90000
#define DOWNLINK_GRACE_MS          4000
#define SLEEP_NOTICE_FLUSH_MS      1000   /* cho ban tin APP_MSG_GOING_TO_SLEEP kip gui qua mang truoc khi mat nguon */

/* Duyet toan bo partition table (doc truc tiep tu flash, khong phu thuoc NVS
 * hay WiFi/MQTT) va log ra tung partition - huu ich de xac nhan ngay luc boot
 * layout thuc te tren board co dung nhu ky vong khong (vd co du 2 slot OTA
 * ota_0/ota_1 sau khi thiet ke partitions.csv chua), va biet dang chay tu
 * partition nao (danh dau "<-- RUNNING"). */
static void log_partition_table(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();

    ESP_LOGI(TAG, "Partition table:");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        const char *type_str = (p->type == ESP_PARTITION_TYPE_APP) ? "app " : "data";
        bool is_running = (running != NULL) && (p->address == running->address);
        ESP_LOGI(TAG, "  %-10s | type=%s subtype=0x%02x | addr=0x%08" PRIx32 " size=%" PRIu32 " KB%s",
                 p->label, type_str, p->subtype, p->address, p->size / 1024,
                 is_running ? "  <-- RUNNING" : "");
    }
    esp_partition_iterator_release(it);
}

/* ===================== "Thu thap du lieu" - nghiep vu, khong lien quan transport ===================== */

typedef app_message_t (*msg_collector_t)(void);

static app_message_t collect_sensor(void)
{
    app_message_t msg = { .type = APP_MSG_SENSOR };
    msg.data.sensor_value = sensor_sim_read();
    return msg;
}

static app_message_t collect_keepalive(void)
{
    app_message_t msg = { .type = APP_MSG_KEEPALIVE };
    return msg;
}

static msg_collector_t collector_for(app_msg_type_t type)
{
    switch (type) {
    case APP_MSG_SENSOR:    return collect_sensor;
    case APP_MSG_KEEPALIVE: return collect_keepalive;
    default:                return NULL;
    }
}

/* ===================== Lich gui - main.c tu quyet dinh, KHONG hoi transport ===================== */
/*
 * Con so interval o day la RANG BUOC VAT LY that (duty-cycle LoRaWAN) can
 * xac nhan lai theo bang tan/Data Rate that dang dung - gia tri hien tai chi
 * la diem khoi dau an toan, KHONG phai so da kiem chung RF.
 *
 * Them 1 loai ban tin moi: them 1 dong app_msg_type_t vao ca 2 bang duoi day
 * (hoac chi 1 bang, neu ban tin do chi ap dung cho 1 transport - giong cach
 * OTA chi wire trong transport_wifi_mqtt.c, KHONG can co "entry" gi trong
 * bang lich cua LoRaWAN).
 */
typedef struct {
    app_msg_type_t type;
    uint32_t interval_ms;
} message_schedule_t;

static const message_schedule_t s_schedule_wifi_mqtt[] = {
    { APP_MSG_SENSOR,    30000 },   /* 30s - khong gioi han bang thong dang ke qua MQTT/WiFi */
    { APP_MSG_KEEPALIVE, 10000 },   /* 10s - theo yeu cau ban dau */
};

static const message_schedule_t s_schedule_lorawan[] = {
    { APP_MSG_SENSOR,    60000 },    /* 60s - CAN xac nhan lai theo duty-cycle AS923 that */
    { APP_MSG_KEEPALIVE, 300000 },   /* 5 phut - CAN xac nhan lai theo duty-cycle AS923 that, KHONG duoc dung 10s nhu ben MQTT */
};

typedef struct {
    msg_collector_t collector;
    uint32_t interval_ms;
} publish_task_arg_t;

/* 1 task rieng cho MOI loai ban tin trong bang lich - vong lap don gian:
 * thu thap -> goi transport->publish() -> ngu dung interval cua LOAI BAN TIN
 * DO (khong phai 1 interval chung cho ca he thong). arg duoc cap phat bang
 * malloc() trong on_transport_ready() va KHONG bao gio free() - dung y, vi
 * task nay chay mai mai cho toi khi thiet bi reset. */
static void publish_task(void *arg)
{
    publish_task_arg_t *a = (publish_task_arg_t *)arg;
    while (1) {
        app_message_t msg = a->collector();
        esp_err_t err = s_transport->publish(&msg);
        ESP_LOGI(TAG, "Publish type=%d -> %s", msg.type, err == ESP_OK ? "OK" : esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(a->interval_ms));
    }
}

/* ===================== Xu ly ban tin TU SERVER XUONG ===================== */
/*
 * Goi moi lan transport (bat ke WiFi/MQTT hay LoRaWAN) decode duoc 1 lenh -
 * day la DIEM DUY NHAT trong toan bo firmware xu ly downlink, doi xung voi
 * publish_task() o chieu len. Hien CHUA co nghiep vu cu the (chi co
 * APP_CMD_UNKNOWN) - them lenh that: them case moi trong switch nay, khop
 * voi app_cmd_type_t da them trong transport.h.
 */
static void on_downlink_command(const app_command_t *cmd)
{
    switch (cmd->type) {
    case APP_CMD_UNKNOWN:
    default:
        ESP_LOGI(TAG, "Nhan lenh tu server (chua co nghiep vu xu ly rieng), %u byte: %.*s",
                 (unsigned)cmd->data.raw.len, (int)cmd->data.raw.len, (const char *)cmd->data.raw.payload);
        break;
    }
}

/* Goi DUNG 1 LAN MOI LAN BOOT, ngay khi transport lan dau tien san sang (MQTT
 * CONNECTED lan dau, hoac LoRaWAN JOINED lan dau) - dam bao boi transport.c,
 * xem transport.h. (Vi Deep Sleep = reset hoan toan, "1 lan moi boot" o day
 * cung chinh la "1 lan moi chu ky thuc" khi sleep_enabled=true - khong can co
 * che RTC memory gi them.)
 *
 * sleep_enabled=false (mac dinh, hanh vi hien tai KHONG doi): tao 1
 * task/dong trong bang lich tuong ung voi mode dang chay, chay vinh vien.
 *
 * sleep_enabled=true: chi "give" semaphore de app_main() (dang cho o
 * xSemaphoreTake) tiep tuc chay publish 1 lan roi tu quyet dinh Deep Sleep -
 * xem app_main(). */
static void on_transport_ready(void)
{
    app_config_t cfg;
    app_config_load(&cfg);   /* doc lai de biet dang o mode nao - an toan, chi doc RAM cache cua NVS driver */

    if (cfg.sleep_enabled) {
        /* KHONG publish gi o day! Ham nay dang chay trong context cua task
         * NOI BO esp-mqtt (MQTT_EVENT_CONNECTED) hoac rak3172_rx_task - KHONG
         * duoc block lau (vd cho publish() hoan tat, hay vTaskDelay() cho
         * downlink) vi se nghen viec xu ly su kien/doc UART tiep theo cua
         * chinh transport do.
         *
         * Chi "bao chuong" bang xSemaphoreGive() roi return NGAY - viec publish
         * sensor+keepalive that su duoc app_main() (dang block o
         * xSemaphoreTake(s_ready_sem, ...)) tu lam SAU KHI nhan duoc tin hieu
         * nay, trong context cua task "main" rieng - task nay ranh, duoc phep
         * block/vTaskDelay/tu quyet dinh Deep Sleep thoai mai. Xem doan code
         * ngay sau xSemaphoreTake() trong app_main(). */
        xSemaphoreGive(s_ready_sem);
        return;
    }

    const message_schedule_t *schedule = (cfg.mode == APP_MODE_WIFI_MQTT) ? s_schedule_wifi_mqtt : s_schedule_lorawan;
    size_t count = (cfg.mode == APP_MODE_WIFI_MQTT)
                       ? sizeof(s_schedule_wifi_mqtt) / sizeof(s_schedule_wifi_mqtt[0])
                       : sizeof(s_schedule_lorawan) / sizeof(s_schedule_lorawan[0]);

    for (size_t i = 0; i < count; i++) {
        publish_task_arg_t *arg = malloc(sizeof(publish_task_arg_t));
        arg->collector = collector_for(schedule[i].type);
        arg->interval_ms = schedule[i].interval_ms;

        char task_name[16];
        snprintf(task_name, sizeof(task_name), "pub_%d", schedule[i].type);
        xTaskCreate(publish_task, task_name, 4096, arg, 5, NULL);
    }
}

/* Chi goi khi cfg.sleep_enabled == true, sau khi da het ngan sach thuc (dong
 * bo publish xong hoac timeout). Chan 2 truong hop KHAC NHAU, CA HAI deu chi
 * ap dung mode WIFI_MQTT (LoRaWAN khong co OTA/rollback):
 *
 *  1. Dang co qua trinh OTA THAT SU dien ra (da nhan lenh MQTT, ota_task dang
 *     tai/ghi firmware moi vao slot chua active) - kiem tra qua
 *     ota_manager_is_busy(). Deep Sleep = esp_deep_sleep_start() cat nguon
 *     DOT NGOT (khac esp_restart(), khong co buoc don dep) - neu roi vao
 *     dung luc dang ghi flash, co the de lai anh firmware trong slot moi bi
 *     "torn" (ghi do dang), hoac te hon la lam gian doan dung luc
 *     esp_ota_set_boot_partition() dang cap nhat otadata. TUYET DOI khong
 *     duoc ngu trong luc nay.
 *
 *  2. Da OTA xong, dang cho xac nhan (PENDING_VERIFY) - xem "Phat hien quan
 *     trong nhat" trong plan sleep mode: bootloader TU DONG danh dau ABORTED
 *     (=hong) bat ky slot OTA nao con PENDING_VERIFY ngay dau MOI lan boot, ke
 *     ca boot do Deep Sleep timer wake (Deep Sleep = reset hoan toan, di qua
 *     dung duong boot nhu cold boot, KHONG co "resume" nhu Light Sleep). Vi
 *     vay: Deep Sleep trong luc con PENDING_VERIFY = CHAC CHAN bi rollback
 *     oan o lan thuc ke tiep - khong phai rui ro "co the", ma la hanh vi tat
 *     dinh cua bootloader. */
static bool is_safe_to_sleep(app_mode_t mode)
{
    if (mode != APP_MODE_WIFI_MQTT) {
        return true;
    }

    if (ota_manager_is_busy()) {
        return false;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        /* Khong doc duoc state (hiem) - an toan hon la KHONG ngu. */
        return false;
    }
    return state != ESP_OTA_IMG_PENDING_VERIFY;
}

void app_main(void)
{
    log_partition_table();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s), reinitializing...", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* LUON DAU TIEN, bat ke mode - duong phuc hoi duy nhat neu cau hinh sai. */
    cli_console_start();

    app_config_t cfg;
    esp_err_t cfg_err = app_config_load(&cfg);
    if (cfg_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Chua co config da luu, dung mac dinh (mode=WIFI_MQTT) va luu lai vao NVS");
        app_config_save(&cfg);
    }
    cli_console_set_running_mode(cfg.mode);

    ESP_LOGI(TAG, "Boot mode = %s", cfg.mode == APP_MODE_WIFI_MQTT ? "WIFI_MQTT" : "LORAWAN");

    /* Chon DUNG 1 LAN implementation cua transport - khong bao gio doi lai
     * trong cung 1 phien chay (doi mode qua CLI 'mode_set' can reboot). */
    s_transport = transport_get(cfg.mode);
    transport_set_ready_callback(on_transport_ready);
    transport_set_downlink_callback(on_downlink_command);

    if (cfg.sleep_enabled) {
        s_ready_sem = xSemaphoreCreateBinary();   /* PHAI tao TRUOC start(), tranh race voi on_transport_ready() */
    }

    esp_err_t err = s_transport->start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "transport->start() that bai: %s", esp_err_to_name(err));
    }

    if (!cfg.sleep_enabled) {
        /* Hanh vi hien tai, KHONG doi: app_main() ket thuc o day, phan con lai
         * hoan toan chay theo event (WiFi/IP/MQTT hoac rak3172 join/downlink
         * callback) hoac theo cac publish_task() dinh ky. */
        return;
    }

    /* ===================== Nhanh Deep Sleep (sleep_enabled == true) ===================== */
    uint32_t awake_timeout_ms = (cfg.mode == APP_MODE_WIFI_MQTT) ? AWAKE_TIMEOUT_WIFI_MQTT_MS : AWAKE_TIMEOUT_LORAWAN_MS;
    bool ready = xSemaphoreTake(s_ready_sem, pdMS_TO_TICKS(awake_timeout_ms)) == pdTRUE;

    if (ready) {
        app_message_t sensor_msg = collect_sensor();
        esp_err_t perr = s_transport->publish(&sensor_msg);
        ESP_LOGI(TAG, "Publish type=%d -> %s", sensor_msg.type, perr == ESP_OK ? "OK" : esp_err_to_name(perr));

        app_message_t keepalive_msg = collect_keepalive();
        perr = s_transport->publish(&keepalive_msg);
        ESP_LOGI(TAG, "Publish type=%d -> %s", keepalive_msg.type, perr == ESP_OK ? "OK" : esp_err_to_name(perr));

        /* Cho ngan de nhan downlink/lenh MQTT retained/RX LoRaWAN truoc khi
         * ngu lai - dung mo hinh "pull" da chot: downlink chi can nhan duoc
         * luc thuc, khong can real-time luc dang ngu. */
        vTaskDelay(pdMS_TO_TICKS(DOWNLINK_GRACE_MS));
    } else {
        ESP_LOGW(TAG, "Wake cycle timeout (%" PRIu32 " ms) - bo qua publish chu ky nay", awake_timeout_ms);
    }

    if (!ready || !is_safe_to_sleep(cfg.mode)) {
        /* KHONG Deep Sleep chu ky nay - de app_main() return binh thuong nhu
         * hanh vi sleep_enabled=false (esp-mqtt tu reconnect nen; neu con
         * PENDING_VERIFY, esp_timer watchdog cua ota_rollback.c van dang dem
         * va se tu rollback+reboot neu thuc su bi treo). */
        ESP_LOGW(TAG, "Bo qua Deep Sleep chu ky nay (ready=%d, safe_to_sleep=%d)", ready, is_safe_to_sleep(cfg.mode));
        return;
    }

    /* Bao truoc cho server biet SAP vao Deep Sleep - CHI o mode WIFI_MQTT (JSON,
     * xem APP_MSG_GOING_TO_SLEEP trong transport.h). LoRaWAN khong co ban tin
     * tuong duong (giu payload nho, dung tinh than thiet ke chung). Publish
     * NGAY TRUOC khi ngu that su (sau khi da qua het cac kiem tra an toan o
     * tren) - dung "publish() da an toan de goi ngay ca khi khong ai lay ket
     * qua" (khong block lau, tra ve nhanh du OK hay loi). */
    if (cfg.mode == APP_MODE_WIFI_MQTT) {
        app_message_t sleep_msg = { .type = APP_MSG_GOING_TO_SLEEP, .data.sleep_interval_ms = cfg.sleep_interval_ms };
        esp_err_t perr = s_transport->publish(&sleep_msg);
        ESP_LOGI(TAG, "Publish type=%d (going_to_sleep) -> %s", sleep_msg.type, perr == ESP_OK ? "OK" : esp_err_to_name(perr));
        /* esp_mqtt_client_publish() chi enqueue vao outbox noi bo, KHONG doi
         * gui xong qua day mang - can 1 khoang cho ngan de MQTT task noi bo
         * kip gui truoc khi mat nguon dot ngot, neu khong ban tin nay de bi
         * mat vo nghia (dung ngay luc sap ngu, khong con chu ky nao khac de
         * gui bu). */
        vTaskDelay(pdMS_TO_TICKS(SLEEP_NOTICE_FLUSH_MS));
    }

    ESP_LOGI(TAG, "Chuan bi Deep Sleep %" PRIu32 " ms...", cfg.sleep_interval_ms);
    fflush(stdout);
    uart_wait_tx_done(CONFIG_ESP_CONSOLE_UART_NUM, pdMS_TO_TICKS(100));   /* dam bao log tren kip in het truoc khi mat nguon dot ngot */

    esp_sleep_enable_timer_wakeup((uint64_t)cfg.sleep_interval_ms * 1000ULL);
    esp_deep_sleep_start();   /* noreturn */
}
