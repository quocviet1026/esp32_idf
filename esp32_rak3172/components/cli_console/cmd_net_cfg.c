/* Copyright (C) 2025 XYZ Corp. All rights reserved. */

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_netif.h"
#include "argtable3/argtable3.h"

#include "app_config.h"
#include "cli_console_internal.h"

static struct {
    struct arg_str *mode;      /* bat buoc: auto|manual */
    struct arg_str *ip;        /* bat buoc neu mode=manual */
    struct arg_str *netmask;   /* bat buoc neu mode=manual */
    struct arg_str *gateway;   /* bat buoc neu mode=manual */
    struct arg_str *dns1;      /* tuy chon */
    struct arg_str *dns2;      /* tuy chon */
    struct arg_end *end;
} s_ipconfig_args;

/* esp_netif_str_to_ip4() vua parse vua validate dinh dang IPv4 - dung lam
 * "bo loc" truoc khi luu vao NVS, tranh AC "nhap sai dinh dang -> bao loi ro
 * rang, khong crash" (tham khao yeu cau IPCONFIG goc). */
static bool is_valid_ipv4(const char *s)
{
    esp_ip4_addr_t tmp;
    return esp_netif_str_to_ip4(s, &tmp) == ESP_OK;
}

static int cmd_ipconfig(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_ipconfig_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_ipconfig_args.end, argv[0]);
        return 1;
    }

    const char *mode_str = s_ipconfig_args.mode->sval[0];
    app_config_t cfg;
    app_config_load(&cfg);

    if (strcmp(mode_str, "auto") == 0) {
        cfg.net_mode = APP_NET_DHCP;
        /* Giu nguyen cac truong net_ip/... cu trong NVS (khong xoa) - de
         * chuyen lai 'manual' sau nay khong can nhap lai tu dau neu muon
         * dung lai dung cau hinh cu. */
    } else if (strcmp(mode_str, "manual") == 0) {
        if (s_ipconfig_args.ip->count == 0 || s_ipconfig_args.netmask->count == 0 || s_ipconfig_args.gateway->count == 0) {
            printf("Loi: mode 'manual' can du <ip> <netmask> <gateway>\n");
            return 1;
        }
        const char *ip = s_ipconfig_args.ip->sval[0];
        const char *netmask = s_ipconfig_args.netmask->sval[0];
        const char *gateway = s_ipconfig_args.gateway->sval[0];
        const char *dns1 = s_ipconfig_args.dns1->count > 0 ? s_ipconfig_args.dns1->sval[0] : "";
        const char *dns2 = s_ipconfig_args.dns2->count > 0 ? s_ipconfig_args.dns2->sval[0] : "";

        if (!is_valid_ipv4(ip))      { printf("Loi: <ip> khong hop le: %s\n", ip); return 1; }
        if (!is_valid_ipv4(netmask)) { printf("Loi: <netmask> khong hop le: %s\n", netmask); return 1; }
        if (!is_valid_ipv4(gateway)) { printf("Loi: <gateway> khong hop le: %s\n", gateway); return 1; }
        if (dns1[0] != '\0' && !is_valid_ipv4(dns1)) { printf("Loi: <dns1> khong hop le: %s\n", dns1); return 1; }
        if (dns2[0] != '\0' && !is_valid_ipv4(dns2)) { printf("Loi: <dns2> khong hop le: %s\n", dns2); return 1; }

        cfg.net_mode = APP_NET_STATIC;
        strlcpy(cfg.net_ip, ip, sizeof(cfg.net_ip));
        strlcpy(cfg.net_netmask, netmask, sizeof(cfg.net_netmask));
        strlcpy(cfg.net_gateway, gateway, sizeof(cfg.net_gateway));
        strlcpy(cfg.net_dns1, dns1, sizeof(cfg.net_dns1));
        strlcpy(cfg.net_dns2, dns2, sizeof(cfg.net_dns2));
    } else {
        printf("Loi: mode phai la 'auto' hoac 'manual'\n");
        return 1;
    }

    esp_err_t err = app_config_save(&cfg);
    if (err == ESP_OK) {
        printf("OK. Can 'mode_set wifi_mqtt' (neu chua) va 'reboot' de ap dung.\n");
    } else {
        printf("Loi luu cau hinh: %s\n", esp_err_to_name(err));
    }
    return err == ESP_OK ? 0 : 1;
}

