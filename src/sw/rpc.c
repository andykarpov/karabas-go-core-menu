// Calls of the menu to the firmware over link v2 RPC: the request into the mailbox, the call, then
// the response (kg_l2_rpc map: rtl/soc/menu_soc.v, modules rtl/mcu/kg_l2_rpc.v).
#include "rpc.h"
#include "mmio.h"

static uint8_t next_id = 1;

int rpc_enabled(void) { return (RPC(RPC_FLAGS) & 8) != 0; }

static void put_req(uint8_t svc, uint8_t fn, const void *in, uint16_t in_len) {
  const uint8_t *p = (const uint8_t *) in;
  for (uint16_t i = 0; i < in_len; i++) RPC(RPC_REQ + i) = p[i];
  RPC(RPC_ID) = next_id;
  RPC(RPC_SVC) = svc;
  RPC(RPC_FN) = fn;
  RPC(RPC_LEN) = in_len & 0xFF;
  RPC(RPC_LEN + 1) = in_len >> 8;
}

int rpc_call(uint8_t svc, uint8_t fn, const void *in, uint16_t in_len, void *out, uint16_t out_max,
             uint16_t *out_len, uint32_t timeout_ms) {
  if (in_len > RPC_MAX) return RPC_ARGS;
  put_req(svc, fn, in, in_len);
  next_id++;
  RPC(RPC_CALL) = 1;
  uint32_t t0 = R_MS;
  while (!(RPC(RPC_FLAGS) & 2)) {
    if (R_MS - t0 > timeout_ms) return RPC_TIMEOUT;
  }
  uint16_t len = RPC(RPC_RLEN) | (RPC(RPC_RLEN + 1) << 8);
  if (len > out_max) len = out_max;
  uint8_t *q = (uint8_t *) out;
  for (uint16_t i = 0; i < len; i++) q[i] = RPC(RPC_RESP + i);
  if (out_len) *out_len = len;
  return RPC(RPC_RST);
}

void rpc_launch(const char *file) {
  uint16_t n = 0;
  while (file[n]) n++;
  put_req(RPC_CORES, 2, file, n + 1);
  R_LAUNCH = 1;  // the CPU stops here
  for (;;) ;
}
