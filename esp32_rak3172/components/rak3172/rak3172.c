/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* rak3172.c
 *
 * Driver AT-command cho module LoRa RAK3172 (firmware RUI3) qua UART. Giao
 * thuc la text/dong-lenh BAT DONG BO: 1 task rieng (rak3172_rx_task) doc UART
 * lien tuc, tach theo "\r\n", roi phan loai MOI dong hoan chinh thanh 1 trong
 * 2 loai:
 *   - Bat dau bang "+EVT:"  -> su kien KHONG LIEN QUAN gi den lenh dang cho
 *     (join xong, co downlink...) - xu ly ngay lap tuc qua callback, doc lap
 *     hoan toan voi viec co dang co 1 lenh "in flight" hay khong.
 *   - Con lai -> 1 phan cua PHAN HOI DONG BO cho lenh AT vua gui - gom lai
 *     toi khi gap dong ket thuc (OK/AT_ERROR/...) roi day vao s_resp_queue de
 *     danh thuc rak3172_send_cmd() dang cho.
 *
 * Chi 1 lenh duoc phep "in flight" tai 1 thoi diem (dung ban chat AT protocol
 * khong pipeline duoc) - dam bao bang s_cmd_lock (mutex).
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "rak3172.h"

static const char *TAG = "rak3172";

#define RAK3172_LINE_MAXLEN   256
#define RAK3172_RESP_MAXLEN   256
#define RAK3172_UART_RX_BUF   1024

/* Ket qua cua 1 lenh AT sau khi da hoan tat (dong ket thuc OK/AT_ERROR/...
 * da toi) - day la "don vi du lieu" duy nhat luu chuyen qua s_resp_queue,
 * tu RX task (nguoi ghi) sang rak3172_send_cmd() (nguoi doc dang cho). */
typedef struct {
    bool ok;                              /* true neu dong ket thuc la "OK", false neu la 1 trong cac ma loi (AT_ERROR/...) */
    char payload[RAK3172_RESP_MAXLEN];    /* noi dung cac dong TRUOC dong ket thuc (vd noi dung tra ve cua AT+VER=?) - co the rong neu lenh chi tra ve OK/loi ma khong co du lieu kem theo */
} rak3172_resp_t;

/* ===================== Bien trang thai toan cuc ===================== */
/* Tat ca bien duoi day duoc GHI boi rak3172_rx_task (1 task duy nhat) va DOC
 * boi cac ham public (rak3172_send_cmd, rak3172_get_join_state...) co the
 * chay tu bat ky task nao khac (CLI, main, sensor task). Khong dung mutex rieng
 * cho tung bien - s_cmd_lock chi bao ve tinh "1 lenh tai 1 thoi diem", con cac
 * co/gia tri trang thai (s_join_state, s_last_downlink) la kieu doc-nhieu-ghi-it,
 * chap nhan duoc voi kieu du lieu don gian (bool/enum/struct nho) tren kien
 * truc RISC-V/Xtensa (doc/ghi 1 tu may la nguyen tu tu nhien). */

static uart_port_t        s_uart;               /* UART port dang dung de noi voi RAK3172 - gan 1 lan trong rak3172_init() */
static bool                s_initialized;        /* false truoc khi goi rak3172_init() - moi ham public khac deu tu kiem tra co nay truoc */
static QueueHandle_t       s_resp_queue;         /* queue 1 phan tu (xem rak3172_init) - noi RX task "chuyen giao" ket qua lenh vua hoan tat */
static SemaphoreHandle_t   s_cmd_lock;           /* mutex dam bao chi 1 rak3172_send_cmd() dang chay tai 1 thoi diem */
static volatile bool       s_awaiting_response;  /* true trong luc rak3172_send_cmd() dang cho phan hoi - RX task doc co nay de biet 1 dong ket thuc vua nhan co "ai do dang cho" hay khong (xem handle_line) */

static rak3172_downlink_cb_t   s_downlink_cb;    /* callback nguoi dung dang ky qua rak3172_set_downlink_callback(), goi tu RX task moi khi co downlink */
static rak3172_join_cb_t       s_join_cb;        /* callback nguoi dung dang ky qua rak3172_set_join_callback(), goi tu RX task khi join xong/that bai */
static volatile rak3172_join_state_t s_join_state = RAK3172_JOIN_IDLE;
static rak3172_downlink_info_t s_last_downlink = { .valid = false };   /* cache downlink gan nhat, phuc vu lenh CLI 'lorawan_status' */

/* ===================== Tien ich hex - RAK3172 dung payload dang ASCII hex ===================== */
/*
 * AT+SEND/+EVT:RX_ trao doi payload nhi phan duoi dang chuoi HEX ASCII (vd
 * byte 0xAB -> chuoi "AB"), KHONG phai binary tho - vi day la giao thuc AT
 * dang TEXT (xem comment dau file). 2 ham duoi day chuyen doi 2 chieu giua
 * mang byte thuc su (dung trong C) va chuoi hex (dung tren day UART).
 */