static int cmd_ipconfig_show(int argc, char **argv)
{
    app_config_t cfg;
    app_config_load(&cfg);

    printf("--- Cau hinh da luu (NVS) - se ap dung o lan boot tiep theo ---\n");
    if (cfg.net_mode == APP_NET_DHCP) {
        printf("net_mode = auto (DHCP)\n");
    } else {
        printf("net_mode = manual (static IP)\n");
        printf("net_ip      = %s\n", cfg.net_ip);
        printf("net_netmask = %s\n", cfg.net_netmask);
        printf("net_gateway = %s\n", cfg.net_gateway);
        printf("net_dns1    = %s\n", cfg.net_dns1[0] ? cfg.net_dns1 : "(chua dat)");
        printf("net_dns2    = %s\n", cfg.net_dns2[0] ? cfg.net_dns2 : "(chua dat)");
    }

    /* Trang thai mang THAT dang chay - doc thang tu netif, KHONG phai tu NVS.
     * Co the KHAC voi phan "da luu" o tren neu vua doi cau hinh nhung chua
     * reboot, hoac dang chay o mode lorawan (netif STA khong ton tai). */
    printf("--- Trang thai mang THAT (runtime, dang chay) ---\n");
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        printf("(chua co netif WiFi STA nao duoc tao - dang o mode lorawan?)\n");
        return 0;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        printf("ip      = " IPSTR "\n", IP2STR(&ip_info.ip));
        printf("netmask = " IPSTR "\n", IP2STR(&ip_info.netmask));
        printf("gateway = " IPSTR "\n", IP2STR(&ip_info.gw));
    } else {
        printf("(chua co IP - WiFi dang ket noi do hoac chua ket noi duoc)\n");
    }
    return 0;
}

void register_net_cfg_cmds(void)
{
    s_ipconfig_args.mode    = arg_str1(NULL, NULL, "<auto|manual>", "Che do cap IP cho WiFi STA");
    s_ipconfig_args.ip      = arg_str0(NULL, NULL, "<ip>", "Dia chi IP tinh (bat buoc neu manual)");
    s_ipconfig_args.netmask = arg_str0(NULL, NULL, "<netmask>", "Subnet mask (bat buoc neu manual)");
    s_ipconfig_args.gateway = arg_str0(NULL, NULL, "<gateway>", "Default gateway (bat buoc neu manual)");
    s_ipconfig_args.dns1    = arg_str0(NULL, NULL, "<dns1>", "DNS chinh (tuy chon)");
    s_ipconfig_args.dns2    = arg_str0(NULL, NULL, "<dns2>", "DNS phu (tuy chon)");
    s_ipconfig_args.end     = arg_end(3);

    const esp_console_cmd_t ipconfig_cmd = {
        .command = "ipconfig",
        .help = "ipconfig auto | ipconfig manual <ip> <netmask> <gateway> [<dns1>] [<dns2>] - Luu cau hinh IP cho WiFi STA vao NVS. Can reboot de ap dung.",
        .hint = NULL,
        .func = &cmd_ipconfig,
        .argtable = &s_ipconfig_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ipconfig_cmd));

    const esp_console_cmd_t ipconfig_show_cmd = {
        .command = "ipconfig_show",
        .help = "In cau hinh IP (DHCP hoac static) da luu",
        .hint = NULL,
        .func = &cmd_ipconfig_show,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ipconfig_show_cmd));
}
