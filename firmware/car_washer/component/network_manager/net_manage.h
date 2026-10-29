#ifndef __NET_MANAGE_H__
#define __NET_MANAGE_H__

#include "esp_event.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stddef.h>

ESP_EVENT_DECLARE_BASE(NETMGR_EVENT);

typedef enum
{
    NM_IF_ETH = 0,
    NM_IF_WIFI,
    NM_IF_PPP,
    NM_IF_MAX
} nm_if_t;

typedef enum
{
    NETMGR_EVENT_DEFAULT_CHANGED = 0,
    NETMGR_EVENT_ONLINE,
    NETMGR_EVENT_OFFLINE,
    NETMGR_EVENT_IF_DOWN,     /* 1 mang vua mat (rut day, rot WiFi, mat 4G). data = esp_netif_t* cua mang do */
} netmgr_event_id_t;


void net_manager_init(esp_netif_t *eth_netif, esp_netif_t *wifi_netif, esp_netif_t *ppp_netif);
void net_manager_set_priority(const nm_if_t *order, size_t count);
void net_manager_evaluate(void);
esp_netif_t *net_manager_get_default_netif(void);
bool net_manager_is_primary_available(void);

/* Loai mang t (Ethernet / WiFi / 4G) dang co ket noi Internet dung duoc */
bool net_manager_type_ready(nm_if_t t);

/* Mang n dang dung duoc (co link + co IP; Ethernet: da on dinh) */
bool net_manager_netif_ready(esp_netif_t *n);

/* Con mang nao dung duoc NGOAI mang exclude khong (de bao truoc khi ngat WiFi) */
bool net_manager_has_ready_other(esp_netif_t *exclude);

/* Mang dung duoc uu tien cao nhat ngoai exclude: "eth" / "wifi" / "4g"; NULL neu khong con */
const char *net_manager_ready_other_name(esp_netif_t *exclude);
void net_manager_update_ppp_netif(esp_netif_t *netif);

/* cloud_manager bao: mang chinh hien tai CO IP nhung khong toi duoc server (DNS / TCP loi lien tiep).
 * Ethernet: lan dau khoi dong lai W5500 (giong rut/cam lai day); van loi -> tat ETH 2 phut, chay bang
 * WiFi/4G roi thu lai. WiFi/4G: tam bo qua 2 phut neu con mang khac. Ghi log trang thai mang de chan doan. */
void net_manager_report_unreachable(void);

/* cloud_manager bao: da ket noi server qua mang chinh hien tai -> xoa dem loi cua mang do */
void net_manager_report_reachable(void);

#endif