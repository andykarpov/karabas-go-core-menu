# Link v2 target RPC

The spec is in karabas-go-firmware `docs/link_v2.md` section 5.9 (+ `link_v2.ru.md`, branch
`feature/rpc` until the release); the FPGA side is `kg_l2_rpc` of karabas-go-modules (branch
`feature/rpc`), the firmware side `src/fpga/link_api.c` (`link_rpc_*`) and `src/fpga/rpc_server.c`.
This file keeps the design notes and the services planned for later phases.

Done in Ф1 (2026-11-21), different from the first draft: the read of the sum of `REQ` ends the irq
(the request stays until its response, it can be read again); CFG `SET` writes the line at once
and `SAVE` only takes the settings into the firmware; the menu reaches the mailbox at MMIO
0x80002000 + 4 x byte (`src/sw/rpc.c`), `LAUNCH` through the MMIO register 0x14 (the clear, then
the call).

## 1. Why a mailbox

The MCU is the SPI master, so the core cannot call the MCU directly. A call of the core (its CPU)
is a **request** in a buffer of the core; the core raises its IRQ bit, the MCU reads the request,
does it and writes the **response** back. This is the IMG pattern (`EVENT` / `RDATA` / `DONE`) for
any service. The MCU can also push **events** to the core (unsolicited data).

- One call at a time (the CPU waits or polls for its response); payload up to 512 bytes each way.
- A call id (8 bits, the core counts it) ties a response to its request; a 16-bit sum on every
  request, response and event (as the IMG sectors).
- Generic on purpose: the boot menu uses SYS, CFG, CORES, HID; any core can use SYS, CFG, FS.

## 2. Place in link v2

- Target **`08` RPC**. IRQ bit **8**: `SYS IRQ` gets a second pending byte (bit 0 = RPC). An old
  firmware reads one byte; an old core gives 0 for the second (reading past the data gives 0).
- Caps (`SYS STATUS`) bit **10** = RPC.
- The core never raises IRQ bit 8 before `RPC HELLO` from the MCU, so an old firmware does not see
  INT held high by a target it does not know.
- `kg_l2_link` takes a 16-bit `irq` (bits 9..15 zero for now).

## 3. Commands (MCU side)

`→` from the MCU, `←` from the FPGA after the turnaround byte (`Z`).

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `HELLO` | firmware RPC version | core RPC version, max payload (2), services mask (2) | Enables IRQ bit 8. Sent once after `SYS STATUS` when caps bit 10 is set |
| `02` | `STATUS` | | flags, request length (2) | flags: bit 0 a request waits, bit 1 the event slot is free |
| `03` | `REQ` | | id, service, function, length (2), payload, sum (2) | Reading does not take the request; the response does |
| `04` | `RESP` | id, status, length (2), payload, sum (2) | Z, 0 taken / 1 refused | Refused: bad sum or another id; the MCU sends it again. Taken: IRQ bit 8 falls, the CPU sees "response ready" |
| `05` | `EVENT` | service, code, length (2), payload, sum (2) | Z, 0 taken / 1 refused | Only when `STATUS` bit 1; the CPU frees the slot when it has read it |

Response status: 0 ok, 1 unknown service / function, 2 bad arguments, 3 not found, 4 I/O error,
5 denied (sandbox), 6 busy (call again).

## 4. CPU side (`kg_l2_rpc` core ports)

A memory-mapped block (one RAMB16, 2 KB): the request buffer, the response buffer, the event
buffer, and a control register: `CALL` (write: the request is complete -> pending), `RESP_READY`,
`EVT_READY`, `EVT_DONE` (write), `ENABLED` (set by `HELLO`). A small C library in the menu wraps
it (`rpc_call(svc, fn, in, in_len, out, &out_len)`).

## 5. Services

### 00 SYS
| F | Name | In | Out |
|---|---|---|---|
| 00 | `INFO` | | firmware build (8), board, RPC version, the services of the firmware (mask) |
| 01 | `TIME_GET` | | year (2), month, day, hour, minute, second, weekday |
| 02 | `TIME_SET` | as `TIME_GET` | |
| 03 | `ACTION` | action | | 0 MSC (card), 1 MSC2 (SD2), 2 BOOTSEL, 3 reboot the MCU |

### 01 CFG (key / value settings)
| F | Name | In | Out |
|---|---|---|---|
| 00 | `GET` | file, section, key (NUL separated) | value |
| 01 | `SET` | file, section, key, value | | Staged in the MCU |
| 02 | `SAVE` | file | | Writes the staged values; for `karabas.ini` the firmware reloads its setup |

file: 0 `karabas.ini` (the boot core only), 1 the core's own `/config/<id>.ini`.

### 02 CORES (the boot core only)
| F | Name | In | Out |
|---|---|---|---|
| 00 | `SCAN` | | count (2) | In the order of `[setup] core_sort` |
| 01 | `ITEM` | index (2) | file name, name, build, id, flags (NUL separated) |
| 02 | `LAUNCH` | file name | | Sent with "clear, then call": the hardware fills the SRAM (the code is there too), then the request is pending; no response is read |
| 03 | `AUTOLOAD` | | timeout (s), file name | From `[autoload]` |

### 03 HID (the boot core only)
| F | Name | In | Out |
|---|---|---|---|
| 00 | `DEVICES` | | (slot, VID (2), PID (2), kind, name) per device |
| 01 | `LEARN_START` | slot | |
| 02 | `LEARN_STEP` | button | the rule as text, or status 6 while waiting |
| 03 | `LEARN_SAVE` | | | The mapping file of the firmware, reloaded at once |
| 04 | `LEARN_CANCEL` | | |

Events of HID: `code 1` a device plugged / unplugged (so the mapper list updates).

### 04 FS (Ф5, generic, sandboxed)

The files of SD1 (the card of the MCU). SD2 and CF are wired to the FPGA: a core reaches them
itself (the menu: FatFs on its CPU), not over RPC.

`OPEN` (path, mode) -> handle, `READ` (handle, length), `WRITE`, `SEEK` (handle, position (4)),
`CLOSE`, `STAT` (path) -> size (4), attributes, date, `DIR_OPEN`, `DIR_NEXT`, `MKDIR`, `DELETE`,
`RENAME`. Paths are inside the roots the core file allows (a `core.json` key, open item); the boot
core gets the whole card read only and `/config` read / write.

## 6. Firmware side

- Caps bit 10 of the boot core -> `HELLO`, no boot UI: the main loop serves IRQ bit 8 (one request
  per pass, as the IMG events).
- A boot core without bit 10 -> the fallback: a message in its bitmap OSD ("update boot.kg", BTN2
  for MSC).
- The keys, mouse and joysticks reach the boot core as now (HID target), with the OSD closed.