/* Ma hoa in_len byte thanh chuoi hex 2*in_len ky tu (luon in hoa) + 1 byte
 * NUL ket thuc - buffer out PHAI co it nhat 2*in_len+1 byte, khong tu kiem
 * tra kich thuoc (trach nhiem cua caller, ca 2 noi goi ham nay trong file
 * deu tu tinh dung kich thuoc buffer truoc). */
static void hex_encode(const uint8_t *in, size_t in_len, char *out /* >= 2*in_len+1 */)
{
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < in_len; i++) {
        /* Moi byte tach thanh 2 nibble (4-bit): nibble cao (>>4) truoc, nibble
         * thap (&0xF) sau - dung thu tu "big-endian ve hien thi" chuan cua hex. */
        out[i * 2]     = digits[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = digits[in[i] & 0xF];
    }
    out[in_len * 2] = '\0';
}

/* Doi 1 ky tu hex ('0'-'9', 'A'-'F', 'a'-'f') thanh gia tri 0-15, hoac -1 neu
 * khong phai ky tu hex hop le. RAK3172 co the tra ve hex hoa hoac thuong tuy
 * lenh, nen ham nay chap nhan ca 2. */
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Giai ma nguoc lai hex_encode(): chuoi hex -> mang byte. Tra ve so byte giai
 * ma duoc (>= 0), hoac -1 trong 2 truong hop loi: (a) do dai chuoi hex le
 * (khong chia het cho 2 - moi byte phai la dung 2 ky tu hex), (b) so byte giai
 * ma duoc vuot qua out_cap (buffer nguoi goi cung cap khong du lon), hoac co
 * ky tu khong phai hex trong chuoi. Dung o handle_downlink_event() de giai ma
 * payload downlink nhan tu module - LUON phai kiem tra gia tri tra ve < 0
 * truoc khi dung payload, vi du lieu nay den tu ben ngoai (module/network),
 * khong duoc tin tuong mu quang. */
static int hex_decode(const char *hex, uint8_t *out, size_t out_cap)
{
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) {
        return -1;
    }
    size_t out_len = hex_len / 2;
    if (out_len > out_cap) {
        return -1;
    }
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)out_len;
}

/* Parse 1 dong downlink dang: "+EVT:RX_1:<rssi>:<snr>:UNICAST:<port>:<hex>"
 * (vd "+EVT:RX_1:-70:8:UNICAST:1:1234"). Chi goi tu handle_event_line() khi
 * da xac nhan prefix "+EVT:RX_" khop.
 *
 * Format string sscanf giai thich tung phan:
 *   "+EVT:RX_%*d:  " - "%*d" doc va BO QUA 1 so nguyen (chinh la so "1" sau
 *                       RX_, phan biet Class A/B/C - hien khong dung toi nen
 *                       khong can luu, dau "*" bao sscanf khong ghi vao bien nao)
 *   "%d:%d:UNICAST:" - doc rssi va snr (2 so nguyen co dau, vd "-70" va "8")
 *   "%d:"            - doc port (LoRaWAN port, 1-223)
 *   "%255s"          - doc phan con lai (chuoi hex payload) vao hexbuf, gioi
 *                       han 255 ky tu de khop kich thuoc hexbuf[] (RAK3172_LINE_MAXLEN=256,
 *                       tru 1 byte cho NUL ket thuc ma sscanf tu them) */
static void handle_downlink_event(const char *line)
{
    int rssi = 0, snr = 0, port = 0;
    char hexbuf[RAK3172_LINE_MAXLEN];
    int matched = sscanf(line, "+EVT:RX_%*d:%d:%d:UNICAST:%d:%255s", &rssi, &snr, &port, hexbuf);
    if (matched != 4) {
        /* sscanf tra ve SO LUONG truong da doc duoc thanh cong - phai dung 4
         * (rssi, snr, port, hexbuf khop het) moi coi la parse dung; it hon
         * nghia la dong khong dung dinh dang mong doi (vd ban firmware khac). */
        ESP_LOGW(TAG, "Khong parse duoc dong downlink (dinh dang khac ky vong?): %s", line);
        return;
    }
    uint8_t payload[128];
    int len = hex_decode(hexbuf, payload, sizeof(payload));
    if (len < 0) {
        ESP_LOGW(TAG, "Downlink payload hex khong hop le: %s", hexbuf);
        return;
    }
    ESP_LOGI(TAG, "Downlink: port=%d rssi=%d snr=%d len=%d", port, rssi, snr, len);

    /* Cap nhat cache "downlink gan nhat" (phuc vu CLI 'lorawan_status') TRUOC
     * khi goi callback nguoi dung - de du callback co lam gi mat thoi gian,
     * trang thai da duoc ghi nhan dung luc. */
    s_last_downlink.valid = true;
    s_last_downlink.port  = (uint8_t)port;
    s_last_downlink.rssi  = rssi;
    s_last_downlink.snr   = snr;

    if (s_downlink_cb) {
        /* CANH BAO: callback nay chay TRUC TIEP trong RX task (khong qua
         * queue/task rieng) - neu callback block lau, se lam nghen viec doc
         * cac dong UART tiep theo tu module. Nguoi dung dang ky callback
         * (xem transport_lorawan.c: on_lorawan_downlink) phai giu no ngan
         * gon, khong duoc goi lai rak3172_send_cmd() tu day (se deadlock voi
         * chinh s_cmd_lock neu dang co 1 lenh khac cho phan hoi tu RX task nay). */
        s_downlink_cb((uint8_t)port, payload, (size_t)len, rssi, snr);
    }
}

