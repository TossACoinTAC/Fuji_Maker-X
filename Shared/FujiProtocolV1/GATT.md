# Fuji GATT v1

## UUIDs

| Item | UUID | Properties |
| --- | --- | --- |
| Service | `F157CFF7-0A18-4020-8CC0-CB1A0DA5BC22` | Primary service |
| Command | `43265B1A-2D59-40A3-BC4D-0E2FA73FFC20` | Phone write with response |
| Event | `FD1BA046-D0DD-415A-A757-A907E36AB912` | Device indicate |
| State | `6406E94B-8721-4CA7-ACE4-5E67D0AFD1FD` | Device read and notify |

All characteristics require an encrypted, authenticated bonded connection.
Audio is never carried by this service.

## Fragment header

Every characteristic value begins with this packed 14-byte header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | ASCII `FU` |
| 2 | 1 | Framing version, `1` |
| 3 | 1 | Flags: bit 0 start, bit 1 end; other bits zero |
| 4 | 4 | Transfer ID, little-endian |
| 8 | 2 | Zero-based chunk index, little-endian |
| 10 | 2 | Chunk count, little-endian |
| 12 | 2 | Total JSON byte length, little-endian |

The remaining bytes are the fragment payload. The maximum total length is
8192 bytes. A sender uses the peer's current ATT value limit and never assumes
that one write or indication can carry the full JSON message.

Chunks are sent in increasing index order. The receiver permits one in-flight
transfer per direction and accepts out-of-order delivery after the start chunk.
It rejects transfer collisions, duplicate chunks, invalid flags/counts/lengths,
and resets incomplete state after five seconds. EVENT chunks use indications so the device serializes delivery;
STATE updates may use notifications and are always replaceable by a read.
