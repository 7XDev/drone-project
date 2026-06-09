#include <string.h>

#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#include "queuemonitor.h"
#include "wifi_esp32.h"
#include "stm32_legacy.h"
#define DEBUG_MODULE  "WIFI_UDP"
#include "debug_cf.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#define UDP_SERVER_PORT         2390
#define UDP_SERVER_BUFSIZE      64
#define UDP_DISCOVERY_PORT      2391
#define UDP_BROADCAST_INTERVAL_MS 1000

/* SSID and password for the WiFi network to connect to */
#define STA_SSID                "dronewifi"
#define STA_PASSWORD            "12345678"
#define STA_MAX_RETRY           10

static struct sockaddr_storage source_addr;

/* STA connection state */
static int s_retry_num = 0;
static bool isSTAConnected = false;

static int sock;
static xQueueHandle udpDataRx;
static xQueueHandle udpDataTx;

static bool isInit = false;
static bool isUDPInit = false;
static bool isUDPConnected = false;

static char drone_id_str[32] = "";  /* Unique drone identifier e.g. "drone_AABBCCDDEEFF" */

#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

static esp_err_t udp_server_create(void *arg);

static uint8_t calculate_cksum(void *data, size_t len)
{
    unsigned char *c = data;
    int i;
    unsigned char cksum = 0;

    for (i = 0; i < len; i++) {
        cksum += *(c++);
    }

    return cksum;
}

/**
 * STA event handler: connect/disconnect from the router AP
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        DEBUG_PRINT_LOCAL("STA started, connecting to %s", STA_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        isSTAConnected = false;
        isUDPConnected = false;
        if (s_retry_num < STA_MAX_RETRY) {
            DEBUG_PRINT_LOCAL("STA disconnected, retrying (%d/%d)...", s_retry_num + 1, STA_MAX_RETRY);
            esp_wifi_connect();
            s_retry_num++;
        } else {
            DEBUG_PRINT_LOCAL("STA max retries reached. Rebooting...");
            esp_restart();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        DEBUG_PRINT_LOCAL("Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        isSTAConnected = true;
        isUDPConnected = true;
    }
}

/**
 * Discovery responder task: listens on UDP_DISCOVERY_PORT for "ESPDRONE_DISCOVER"
 * and replies with drone identity info so clients on the network can find all drones.
 */
static void discovery_responder_task(void *pvParameters)
{
    int disc_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (disc_sock < 0) {
        DEBUG_PRINT_LOCAL("Discovery socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in disc_addr;
    memset(&disc_addr, 0, sizeof(disc_addr));
    disc_addr.sin_family = AF_INET;
    disc_addr.sin_port = htons(UDP_DISCOVERY_PORT);
    disc_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(disc_sock, (struct sockaddr *)&disc_addr, sizeof(disc_addr)) < 0) {
        DEBUG_PRINT_LOCAL("Discovery bind failed");
        close(disc_sock);
        vTaskDelete(NULL);
        return;
    }

    DEBUG_PRINT_LOCAL("Discovery responder listening on port %d", UDP_DISCOVERY_PORT);

    while (1) {
        char rx_buf[64];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        int len = recvfrom(disc_sock, rx_buf, sizeof(rx_buf) - 1, 0,
                           (struct sockaddr *)&from_addr, &from_len);
        if (len > 0) {
            rx_buf[len] = '\0';
            if (strcmp(rx_buf, "ESPDRONE_DISCOVER") == 0) {
                /* Reply with drone ID and IP info */
                char reply[128];
                struct sockaddr_in our_addr;
                socklen_t our_len = sizeof(our_addr);
                getsockname(sock, (struct sockaddr *)&our_addr, &our_len);

                uint32_t our_ip = ntohl(our_addr.sin_addr.s_addr);
                snprintf(reply, sizeof(reply), "ESPDRONE %s %u.%u.%u.%u",
                         drone_id_str,
                         (unsigned)((our_ip >> 24) & 0xFF),
                         (unsigned)((our_ip >> 16) & 0xFF),
                         (unsigned)((our_ip >> 8) & 0xFF),
                         (unsigned)(our_ip & 0xFF));

                sendto(disc_sock, reply, strlen(reply), 0,
                       (struct sockaddr *)&from_addr, from_len);
                
                uint32_t from_ip = ntohl(from_addr.sin_addr.s_addr);
                DEBUG_PRINT_LOCAL("Discovery reply sent to %u.%u.%u.%u",
                         (unsigned)((from_ip >> 24) & 0xFF),
                         (unsigned)((from_ip >> 16) & 0xFF),
                         (unsigned)((from_ip >> 8) & 0xFF),
                         (unsigned)(from_ip & 0xFF));
            }
        }
    }
}

bool wifiTest(void)
{
    return isInit;
};

bool wifiGetDataBlocking(UDPPacket *in)
{
    /* command step - receive  02  from udp rx queue */
    while (xQueueReceive(udpDataRx, in, portMAX_DELAY) != pdTRUE) {
        vTaskDelay(M2T(10));
    }; // Don't return until we get some data on the UDP

    return true;
};

bool wifiSendData(uint32_t size, uint8_t *data)
{
    UDPPacket outStage = {0};
    outStage.size = size;
    memcpy(outStage.data, data, size);
    // Dont' block when sending
    return (xQueueSend(udpDataTx, &outStage, M2T(100)) == pdTRUE);
};

static esp_err_t udp_server_create(void *arg)
{
    if (isUDPInit){
        return ESP_OK;
    }

    static struct sockaddr_in dest_addr = {0};
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_SERVER_PORT);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        DEBUG_PRINT_LOCAL("Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }
    DEBUG_PRINT_LOCAL("Socket created");

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        DEBUG_PRINT_LOCAL("Socket unable to bind: errno %d", errno);
    }
    DEBUG_PRINT_LOCAL("Socket bound, port %d", UDP_SERVER_PORT);

    isUDPInit = true;
    return ESP_OK;
}