/* Phan loai va xu ly 1 dong bat dau bang "+EVT:" - day la CAC SU KIEN, khong
 * phai phan hoi dong bo cua lenh dang cho (xem handle_line() de biet ranh gioi
 * giua 2 loai). Goi tu RX task, nen MOI nhanh duoi day deu phai nhanh gon. */
static void handle_event_line(const char *line)
{
    if (strcmp(line, "+EVT:JOINED") == 0) {
        ESP_LOGI(TAG, "LoRaWAN joined");
        s_join_state = RAK3172_JOIN_JOINED;
        if (s_join_cb) {
            s_join_cb(true);
        }
    } else if (strncmp(line, "+EVT:JOIN_FAILED", 16) == 0) {
        /* Match theo PREFIX vi hau to cu the (vd _TX_TIMEOUT/_RX_TIMEOUT) tuy
         * ban firmware that - chua xac nhan day du bo hau to, an toan hon la
         * bo sot con hon la doi hoi match tuyet doi. */
        ESP_LOGW(TAG, "LoRaWAN join failed: %s", line);
        s_join_state = RAK3172_JOIN_FAILED;
        if (s_join_cb) {
            s_join_cb(false);
        }
    } else if (strncmp(line, "+EVT:RX_", 8) == 0) {
        handle_downlink_event(line);
    } else if (strncmp(line, "+EVT:SEND_CONFIRMED", 20) == 0) {
        /* Chi lien quan neu AT+CFM=1 - hien tai cau hinh CFM=0 (unconfirmed),
         * giu nhanh nay de tuong thich neu sau nay doi sang confirmed. */
        ESP_LOGI(TAG, "Send-confirmed event: %s", line);
    } else {
        /* Unknown +EVT: - CO Y khong coi la loi nghiem trong (khong return
         * ESP_ERR gi), chi log WARN va bo qua - vi ban RUI3 khac co the sinh
         * them event moi ma driver nay chua biet, khong nen lam sap toan bo
         * he thong chi vi 1 dong log la. */
        ESP_LOGW(TAG, "Event +EVT: khong nhan dien duoc (co the ban RUI3 khac): %s", line);
    }
}

/* Kiem tra 1 dong co phai la "dong ket thuc" 1 phan hoi AT hay khong - tuc
 * la tin hieu "lenh vua roi da xong, khong con dong nao tiep theo nua". Neu
 * co, ghi ket qua thanh cong/that bai vao *out_ok va tra ve true. Neu dong
 * khong khop bat ky chuoi nao trong 2 danh sach nay, tra ve false (nghia la
 * day chi la 1 dong noi dung/du lieu binh thuong, xem nhanh goi ham nay trong
 * handle_line()).
 *
 * ok_status/err_status dinh nghia CUNG TAP HOP voi bang trong docs (xem
 * "AT Command Manual RUI3") - neu module tra ve 1 ma loi moi chua co trong
 * danh sach nay, no se bi coi nham la "dong noi dung" thay vi "dong ket
 * thuc" - rak3172_send_cmd() se cho toi khi HET TIMEOUT thay vi nhan loi ngay
 * lap tuc. Neu gap truong hop nay khi test that, bo sung them ma loi do vao
 * err_status[]. */
static bool is_terminal_status(const char *line, bool *out_ok)
{
    static const char *const ok_status[] = { "OK" };
    static const char *const err_status[] = {
        "AT_ERROR", "AT_PARAM_ERROR", "AT_BUSY_ERROR", "AT_NO_NETWORK_JOINED",
    };
    for (size_t i = 0; i < sizeof(ok_status) / sizeof(ok_status[0]); i++) {
        if (strcmp(line, ok_status[i]) == 0) {
            *out_ok = true;
            return true;
        }
    }
    for (size_t i = 0; i < sizeof(err_status) / sizeof(err_status[0]); i++) {
        if (strcmp(line, err_status[i]) == 0) {
            *out_ok = false;
            return true;
        }
    }
    return false;
}

