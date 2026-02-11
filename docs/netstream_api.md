# NETStream Handler API (ASM + C)

This document describes the callable jump-table API exported by the NETStream handler binary (`NSENGINE.OBX`) and how to call it from assembly or C.

Base address:
- The handler is built at a fixed `BASEADDR` (Makefile `HANDLER_BASE`, currently `$2800`).
- The jump table starts at `BASEADDR` and each entry is a 3-byte `JMP`.

## Jump Table (BASEADDR + offset)

Offsets are in bytes from `BASEADDR`.

| Offset | Name | Inputs | Outputs | Notes |
|---:|---|---|---|---|
| +0  | `NS_BeginStream` | — | Y=1 | Installs IRQ vectors, asserts motor, enables serial IRQs.
| +3  | `NS_EndStream` | — | — | Restores IRQ vectors, deasserts motor, disables serial IRQs.
| +6  | `NS_GetVersion` | — | A=version | Currently `$01`.
| +9  | `NS_GetBase` | — | A=lo, X=hi | Returns base address.
| +12 | `NS_SendByte` | A=byte | C=0 ok / C=1 full | Enqueue one byte for TX.
| +15 | `NS_RecvByte` | — | A=byte, C=0 ok / C=1 empty | Dequeue one byte from RX.
| +18 | `NS_BytesAvail` | — | A=lo, X=hi | RX bytes available.
| +21 | `NS_GetStatus` | — | A=status | Sticky status (clear-on-read).
| +24 | `NS_GetVideoStd` | — | A=0 NTSC / 1 PAL | From VCOUNT detection.
| +27 | `NS_InitNetstream` | See below | A=0 ok / A=1 fail, C=0/1 | Sends FujiNet enable command and programs POKEY.
| +30 | `NS_GetFinalFlags` | — | A=flags | Final flags (PAL bit applied).
| +33 | `NS_GetFinalAUDF3` | — | A=AUDF3 | Selected divisor.
| +36 | `NS_GetFinalAUDF4` | — | A=AUDF4 | Selected divisor.

## Flags byte (NS_InitNetstream)

Bit meanings:
- `0x01` Transport select: 0=UDP, 1=TCP
- `0x02` REGISTER: 0=off, 1=on
- `0x04` TX clock source: 0=internal, 1=external
- `0x08` RX clock source: 0=internal, 1=external
- `0x10` Video standard: 0=NTSC, 1=PAL (set by handler)
- `0x20` UDP sequencing: 0=off, 1=on (only valid when UDP is selected)
- `0x40..0x80` Reserved

## C usage (cc65)

The repo ships `examples/common/netstream_api.s` which wraps the jump table with cc65 `__fastcall__` signatures. You can reuse those prototypes in C:

```
void __fastcall__ ns_begin_stream(void);
void __fastcall__ ns_end_stream(void);
unsigned char __fastcall__ ns_send_byte(unsigned char b);
int __fastcall__ ns_recv_byte(void);
unsigned int __fastcall__ ns_bytes_avail(void);
unsigned char __fastcall__ ns_init_netstream(const char* host,
                                             unsigned char flags,
                                             unsigned int nominal_baud,
                                             unsigned int port_swapped);
```

`ns_init_netstream()` returns 0 on success, 1 on failure.

Note: `port_swapped` is the port with bytes swapped (big-endian in AUX1/AUX2), e.g. `swap16(9000)`.

## ASM usage (direct)

You can call the jump table directly with `JSR BASEADDR+offset`. For most calls this is trivial; `NS_InitNetstream` is the only one with a C-oriented calling convention.

### Simple calls

```
        jsr BASEADDR+0   ; NS_BeginStream
        jsr BASEADDR+12  ; NS_SendByte, A=byte, C=0 ok / C=1 full
```

### NS_InitNetstream (ASM)

`NS_InitNetstream` expects arguments laid out like cc65 `__fastcall__`:
- A/X = port swapped (low/high)
- C-stack (c_sp at $82):
  - nominal baud (lo, hi)
  - flags (1 byte)
  - hostname pointer (lo, hi)

If you are not using cc65, the easiest approach is to write a small wrapper that sets up these bytes in a temporary buffer and sets `c_sp` to point at it before calling `NS_InitNetstream`.

Example outline (pseudo-ASM):

```
; buffer layout: [baud_lo][baud_hi][flags][host_lo][host_hi]
        lda #<args
        sta c_sp
        lda #>args
        sta c_sp+1

        lda #<port_swapped
        ldx #>port_swapped
        jsr BASEADDR+30  ; NS_InitNetstream
```

## Recommended flow

1) Load `NSENGINE.OBX` at `BASEADDR`.
2) Call `NS_InitNetstream`.
3) Call `NS_BeginStream`.
4) Use `NS_SendByte` / `NS_RecvByte` in your main loop.
5) Call `NS_EndStream` before exit.
