/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

/* transport.c
 *
 * Day la "bo nao" cua toan bo lop transport-abstraction: KHONG tu ket noi WiFi/
 * MQTT/LoRaWAN nao ca (viec do 2 file transport_wifi_mqtt.c/transport_lorawan.c
 * lam), file nay chi lam dung 2 viec:
 *   1. transport_get() - tra ve DIA CHI cua 1 trong 2 struct transport_if_t
 *      co san (g_transport_wifi_mqtt hoac g_transport_lorawan), dua theo mode.
 *   2. transport_notify_ready() - 1 "cong gac" dam bao callback nguoi dung
 *      dang ky qua transport_set_ready_callback() CHI chay dung 1 lan trong ca
 *      vong doi thiet bi, du ham nay co bi goi lai nhieu lan.
 */

#include "esp_log.h"

#include "transport.h"
#include "transport_internal.h"

static const char *TAG = "transport";

/* s_ready_cb: con tro toi ham cua main.c (on_transport_ready) - luu lai o day
 * de goi duoc no ngay tu ben trong transport_notify_ready(), ma khong can
 * transport.c biet gi ve main.c (main.c chu dong "dang ky" xuong, transport.c
 * khong "hoi len" - dao nguoc huong phu thuoc, giong Dependency Inversion). */
static transport_ready_cb_t s_ready_cb;
static transport_downlink_cb_t s_downlink_cb;   /* con tro toi ham cua main.c nhan lenh tu server - cung nguyen tac "chu dong dang ky xuong" nhu s_ready_cb */

/* s_ready_fired: cai "chot" quyet dinh transport_notify_ready() co thuc su
 * lam gi hay khong. false = "chua bao lan nao", true = "da bao roi, moi lan
 * goi sau chi la no-op". Day la bien static toan cuc nen mac dinh = false
 * (0) khi chuong trinh khoi dong, khong can khoi tao tuong minh. */
static bool s_ready_fired;

const transport_if_t *transport_get(app_mode_t mode)
{
    /* Toan tu 3-ngoi (?:) o day CHINH LA "diem re nhanh" duy nhat trong toan
     * bo firmware quyet dinh dung WiFi/MQTT hay LoRaWAN - moi noi khac trong
     * code (main.c, sensor task...) khong con cau lenh if/switch nao kiem tra
     * mode nua, vi tat ca da "gap khuc" vao dung 1 dia chi ham duoc tra ve o
     * day. g_transport_wifi_mqtt/g_transport_lorawan la 2 hang so extern,
     * dinh nghia trong transport_wifi_mqtt.c/transport_lorawan.c (xem
     * transport_internal.h) - ham nay khong tao gi moi, chi "chi tay" vao 1
     * trong 2 struct da co san tu luc bien dich. */
    return (mode == APP_MODE_WIFI_MQTT) ? &g_transport_wifi_mqtt : &g_transport_lorawan;
}

void transport_set_ready_callback(transport_ready_cb_t cb)
{
    /* Chi luu con tro, KHONG goi gi ca o day - main.c bat buoc phai goi ham
     * nay TRUOC khi goi transport->start(), neu khong s_ready_cb con NULL
     * dung luc transport_notify_ready() lan dau tien chay (xem nhanh
     * "if (s_ready_cb)" ben duoi - im lang bo qua thay vi crash, nhung ket
     * qua la task cam bien se KHONG BAO GIO duoc tao). */
    s_ready_cb = cb;
}

void transport_notify_ready(void)
{
    /* Buoc 1 - kiem tra chot: neu da bao roi (vd MQTT connect lan dau xong,
     * sau do WiFi chap chon khien MQTT disconnect/reconnect vai lan nua),
     * thoat ngay, KHONG goi lai s_ready_cb() - day chinh la co che chan
     * "sinh trung task cam bien" moi lan reconnect (bug tiem an neu khong co
     * chot nay, vi ca transport_wifi_mqtt.c lan transport_lorawan.c deu co
     * the goi ham nay nhieu lan trong doi thuc). */
    if (s_ready_fired) {
        return;   /* da bao 1 lan roi - bo qua im lang, KHONG log gi them de tranh spam log moi lan reconnect */
    }

    /* Buoc 2 - dong chot NGAY LAP TUC, truoc ca khi goi callback. Quan trong:
     * neu dao thu tu (goi s_ready_cb() roi moi set s_ready_fired = true), va
     * ban than s_ready_cb() lai vo tinh lam phat sinh 1 loi goi
     * transport_notify_ready() de quy/long nhau (khong xay ra trong code hien
     * tai, nhung la thoi quen phong thu tot), chot se chua kip dong. */
    s_ready_fired = true;

    ESP_LOGI(TAG, "Transport san sang lan dau tien");

    /* Buoc 3 - goi callback cua main.c (neu co dang ky) de bao tin, thuong la
     * de main.c tao task cam bien dinh ky (xem on_transport_ready() trong
     * main.c). Kiem tra NULL o day vi transport_set_ready_callback() la tuy
     * chon - implementation khong bat buoc phai co ai lang nghe. */
    if (s_ready_cb) {
        s_ready_cb();
    }
}

void transport_set_downlink_callback(transport_downlink_cb_t cb)
{
    s_downlink_cb = cb;
}

void transport_notify_downlink(const app_command_t *cmd)
{
    /* KHONG co chot "chi 1 lan" nhu transport_notify_ready() - downlink hop
     * le va can chuyen tiep MOI lan no toi, khong phai su kien mot-lan-trong-doi. */
    if (s_downlink_cb) {
        s_downlink_cb(cmd);
    }
}

/* Vi sao KHONG can mutex/atomic cho s_ready_fired du ham nay co the duoc goi
 * tu nhieu "nguon" khac nhau (mqtt_event_handler trong transport_wifi_mqtt.c,
 * hoac on_lorawan_joined trong transport_lorawan.c)?
 *
 * Vi tai bat ky thoi diem nao trong 1 lan chay, CHI DUNG 1 trong 2 transport
 * dang hoat dong (transport_get() chon 1 lan luc boot, khong bao gio ca 2
 * cung chay - xem transport.h). Ma ngay ca voi transport WiFi/MQTT, moi event
 * MQTT (ke ca cac lan CONNECTED do reconnect) deu duoc esp-mqtt phat tuan tu
 * trong CUNG 1 task noi bo cua no, khong bao gio chay dong thoi 2 event
 * CONNECTED cung luc. Vi vay khong bao gio co 2 "luong thuc thi" cung doc/ghi
 * s_ready_fired dong thoi trong thuc te - an toan du khong dung khoa. */