/* "Bo nao" cua co che demux - xu ly 1 dong hoan chinh DA duoc rak3172_rx_task
 * tach san (khong con \r\n). resp_accum/resp_accum_len la buffer TICH LUY cac
 * dong noi dung cua phan hoi dang cho, duoc RX task giu song song qua nhieu
 * lan goi ham nay (bien 'static' trong rak3172_rx_task, truyen vao qua con
 * tro) - vi 1 phan hoi AT co the trai dai NHIEU dong truoc khi gap dong ket
 * thuc (vd AT+VER=? tra ve 1 dong version roi moi den dong "OK").
 *
 * 3 nhanh xu ly, THEO DUNG THU TU UU TIEN:
 *   1. Dong rong -> bo qua (module hay chen dong rong giua cac phan hoi).
 *   2. Bat dau bang "+EVT:" -> LUON la su kien doc lap, xu ly ngay bat ke
 *      dang co lenh nao cho hay khong (xem handle_event_line).
 *   3. Con lai -> hoac la DONG KET THUC (dong bo lenh vua gui), hoac la 1
 *      DONG NOI DUNG can tich luy tiep - phan biet bang is_terminal_status(). */
static void handle_line(const char *line, char *resp_accum, size_t *resp_accum_len, size_t resp_accum_cap)
{
    if (line[0] == '\0') {
        return;   /* dong rong - bo qua */
    }

    if (strncmp(line, "+EVT:", 5) == 0) {
        handle_event_line(line);
        return;
    }

    bool ok;
    if (is_terminal_status(line, &ok)) {
        /* Dong nay danh dau KET THUC 1 phan hoi - dong goi toan bo noi dung
         * da tich luy (resp_accum) + trang thai OK/loi thanh 1 rak3172_resp_t
         * roi "giao" cho nguoi dang cho qua queue. */
        rak3172_resp_t resp = { .ok = ok };
        strlcpy(resp.payload, resp_accum, sizeof(resp.payload));

        /* Don sach buffer tich luy NGAY, chuan bi cho phan hoi TIEP THEO (co
         * the la cua 1 lenh AT khac hoan toan). */
        *resp_accum_len = 0;
        resp_accum[0] = '\0';

        if (s_awaiting_response) {
            /* xQueueOverwrite: queue nay chi co suc chua 1 phan tu (xem
             * xQueueCreate trong rak3172_init) - Overwrite ghi de bat ke
             * queue dang rong hay da co san 1 gia tri cu chua ai lay, khong
             * bao gio "day" hay block. An toan hon xQueueSend() o day vi
             * nguoi ghi (RX task) TUYET DOI khong duoc phep block cho queue
             * co cho trong - phai luon tiep tuc doc UART ngay. */
            xQueueOverwrite(s_resp_queue, &resp);
        } else {
            /* Khong co ai dang goi rak3172_send_cmd() luc nay ma van nhan
             * duoc dong ket thuc - co the do: (a) rak3172_send_cmd() truoc do
             * da timeout va bo cuoc, nhung phan hoi tre van toi sau; hoac (b)
             * module tu gui 1 dong OK khong ro nguyen nhan. Ca 2 truong hop
             * deu KHONG nguy hiem (khong co ai cho de danh thuc sai), chi log
             * WARN de biet neu can dieu tra. */
            ESP_LOGW(TAG, "Nhan duoc dong ket thuc (%s) nhung khong co lenh nao dang cho - bo qua", line);
        }
        return;
    }

    /* Dong "than" cua phan hoi (vd noi dung tra ve cua AT+VER=?) - gom noi
     * tiep vao cuoi buffer tich luy. Kiem tra "+1" du cho byte NUL ket thuc
     * truoc khi ghi, tranh tran buffer neu phan hoi qua dai bat thuong. */
    size_t line_len = strlen(line);
    if (*resp_accum_len + line_len + 1 < resp_accum_cap) {
        memcpy(resp_accum + *resp_accum_len, line, line_len);
        *resp_accum_len += line_len;
        resp_accum[*resp_accum_len] = '\0';
    } else {
        ESP_LOGW(TAG, "Response buffer day, bo bot noi dung: %s", line);
    }
}

/* Task chay VINH VIEN tu luc rak3172_init() toi khi thiet bi reset - nhiem vu
 * DUY NHAT: doc byte tho tu UART, tu ghep lai thanh tung DONG hoan chinh (ket
 * thuc boi '\n', theo dung khung "\r\n" chuan cua giao thuc AT), roi giao moi
 * dong cho handle_line() xu ly tiep.
 *
 * Vi sao "tu ghep dong" ma khong dung ham co san (vd uart driver's pattern
 * detection)? Vi UART co the giao byte ve KHONG THEO DUNG RANH GIOI 1 dong -
 * 1 lan uart_read_bytes() co the tra ve nua dong, ca dong, hay nhieu dong gop
 * lai, tuy toc do module gui va khi ESP32 kip doc. Vi vay can 1 buffer tich
 * luy (line_buf) giua CAC LAN goi uart_read_bytes(), khong the gia dinh 1 lan
 * doc = 1 dong.
 *
 * line_buf/resp_accum khai bao 'static' (khong phai bien cuc bo tren stack cua
 * ham) vi 256 byte x 2 la kha lon so voi stack mac dinh cua 1 task FreeRTOS -
 * dat static giup chung nam trong vung .bss (RAM tinh) thay vi chiem stack. */
