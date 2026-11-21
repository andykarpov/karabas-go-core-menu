// Calls of the menu to the firmware over link v2 RPC (kg_l2_rpc, karabas-go-firmware docs/link_v2.md 5.9).
#pragma once
#include <stdint.h>

enum { RPC_SYS = 0, RPC_CFG = 1, RPC_CORES = 2 };
enum { RPC_OK = 0, RPC_UNKNOWN = 1, RPC_ARGS = 2, RPC_NOT_FOUND = 3, RPC_IO = 4, RPC_DENIED = 5, RPC_BUSY = 6,
       RPC_TIMEOUT = 255 };
#define RPC_MAX 512

int rpc_enabled(void);   // the firmware said HELLO
// a call: in / in_len the payload of the request, the response into out (up to out_max bytes, its
// length in *out_len); returns the status of the response, RPC_TIMEOUT without one in timeout_ms
int rpc_call(uint8_t svc, uint8_t fn, const void *in, uint16_t in_len, void *out, uint16_t out_max,
             uint16_t *out_len, uint32_t timeout_ms);
// CORES LAUNCH: the SRAM is cleared, then the firmware loads the core; never returns
void rpc_launch(const char *file);
