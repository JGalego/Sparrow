# 3. Controller and HMI exchange raw structs in UDP datagrams

Status: accepted

## Context

The HMI runs as a separate process from the controller: on the desktop next to the simulator, and on the target possibly on another core or board. The link carries a 56-byte status frame at 10 Hz and occasional commands. It must be simple to implement in C and Python and must not block the HMI.

## Decision

Each datagram is a 16-byte header (magic, version, kind, sequence, payload size) followed by one generated struct copied verbatim (`sparrow/core/sp_frame.h`). Sockets are non-blocking UDP on a fixed port pair. The HMI declares the link lost after 2 s without a status frame.

## Consequences

- No serialization library and no schema compiler. The generated struct is the wire format.
- Both ends must share byte order and struct layout. The header refuses to compile on big-endian targets, and the generated code asserts every struct size.
- Delivery is best effort. Status frames are periodic, so a lost frame is replaced by the next one. A lost command is repeated by the operator. The controller is authoritative, so the HMI cannot put it in an unsafe state.
- A breaking layout change needs a new `SP_FRAME_VERSION`. Old peers then reject the frames instead of misreading them.