static void rak3172_rx_task(void *arg)
{
    uint8_t byte_buf[128];              /* buffer tam cho 1 lan uart_read_bytes() - khong can lon, chi la "trung chuyen" */
    static char line_buf[RAK3172_LINE_MAXLEN];   /* tich luy 1 DONG dang xay dung, qua nhieu lan doc UART */
    size_t line_len = 0;
    static char resp_accum[RAK3172_RESP_MAXLEN]; /* tich luy NOI DUNG phan hoi (co the nhieu dong), xem handle_line() */
    size_t resp_accum_len = 0;
    resp_accum[0] = '\0';

    while (1) {
        /* Timeout 50ms (khong phai portMAX_DELAY): du lieu khong toi cung
         * khong sao, vong lap chi quay lai doc tiep - gia tri nay khong anh
         * huong do tre xu ly (byte toi bao nhieu, xu ly ngay bay nhieu),
         * chi la khoang thoi gian toi da task "ngu" khi khong co gi de doc. */
        int n = uart_read_bytes(s_uart, byte_buf, sizeof(byte_buf), pdMS_TO_TICKS(50));
        for (int i = 0; i < n; i++) {
            char c = (char)byte_buf[i];
            if (c == '\n') {
                /* Ket thuc 1 dong. RAK3172 gui "\r\n" (CR truoc LF) - da doc
                 * '\r' o nhanh else duoi (bi bo qua khoi line_buf ngay khi
                 * doc duoc, KHONG doi den day moi strip) nen o day line_buf
                 * thuong da SACH san. Dong "if" nay chi la luoi an toan cho
                 * truong hop hiem module gui "\r\n" dinh lien nhau khac thu
                 * tu byte thong thuong. */
                if (line_len > 0 && line_buf[line_len - 1] == '\r') {
                    line_len--;
                }
                line_buf[line_len] = '\0';
                handle_line(line_buf, resp_accum, &resp_accum_len, sizeof(resp_accum));
                line_len = 0;   /* san sang tich luy dong TIEP THEO */
            } else if (c != '\r') {
                /* Ky tu binh thuong (khong phai CR/LF) - them vao dong dang
                 * xay dung, tru khi buffer da day (dong qua dai bat thuong -
                 * cat bo, tranh tran buffer thay vi crash). */
                if (line_len < sizeof(line_buf) - 1) {
                    line_buf[line_len++] = c;
                } else {
                    ESP_LOGW(TAG, "1 dong UART tu RAK3172 vuot qua %d byte, cat bot", RAK3172_LINE_MAXLEN);
                    line_len = 0;
                }
            }
            /* '\r' don le (khong theo sau boi '\n' ngay) bi bo qua o day - se
             * duoc xu ly khi '\n' tuong ung toi, dung logic strip o tren. */
        }
    }
}

/* Khoi tao UART vat ly + tao task RX - PHAI goi truoc bat ky ham rak3172_*
 * nao khac (moi ham khac deu tu kiem tra s_initialized va tra ve
 * ESP_ERR_INVALID_STATE neu chua goi ham nay). Thu tu cac buoc BAT BUOC dung
 * trinh tu (uart_param_config -> uart_set_pin -> uart_driver_install) theo
 * dung yeu cau cua ESP-IDF UART driver. */
