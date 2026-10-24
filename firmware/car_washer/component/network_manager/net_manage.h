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
    NETMGR_EVENT_ETH_STABLE,  /* (noi bo) day mang da len on dinh NET_ETH_STABLE_MS */
} netmgr_event_id_t;

/* Day mang phai len LIEN TUC chung nay ms moi duoc dung lam mang chinh:
 * day long / switch dam phan lai lam link chap chon se khong lam tu nhay mang lien tuc. */
#define NET_ETH_STABLE_MS 3000

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

#endif