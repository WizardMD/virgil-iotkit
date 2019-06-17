/**
 * Copyright (C) 2019 Virgil Security Inc.
 *
 * Lead Maintainer: Virgil Security Inc. <support@virgilsecurity.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     (1) Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *     (2) Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 *     (3) Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "stdint.h"
#include <stdbool.h>
#include <stddef.h>

#include <virgil/iot/protocols/sdmp/sdmp_structs.h>
#include <virgil/iot/protocols/sdmp.h>

typedef union {
    uint8_t membuf;

    struct {
        unsigned initialized : 1;
        unsigned deinitialized : 1;
        unsigned mac_addr_set_up : 1;
        unsigned sent : 1;
    };
} netif_state_t;

extern netif_state_t netif_state;
extern vs_mac_addr_t mac_addr_client_call;
extern vs_mac_addr_t mac_addr_server_call;
extern bool is_client_call;

void
prepare_test_netif(vs_netif_t *netif);

#define SDMP_CHECK_GOTO(OPERATION, DESCRIPTION)                                                                        \
    if ((OPERATION) != 0) {                                                                                            \
        VS_LOG_ERROR(DESCRIPTION);                                                                                     \
        goto terminate;                                                                                                \
    }

#define SDMP_CHECK_ERROR_GOTO(OPERATION, DESCRIPTION)                                                                  \
    if ((OPERATION) == 0) {                                                                                            \
        VS_LOG_ERROR(DESCRIPTION);                                                                                     \
        goto terminate;                                                                                                \
    }

#define MAC_ADDR_CHECK_GOTO(CURRENT, WAITED)                                                                           \
    if (memcmp((CURRENT).bytes, (WAITED).bytes, sizeof(vs_mac_addr_t))) {                                              \
        VS_LOG_ERROR("Current MAC address is incorrect");                                                              \
        goto terminate;                                                                                                \
    }

#define NETIF_OP_CHECK_GOTO(OPERATION)                                                                                 \
    if ((OPERATION) == 0) {                                                                                            \
        VS_LOG_ERROR("netif operation " #OPERATION " has not been called");                                            \
        goto terminate;                                                                                                \
    }


netif_state_t netif_state;
vs_mac_addr_t mac_addr_client_call;
vs_mac_addr_t mac_addr_server_call;
bool is_client_call;

static vs_netif_t *test_netif = NULL;
static vs_netif_rx_cb_t callback_rx_cb;

/**********************************************************/
static int
test_netif_tx(const uint8_t *data, const size_t data_sz) {
    int ret_code;

    is_client_call = !is_client_call;

    ret_code = callback_rx_cb(test_netif, data, data_sz);

    netif_state.sent = 1;

    return ret_code;
}

/**********************************************************/
static int
test_netif_mac_addr(struct vs_mac_addr_t *mac_addr) {

    *mac_addr = is_client_call ? mac_addr_client_call : mac_addr_server_call;

    netif_state.mac_addr_set_up = 1;

    return 0;
}

/**********************************************************/
static int
test_netif_init(const vs_netif_rx_cb_t rx_cb) {

    callback_rx_cb = rx_cb;

    netif_state.initialized = 1;

    return 0;
}

/**********************************************************/
static int
test_netif_deinit() {
    netif_state.deinitialized = 1;
    return 0;
}

/**********************************************************/
void
prepare_test_netif(vs_netif_t *netif) {

    test_netif = netif;

    netif->user_data = (void *)&netif_state;

    netif->tx = test_netif_tx;
    netif->mac_addr = test_netif_mac_addr;
    netif->init = test_netif_init;
    netif->deinit = test_netif_deinit;

    netif_state.membuf = 0;
}


static vs_netif_t netif;


static bool is_initialized;

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!is_initialized) {
        prepare_test_netif(&netif);
        vs_sdmp_init(&netif);
    }

    vs_sdmp_send(&netif, data, size);
    return 0;
}