esp_err_t rak3172_init(const rak3172_config_t *cfg)
{
    /* Buoc 0 - kiem tra dieu kien tien quyet: GPIO chua duoc cau hinh (gia
     * tri mac dinh -1 trong Kconfig) se khien uart_set_pin() ben duoi that
     * bai kho hieu hoac (te hon) "thanh cong" nham voi chan sai - chan som o
     * day voi thong bao ro rang thay vi de loi kho debug xay ra sau. */
    if (cfg->tx_gpio < 0 || cfg->rx_gpio < 0) {
        ESP_LOGE(TAG, "TX/RX GPIO chua duoc cau hinh dung (tx=%d, rx=%d) - kiem tra Kconfig RAK3172_TX_GPIO/RX_GPIO",
                 cfg->tx_gpio, cfg->rx_gpio);
        return ESP_ERR_INVALID_ARG;
    }

    s_uart = (uart_port_t)cfg->uart_num;

    /* Buoc 1 - cau hinh thong so UART (baudrate/data bits/parity/stop bits) -
     * 115200-8N1 la mac dinh cua RAK3172 RUI3, khong doi duoc tu phia module
     * (phai khop chinh xac o ca 2 dau, khong the "tu dong do"). */
    uart_config_t uart_cfg = {
        .baud_rate = cfg->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(s_uart, &uart_cfg));
    /* Buoc 2 - gan UART logic vao dung chan GPIO vat ly. UART_PIN_NO_CHANGE
     * cho RTS/CTS vi RAK3172 khong dung hardware flow control (flow_ctrl =
     * UART_HW_FLOWCTRL_DISABLE o tren). */
    ESP_ERROR_CHECK(uart_set_pin(s_uart, cfg->tx_gpio, cfg->rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* Buoc 3 - cai driver, cap phat buffer RX noi bo cua ESP-IDF (RAK3172_UART_RX_BUF
     * byte) - day la buffer RIENG cua uart driver, KHAC voi line_buf/byte_buf
     * trong rak3172_rx_task (buffer do la cua ung dung, doc TU buffer nay ra).
     * tx_buffer_size=0 nghia la ghi UART se BLOCK neu FIFO phan cung day, chap
     * nhan duoc vi lenh AT thuong ngan (vai chuc byte). */
    ESP_ERROR_CHECK(uart_driver_install(s_uart, RAK3172_UART_RX_BUF, 0, 0, NULL, 0));

    if (cfg->reset_gpio >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << cfg->reset_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        /* Muc idle mac dinh de HIGH (gia dinh active-low reset, pho bien voi
         * cac module STM32-based) - CAN xac nhan lai dung polarity that cua
         * board truoc khi dung rak3172_reset_module()-qua-GPIO thuc te. */
        gpio_set_level(cfg->reset_gpio, 1);
    }

    /* Queue suc chua 1 phan tu - du dung cho co che "1 lenh in-flight tai 1
     * thoi diem" (xem rak3172_send_cmd/handle_line). Mutex dam bao dung tinh
     * chat do o phia nguoi GUI lenh. */
    s_resp_queue = xQueueCreate(1, sizeof(rak3172_resp_t));
    s_cmd_lock   = xSemaphoreCreateMutex();
    if (s_resp_queue == NULL || s_cmd_lock == NULL) {
        ESP_LOGE(TAG, "Khong the tao queue/mutex noi bo");
        return ESP_ERR_NO_MEM;
    }

    s_join_state   = RAK3172_JOIN_IDLE;
    /* Dat s_initialized = true TRUOC khi tao task - neu khong, co 1 khoang
     * hiep (du rat nho) ma task da chay nhung cac ham public khac van thay
     * s_initialized = false. Thu tu nay khong quan trong lam vi task moi tao
     * chua lam gi ngay, nhung la thoi quen dung dan khi khoi tao trang thai
     * chia se truoc khi "mo cong" cho nguoi dung khac. */
    s_initialized  = true;

    BaseType_t created = xTaskCreate(rak3172_rx_task, "rak3172_rx", 4096, NULL, 5, NULL);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Khong the tao rak3172_rx_task");
        s_initialized = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "rak3172 init OK (uart=%d, tx=%d, rx=%d, reset=%d)",
             s_uart, cfg->tx_gpio, cfg->rx_gpio, cfg->reset_gpio);
    return ESP_OK;
}

/* Gui 1 lenh AT va CHO (block) toi khi nhan duoc dong ket thuc dong bo, hoac
 * het timeout_ms. Day la ham DUY NHAT trong file nay thuc su ghi vao UART -
 * moi ham cong khai khac (configure_identity, join, send_uplink,
 * reset_module) deu chi la "lop vo" goi ham nay voi 1 chuoi AT cu the.
 *
 * An toan goi dong thoi tu nhieu task (CLI, main, sensor task) vi toan bo
 * than ham nam giua 1 cap Take/Give cua s_cmd_lock - task nao goi sau se tu
 * cho task truoc xong. */
esp_err_t rak3172_send_cmd(const char *cmd, char *resp_buf, size_t resp_buf_len, uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* portMAX_DELAY: cho VO HAN de vao duoc vung "chi 1 lenh in-flight" - CO
     * Y khong dat timeout o day, khac voi cho PHAN HOI ben duoi (co
     * timeout_ms). Ly do: neu 1 lenh dang chay that su can toi 3s de xong,
     * lenh ke tiep NEN cho du 3s do (hang doi tu nhien), khong nen bao loi
     * "may ban" chi vi den sau 1 chut. */
    xSemaphoreTake(s_cmd_lock, portMAX_DELAY);

    /* Don du lieu cu con sot lai trong queue (vd tu 1 lan goi truoc bi timeout
     * nhung phan hoi tre van toi sau do) - tranh doc nham phan hoi cua lenh
     * KHAC. Vi queue chi co 1 cho, vong lap nay thuc te chi chay toi da 1 lan,
     * viet dang lap de an toan tuyet doi (khong gia dinh so luong phan tu). */
    rak3172_resp_t stale;
    while (xQueueReceive(s_resp_queue, &stale, 0) == pdTRUE) {
        /* bo qua */
    }

    /* Dat co NGAY TRUOC khi ghi UART - tu thoi diem nay, handle_line() (chay
     * trong RX task) se coi dong ket thuc tiep theo nhan duoc la "cua lenh
     * nay", va xQueueOverwrite() vao s_resp_queue thay vi log WARN "khong ai
     * cho" (xem handle_line). */
    s_awaiting_response = true;

    char line[RAK3172_LINE_MAXLEN];
    int len = snprintf(line, sizeof(line), "%s\r\n", cmd);   /* driver AT can \r\n ket thuc moi lenh - xem doc dau file */
    uart_write_bytes(s_uart, line, len);

    /* Cho o day cho toi khi RX task xQueueOverwrite() (thanh cong) hoac het
     * timeout_ms (that bai) - day la diem BLOCK duy nhat cua ham nay, dung
     * chinh co che dong bo hoa cua FreeRTOS queue, khong can busy-wait/poll. */
    rak3172_resp_t result;
    BaseType_t got = xQueueReceive(s_resp_queue, &result, pdMS_TO_TICKS(timeout_ms));

    /* Ha co xuong TRUOC khi nha khoa - thu tu nay khong quan trong lam (chi
     * co task nay dang giu s_cmd_lock nen khong ai khac doc s_awaiting_response
     * luc nay), nhung giu thu tu ro rang: "don dep trang thai" truoc "cho
     * phep nguoi khac vao". */
    s_awaiting_response = false;
    xSemaphoreGive(s_cmd_lock);

    if (got != pdTRUE) {
        ESP_LOGW(TAG, "Timeout cho phan hoi lenh: %s", cmd);
        return ESP_ERR_TIMEOUT;
    }

    if (resp_buf != NULL && resp_buf_len > 0) {
        strlcpy(resp_buf, result.payload, resp_buf_len);
    }

    return result.ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

/* Chuoi 7 lenh AT tuan tu, THIET LAP che do LoRaWAN OTAA - goi 1 lan truoc
 * rak3172_join(). Idempotent (an toan goi lai moi lan boot, xem rak3172.h).
 *
 * Dung tinh: DUNG NGAY o lenh dau tien that bai (mau "if (err != ESP_OK) return err;"
 * lap lai 7 lan) thay vi co gang chay tiep cac lenh sau - vi cac lenh nay PHU
 * THUOC LAN NHAU theo thu tu (vd AT+NJM phai dat SAU AT+NWM, AT+DEVEUI/APPEUI/APPKEY
 * chi co y nghia neu da o dung NWM/NJM mode) - chay tiep sau 1 loi se dan toi
 * trang thai module khong nhat quan, kho doan truoc. */
esp_err_t rak3172_configure_identity(const char *deveui_hex, const char *appeui_hex,
                                      const char *appkey_hex, int band_index)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (band_index < 0) {
        ESP_LOGE(TAG, "band_index khong hop le (%d) - kiem tra Kconfig RAK3172_BAND_INDEX", band_index);
        return ESP_ERR_INVALID_ARG;
    }

    char cmd[64];
    esp_err_t err;

    err = rak3172_send_cmd("AT+NWM=1", NULL, 0, 3000);   /* 1: chon LoRaWAN mode (khac P2P LoRa/FSK) */
    if (err != ESP_OK) return err;

    snprintf(cmd, sizeof(cmd), "AT+BAND=%d", band_index);   /* 2: vung tan so (AS923/EU868/...) */
    err = rak3172_send_cmd(cmd, NULL, 0, 3000);
    if (err != ESP_OK) return err;

    err = rak3172_send_cmd("AT+NJM=1", NULL, 0, 3000);   /* 3: OTAA (1) thay vi ABP (0) */
    if (err != ESP_OK) return err;

    snprintf(cmd, sizeof(cmd), "AT+DEVEUI=%s", deveui_hex);   /* 4: dinh danh thiet bi */
    err = rak3172_send_cmd(cmd, NULL, 0, 3000);
    if (err != ESP_OK) return err;

    snprintf(cmd, sizeof(cmd), "AT+APPEUI=%s", appeui_hex);   /* 5: dinh danh application/JoinEUI */
    err = rak3172_send_cmd(cmd, NULL, 0, 3000);
    if (err != ESP_OK) return err;

    snprintf(cmd, sizeof(cmd), "AT+APPKEY=%s", appkey_hex);   /* 6: khoa goc, dung derive session key luc join */
    err = rak3172_send_cmd(cmd, NULL, 0, 3000);
    if (err != ESP_OK) return err;

    err = rak3172_send_cmd("AT+CLASS=A", NULL, 0, 3000);   /* 7a: Class A (tiet kiem nang luong nhat, receive window ngan sau moi uplink) */
    if (err != ESP_OK) return err;

    return rak3172_send_cmd("AT+CFM=0", NULL, 0, 3000);   /* 7b: unconfirmed uplink (khong doi ACK tu network server - don gian, du dung cho gia lap cam bien) */
}

/* Kich hoat qua trinh join OTAA - CHI la trigger bat dong bo, xem giai thich
 * day du trong rak3172.h. "1:0:10:8" (theo AT Command Manual):
 *   1  - bat dau join ngay
 *   0  - KHONG tu dong join lai moi lan power-up (main.c tu goi ham nay khi can)
 *   10 - giay giua cac lan thu lai NEU that bai
 *   8  - so lan thu lai toi da */
esp_err_t rak3172_join(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_join_state = RAK3172_JOIN_JOINING;
    esp_err_t err = rak3172_send_cmd("AT+JOIN=1:0:10:8", NULL, 0, 3000);
    if (err != ESP_OK) {
        /* err o day la loi GUI LENH that bai (vd timeout khong nhan duoc OK
         * cho chinh AT+JOIN) - KHAC voi "+EVT:JOIN_FAILED" (loi qua trinh
         * join THAT SU, xu ly rieng trong handle_event_line). Ca 2 truong
         * hop deu dan toi RAK3172_JOIN_FAILED vi ket qua cuoi cung nguoi
         * dung quan tam la nhu nhau: "chua joined duoc". */
        s_join_state = RAK3172_JOIN_FAILED;
    }
    return err;
}

rak3172_join_state_t rak3172_get_join_state(void)
{
    return s_join_state;
}

void rak3172_set_join_callback(rak3172_join_cb_t cb)
{
    s_join_cb = cb;
}

/* Gui 1 uplink LoRaWAN qua AT+SEND=<port>:<hex>. Chuyen payload byte tho
 * (tham so payload/len) sang chuoi hex ASCII truoc khi ghep vao lenh AT - xem
 * hex_encode() dau file va giai thich ve dinh dang AT+SEND trong rak3172.h. */
esp_err_t rak3172_send_uplink(uint8_t port, const uint8_t *payload, size_t len, uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len == 0 || len > 128) {
        /* 128 byte la gioi han an toan chung - gioi han THAT su phu thuoc Data
         * Rate hien tai cua LoRaWAN, module se tu tra AT_PARAM_ERROR neu vuot
         * qua gioi han that. */
        return ESP_ERR_INVALID_ARG;
    }

    /* hex[]: 2 ky tu hex/byte + 1 NUL - "2 * 128 + 1" khop dung gioi han 128
     * byte kiem tra o tren (khong phai so tuy y). cmd[]: du cho tien to
     * "AT+SEND=255:" (toi da ~12 ky tu, lam tron len "16" cho an toan) CONG
     * voi toan bo chuoi hex - tinh bang sizeof(hex) thay vi hang so rieng de
     * tu dong khop neu sau nay doi gioi han 128 byte o tren. */
    char hex[2 * 128 + 1];
    hex_encode(payload, len, hex);

    char cmd[16 + sizeof(hex)];
    snprintf(cmd, sizeof(cmd), "AT+SEND=%u:%s", port, hex);

    return rak3172_send_cmd(cmd, NULL, 0, timeout_ms);
}

