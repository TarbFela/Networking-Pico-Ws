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

#include "../UDP_cfg.h"
// Destination port and IP address
typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    bool complete;
    ip_addr_t gw;
} TCP_SERVER_T;

// --- Globals ---
static ip_addr_t client_ip;
static bool client_connected = false;
volatile bool client_joined;   // signal when a client gets a DHCP lease
semaphore_t new_message;
volatile bool data_ready = false;
volatile uint16_t *data_addr = 0;
char received_data[BEACON_MSG_LEN_MAX];

volatile uint packets_counter = 0;
volatile uint ack_counter = 0;
volatile uint dma_counter = 0;
volatile uint64_t trr_r = 0;
volatile uint64_t trr_t = 0;

static struct udp_pcb *udp_rx_pcb;

static struct udp_pcb *udp_tx_pcb;

// Protocol control block for UDP receive connection
static struct udp_pcb *udp_rx_pcb;

// Buffer in which to copy received messages
char received_data[BEACON_MSG_LEN_MAX] ;


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

        uint8_t test_data[BEACON_MSG_LEN_MAX];
        for(int i = 0; i<BEACON_MSG_LEN_MAX; i++) {
            test_data[i] = 2*i;
        }

        int n = 0;
        while (1) {

            PT_YIELD_UNTIL(pt, data_ready);
            data_ready = false;
            // 256 samples --> 512 bytes --> 4x 127-byte messages (plus some loss...)
            for(int i = 0; i<((256*2)/BEACON_MSG_LEN_MAX); i++) {
                struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, BEACON_MSG_LEN_MAX + 1, PBUF_RAM);
                char *req = (char *) p->payload;
                memset(req, 0, BEACON_MSG_LEN_MAX + 1);
                memcpy(req, (char *) data_addr + (i*BEACON_MSG_LEN_MAX), BEACON_MSG_LEN_MAX);
                udp_sendto(udp_tx_pcb, p, &client_ip, UDP_PORT);
                trr_t = time_us_64();
                pbuf_free(p);
                packets_counter++;

            }
            if(packets_counter%100 == 0) {
                printf("\t,SENT, %d, "
                       "ACK, %d, "
                       "DMA, %d, "
                       "DMA SKIPPAGE, %d, "
                       "ACK SKIPPAGE, %d,"
                       "\n",
                       packets_counter,ack_counter, dma_counter,
                       dma_counter*((256*2)/BEACON_MSG_LEN_MAX) - packets_counter,
                       packets_counter-ack_counter
                       );
//                printf("TRR: %ld\n",(int32_t)(trr_r-trr_t));
            }

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
            trr_r = time_us_64();
            printf("%ld,\n",(uint32_t)(trr_r-trr_t));
            ack_counter++;
            //printf("%s\n", received_data);
        }

    PT_END(pt);
}

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "capstone_adc.h"

volatile capstone_adc_struct_t cas;


void dma_handler() {
    // if the second [1] of the DMAs has triggered an interrupt, we can probably assume (DANGEROUS) that we should look halfway through the buffer.

    int data_offset = (ADC_BUFFER_SIZE / 2) * (int) dma_channel_get_irq0_status(cas.adc_dma_daisy_chain[1]);
    // this shouldn't produce a branch... I think?
    int culprit_dma_daisy_chain_index = (data_offset != 0);
    uint16_t *data = &(cas.adc_dma_buffer[data_offset]);
    // a little stupid, but set the write address to where we starting getting data from.
    // TODO: configure the DMAs as wrapping rings so that they don't need to be reset. or use a third, cleanup DMA
    (cas.adc_dma_daisy_chain_hw[culprit_dma_daisy_chain_index])->write_addr = (uintptr_t)(data);
// clear the correct interrupt
    dma_hw->ints0 = 0x1 << cas.adc_dma_daisy_chain[culprit_dma_daisy_chain_index];
    // debugging toggler
    sio_hw->gpio_togl = 0x1<<18;

    data_ready = true;
    data_addr = data;

    dma_counter++;
}

int main() {
    stdio_init_all();

    sleep_ms(4000);
    printf("Setting up!\n");
    sem_init(&new_message, 0, 1);


    // DATA FLOW SETUP!
    capstone_adc_init(&cas,dma_handler);

    // SERVER(S)

    printf("Server setup...\n");

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

    printf("Starting ADC data...\n");
    capstone_adc_start(&cas);

    printf("Starting PT Threads...\n");
    pt_schedule_start;  // replaces the while(!complete) loop

    dhcp_server_deinit(&dhcp_server);
    cyw43_arch_deinit();
    printf("Test complete\n");
    return 0;

}