static void udp_server_rx_task(void *pvParameters)
{
    socklen_t socklen = sizeof(source_addr);
    char rx_buffer[UDP_SERVER_BUFSIZE];
    UDPPacket inPacket = {0};

    while (true) {
        if(isUDPInit == false) {
            vTaskDelay(20);
            continue;
        }
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);
        /* command step - receive  01 from Wi-Fi UDP */
        if (len < 0) {
            DEBUG_PRINT_LOCAL("recvfrom failed: errno %d", errno);
            continue;
        } else if(len > WIFI_RX_TX_PACKET_SIZE) {
            DEBUG_PRINT_LOCAL("Received data length = %d > %d", len, WIFI_RX_TX_PACKET_SIZE);
            continue;
        } else {
            uint8_t cksum = rx_buffer[len - 1];
            //remove cksum, do not belong to CRTP
            //check packet
            if (cksum == calculate_cksum(rx_buffer, len - 1)) {
                //copy part of the UDP packet, the size not include cksum
                inPacket.size = len - 1;
                memcpy(inPacket.data, rx_buffer, inPacket.size);
                xQueueSend(udpDataRx, &inPacket, M2T(10));
                if(!isUDPConnected) isUDPConnected = true;
            }else{
                DEBUG_PRINT_LOCAL("udp packet cksum unmatched");
            }

#ifdef DEBUG_UDP
            printf("\nReceived size = %d cksum = %02X\n", inPacket.size, cksum);
            for (size_t i = 0; i < inPacket.size; i++) {
                printf("%02X ", inPacket.data[i]);
            }
            printf("\n");
#endif
        }
    }
}

static void udp_server_tx_task(void *pvParameters)
{
    UDPPacket outPacket = {0};
    while (TRUE) {
        if(isUDPInit == false) {
            vTaskDelay(20);
            continue;
        }
        if ((xQueueReceive(udpDataTx, &outPacket, portMAX_DELAY) == pdTRUE) && isUDPConnected) {
            // append cksum to the packet
            outPacket.data[outPacket.size] = calculate_cksum(outPacket.data, outPacket.size);
            outPacket.size += 1;

            int err = sendto(sock, outPacket.data, outPacket.size, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
            if (err < 0) {
                DEBUG_PRINT_LOCAL("Error occurred during sending: errno %d", errno);
                continue;
            }
#ifdef DEBUG_UDP
            printf("\nSend size = %d checksum = %02X\n", outPacket.size, outPacket.data[outPacket.size - 1]);
            for (size_t i = 0; i < outPacket.size; i++) {
                printf("%02X ", outPacket.data[i]);
            }
            printf("\n");
#endif
        }
    }
}

void wifiInit(void)
{
    if (isInit) {
        return;
    }
    
    udpDataRx = xQueueCreate(16, sizeof(UDPPacket));
    DEBUG_QUEUE_MONITOR_REGISTER(udpDataRx);
    udpDataTx = xQueueCreate(16, sizeof(UDPPacket));
    DEBUG_QUEUE_MONITOR_REGISTER(udpDataTx);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL,
                    NULL));

    /* Build a unique drone ID from MAC address */
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(drone_id_str, sizeof(drone_id_str), "drone_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = STA_SSID,
            .password = STA_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    DEBUG_PRINT_LOCAL("STA connecting to SSID:%s", STA_SSID);

    /* Create UDP CRTP server socket */
    if (udp_server_create(NULL) == ESP_FAIL) {
        DEBUG_PRINT_LOCAL("UDP server create socket failed");
    } else {
        DEBUG_PRINT_LOCAL("UDP server create socket succeed");
    }
    
    xTaskCreate(udp_server_tx_task, UDP_TX_TASK_NAME, UDP_TX_TASK_STACKSIZE, NULL, UDP_TX_TASK_PRI, NULL);
    xTaskCreate(udp_server_rx_task, UDP_RX_TASK_NAME, UDP_RX_TASK_STACKSIZE, NULL, UDP_RX_TASK_PRI, NULL);
    
    /* Start the discovery responder so clients can find this drone on the network */
    xTaskCreate(discovery_responder_task, "disc_responder", 4096, NULL, 5, NULL);
    
    isInit = true;
}