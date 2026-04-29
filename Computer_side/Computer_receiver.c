/**
 * Copyright (c) 2022 Andrew McDonnell
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Standard libraries
#include <string.h>
#include "pico/sync.h"
// LWIP libraries
#include "lwip/pbuf.h"
#include "lwip/udp.h"
// Pico SDK hardware support libraries
#include "hardware/sync.h"
// Our header for making WiFi connection
#include "connect.h"
// Protothreads
#include "pt_cornell_rp2040_v1_4.h"

#include "dhcpserver/dhcpserver.h"
// Destination port and IP address
typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    bool complete;
    ip_addr_t gw;
} TCP_SERVER_T;

#define UDP_PORT 1234
#define BEACON_TARGET "172.20.10.2"

// Maximum length of our message
#define BEACON_MSG_LEN_MAX 127
// --- Globals ---
static ip_addr_t client_ip;
static bool client_connected = false;
volatile bool client_joined;   // signal when a client gets a DHCP lease
semaphore_t new_message;
char received_data[BEACON_MSG_LEN_MAX];

static struct udp_pcb *udp_rx_pcb;

static struct udp_pcb *udp_tx_pcb;

// Protocol control block for UDP receive connection
static struct udp_pcb *udp_rx_pcb;

// Buffer in which to copy received messages
char received_data[BEACON_MSG_LEN_MAX] ;

// Semaphore for signaling a new received message
semaphore_t new_message ;

static void udpecho_raw_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                 const ip_addr_t *addr, u16_t port)
{
  LWIP_UNUSED_ARG(arg);

  // Check that there's something in the pbuf
  if (p != NULL) {
    // Copy the contents of the payload
    memcpy(received_data, p->payload, BEACON_MSG_LEN_MAX) ;
    // Semaphore-signal a thread
    PT_SEM_SDK_SIGNAL(pt, &new_message) ;
    // Reset the payload buffer
    memset(p->payload, 0, BEACON_MSG_LEN_MAX+1);
    // Free the PBUF
    pbuf_free(p);
  }
  else printf("NULL pt in callback");
}

// ===================================
// Define the recv callback
void  udpecho_raw_init(void)
{

  // Initialize the RX protocol control block
  udp_rx_pcb  = udp_new_ip_type(IPADDR_TYPE_ANY);

  // Make certain that the pcb has initialized, else print a message
  if (udp_rx_pcb != NULL) {
    // Err_t object for error codes
    err_t err;
    // Bind this PCB to our assigned IP, and our chosen port. Received messages
    // to this port will be directed to our bound protocol control block.
    err = udp_bind(udp_rx_pcb, netif_ip_addr4(netif_default), UDP_PORT);
    // Check that the bind was successful, else print a message
    if (err == ERR_OK) {
      // Setup the receive callback function
      udp_recv(udp_rx_pcb, udpecho_raw_recv, NULL);
    } else {
      printf("bind error");
    }
  } else {
    printf("udp_rx_pcb error");
  }
}
static PT_THREAD(protothread_stream_send(struct pt *pt))
{
    PT_BEGIN(pt);

        // Wait until DHCP has handed out a lease
//        PT_SEM_SDK_WAIT(pt, &client_joined);

        udp_tx_pcb = udp_new();

        int n = 0;
        while (1) {

            sleep_ms(1000);
            n++;
            struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, BEACON_MSG_LEN_MAX+1, PBUF_RAM);
            char *req = (char *)p->payload;
            memset(req, 0, BEACON_MSG_LEN_MAX+1);
            snprintf(req, BEACON_MSG_LEN_MAX, "message number %d\n",n );

            udp_sendto(udp_tx_pcb, p, &client_ip, UDP_PORT);
            pbuf_free(p);
        }

    PT_END(pt);
}

static PT_THREAD(protothread_stream_receive(struct pt *pt))
{
    PT_BEGIN(pt);

        // Also wait — don't bind RX until we know we have a client
//        PT_SEM_SDK_WAIT(pt, &client_joined);

        udpecho_raw_init(); // bind RX pcb, register callback

        while (1) {
            PT_SEM_SDK_WAIT(pt, &new_message);
            printf("%s\n", received_data);
        }

    PT_END(pt);
}


int main() {
    stdio_init_all();

    sleep_ms(10000);
    printf("Setting up!\n");
    sem_init(&new_message, 0, 1);

    TCP_SERVER_T *state = calloc(1, sizeof(TCP_SERVER_T));
    if (!state) {
        printf("failed to allocate state\n");
        return 1;
    }

    if (cyw43_arch_init()) {
        printf("failed to initialize\n");
        return 1;
    }

    cyw43_arch_enable_ap_mode(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

#define IP(x) (x)

    ip4_addr_t mask;
    IP(state->gw).addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    IP(mask).addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);

#undef IP

    // Start the dhcp server
    volatile dhcp_server_t dhcp_server;
    dhcp_server.client_ip_out = &client_ip;
    dhcp_server.client_joined = &client_joined;
    dhcp_server_init(&dhcp_server, &state->gw, &mask);

    printf("Waiting for connection!\n");
    while(*dhcp_server.client_joined != true) {
        sleep_ms(1000);
    }
    printf("New connection complete. Initializing PT threads...\n");
    pt_add_thread(protothread_stream_send);
    pt_add_thread(protothread_stream_receive);
    pt_schedule_start;  // replaces the while(!complete) loop

    dhcp_server_deinit(&dhcp_server);
    cyw43_arch_deinit();
    printf("Test complete\n");
    return 0;

}