void rak3172_set_downlink_callback(rak3172_downlink_cb_t cb)
{
    s_downlink_cb = cb;
}

void rak3172_get_last_downlink_info(rak3172_downlink_info_t *out)
{
    *out = s_last_downlink;
}

esp_err_t rak3172_reset_module(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    /* ATZ khien module reset ngay - co the khong kip tra "OK" truoc khi UART
     * mat ket noi, nen ESP_ERR_TIMEOUT o day la ket qua BINH THUONG, khong
     * han la loi that. */
    return rak3172_send_cmd("ATZ", NULL, 0, 2000);
}

/* Xem giai thich day du trong rak3172.h. AT+LPM=1 bat che do tu dong ngu giua
 * cac lenh AT khi ranh; AT+LPMLVL=1 chon muc STOP1 (giu kha nang danh thuc qua
 * UART - STOP2 tiet kiem hon nhung KHONG danh thuc duoc qua UART, se lam ESP32
 * khong the "goi" lai module o chu ky thuc tiep theo). */
esp_err_t rak3172_enable_low_power(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = rak3172_send_cmd("AT+LPM=1", NULL, 0, 3000);
    if (err != ESP_OK) {
        return err;
    }
    return rak3172_send_cmd("AT+LPMLVL=1", NULL, 0, 3000);
}

/* AT+NJS=? - module tra ve 1 dong noi dung ("0" hoac "1") truoc dong "OK".
 * Parse khoan dung (tim ky tu '1' dau tien trong noi dung, bo qua khoang
 * trang/ky tu la) thay vi so sanh chuoi tuyet doi - phong truong hop module
 * echo them tien to la trong phan hoi (chua xac nhan duoc dinh dang chinh xac
 * tren phan cung that, xem canh bao trong rak3172.h).
 *
 * Neu phat hien da joined, TU CAP NHAT s_join_state = JOINED - de
 * rak3172_get_join_state() (dung boi lenh CLI 'lorawan_status') phan anh dung
 * trang thai THAT cua module ngay ca khi khong di qua rak3172_join() (truong
 * hop transport_lorawan.c bo qua buoc join vi module da joined san). Khong
 * dong gi neu chua joined - de nguyen state hien tai (thuong la IDLE luc
 * boot), vi caller se tu goi rak3172_join() ngay sau do va ham do se tu cap
 * nhat state. */
esp_err_t rak3172_query_join_status(bool *out_joined)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_joined == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char resp[32];
    esp_err_t err = rak3172_send_cmd("AT+NJS=?", resp, sizeof(resp), 3000);
    if (err != ESP_OK) {
        return err;
    }

    *out_joined = (strchr(resp, '1') != NULL);
    if (*out_joined) {
        s_join_state = RAK3172_JOIN_JOINED;
    }
    return ESP_OK;
}
