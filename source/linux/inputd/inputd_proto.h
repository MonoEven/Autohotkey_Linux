/* inputd_proto.h -- wire protocol shared by ahk-inputd and its clients.
 *
 * Protocol v1 (kept verbatim for old clients):
 *   C2S frames are length-prefixed: u32 payload_len (payload length only), then
 *   payload = u8 cmd + args.  S2C frames are fixed-size per cmd (no prefix):
 *     EVENT    = u8 1, u32 code, u8 value(0/1/2), u64 timestamp_us   (14 bytes)
 *     ACK      = u8 2, u8 ok, u32 detail                             (6 bytes)
 *     PONG     = u8 3                                                (1 byte)
 *     DEGRADED = u8 4                                                (1 byte)
 *                broadcast when the replay lane fails open: all grabs are
 *                released and the broker becomes observe/listen-only
 *                (check0901 P0-1).
 *
 * Protocol v2 (check0901 P0-3 / check_detail0901 §3, milestone M3):
 *   Explicit wire format discriminated by a 4-byte magic on the SAME socket.
 *   A v1 frame can never start with the magic: its leading u32 payload_len is
 *   bounded by INPUTD_MAX_FRAME (<< 0x324B4841), so the first four bytes
 *   unambiguously select the protocol (no length guessing).
 *
 *   v2 frame:
 *     magic        4 bytes  "AHK2" (= 0x324B4841u on a LE reader)
 *     version      u16 LE   = INPUTD_V2_PROTO_VERSION
 *     header_len   u16 LE   = INPUTD_V2_HEADER_LEN
 *     message_len  u32 LE   = header_len + payload_len
 *     message_type u16 LE
 *     flags        u16 LE
 *     request_id   u64 LE   (client-assigned correlation id)
 *     client_seq   u64 LE   (strictly increasing per connection)
 *     payload      message_len - header_len bytes
 *
 *   v2 EVENT payload = envelope (58 B) + tagged key payload (16 B).
 *   Every multi-byte field is serialized little-endian, byte by byte; the
 *   payloads are never memcpy'd from a C struct (check_detail0901 §3.2).
 */
#ifndef AHK_INPUTD_PROTO_H
#define AHK_INPUTD_PROTO_H

#define INPUTD_PROTO_VERSION 1u

/* client -> broker (v1) */
#define INPUTD_C2S_HELLO 1u
#define INPUTD_C2S_SUBSCRIBE 2u
#define INPUTD_C2S_UNSUBSCRIBE 3u
#define INPUTD_C2S_PING 4u

/* broker -> client (v1) */
#define INPUTD_S2C_EVENT 1u
#define INPUTD_S2C_ACK 2u
#define INPUTD_S2C_PONG 3u
#define INPUTD_S2C_BACKEND_DEGRADED 4u

#define INPUTD_MAX_RULES 1024
#define INPUTD_MAX_FRAME (4 + 1 + 4 + (INPUTD_MAX_RULES * 5))
#define INPUTD_DEFAULT_SOCKET_NAME "ahk-inputd.sock"

/* ---- protocol v2 (check0901 P0-3) ---------------------------------------- */

#define INPUTD_V2_MAGIC 0x324B4841u /* 'A','H','K','2' */
#define INPUTD_V2_PROTO_VERSION 2u
#define INPUTD_V2_HEADER_LEN 28u
#define INPUTD_V2_MAX_PAYLOAD 16384u
#define INPUTD_V2_MAX_FRAME (4 + INPUTD_V2_HEADER_LEN + INPUTD_V2_MAX_PAYLOAD)

/* message_type space (v2; direction-agnostic ids) */
#define INPUTD_V2_HELLO 1u            /* C2S */
#define INPUTD_V2_HELLO_ACK 2u        /* S2C */
#define INPUTD_V2_SUBSCRIBE 3u        /* C2S */
#define INPUTD_V2_SUBSCRIBE_ACK 4u    /* S2C */
#define INPUTD_V2_UNSUBSCRIBE 5u      /* C2S */
#define INPUTD_V2_UNSUBSCRIBE_ACK 6u  /* S2C */
#define INPUTD_V2_EVENT 7u            /* S2C */
#define INPUTD_V2_PING 8u             /* C2S */
#define INPUTD_V2_PONG 9u             /* S2C */
#define INPUTD_V2_ERROR 10u           /* S2C */
#define INPUTD_V2_DEVICE_ADDED 11u    /* S2C */
#define INPUTD_V2_DEVICE_REMOVED 12u  /* S2C */
#define INPUTD_V2_BACKEND_DEGRADED 13u /* S2C */
/* M4 (broker-owned injection / arbitration), declared but not granted yet:
 * DECISION_REQUEST/DECISION_REPLY, INJECT_BEGIN/EVENT/COMMIT/ABORT,
 * REPLACEMENT_ACK, CONFLICT, DEVICE_COVERAGE, BACKEND_HEALTH.  Clients that
 * send them today receive INPUTD_V2_ERROR / BAD_FRAME. */

/* capability bits (HELLO caps_requested / HELLO_ACK caps_granted|denied) */
#define INPUTD_V2_CAP_OBSERVE 0x1u
#define INPUTD_V2_CAP_SUPPRESS 0x2u
#define INPUTD_V2_CAP_EXCLUSIVE 0x4u /* M4 */
#define INPUTD_V2_CAP_INJECT 0x8u    /* M4 */

/* HELLO_ACK flags */
#define INPUTD_V2_ACK_FLAG_DEGRADED 0x1u

/* event provenance enums (envelope bytes) */
#define INPUTD_V2_SOURCE_PHYSICAL 0u
#define INPUTD_V2_SOURCE_SELF 1u
#define INPUTD_V2_SOURCE_OTHER 2u
#define INPUTD_V2_SOURCE_IME 3u
#define INPUTD_V2_SOURCE_UNKNOWN 4u

#define INPUTD_V2_ORIGIN_EVDEV 0u
#define INPUTD_V2_ORIGIN_UINPUT 1u
#define INPUTD_V2_ORIGIN_XTEST 2u
#define INPUTD_V2_ORIGIN_LIBEI 3u
#define INPUTD_V2_ORIGIN_TEXT 4u
#define INPUTD_V2_ORIGIN_UNKNOWN 5u

#define INPUTD_V2_CONF_AUTHORITATIVE 0u
#define INPUTD_V2_CONF_DEVICE_DERIVED 1u
#define INPUTD_V2_CONF_TIME_CORRELATED 2u
#define INPUTD_V2_CONF_UNKNOWN 3u

#define INPUTD_V2_PAYLOAD_KEY 0u
#define INPUTD_V2_PAYLOAD_POINTER 1u /* M4+ */
#define INPUTD_V2_PAYLOAD_TEXT 2u    /* M4+ */
#define INPUTD_V2_PAYLOAD_DEVICE 3u

#define INPUTD_V2_PHASE_DOWN 0u
#define INPUTD_V2_PHASE_UP 1u
#define INPUTD_V2_PHASE_REPEAT 2u

/* ERROR payload codes */
#define INPUTD_V2_ERR_PROTO_UNSUPPORTED 1u
#define INPUTD_V2_ERR_BAD_FRAME 2u
#define INPUTD_V2_ERR_NOT_HELLOED 3u
#define INPUTD_V2_ERR_CAPABILITY_DENIED 4u
#define INPUTD_V2_ERR_SEQUENCE_VIOLATION 5u
#define INPUTD_V2_ERR_DUPLICATE_HELLO 6u
#define INPUTD_V2_ERR_INTERNAL 7u

/* v2 payload layouts (byte offsets, little-endian) ------------------------- */

/* EVENT envelope, 58 bytes:
 *   0  authority_id[16]    broker instance identity (random per boot)
 *   16 generation u64      boot generation (changes on restart)
 *   24 event_seq u64       monotonic inside authority+generation
 *   32 timestamp_us u64    CLOCK_MONOTONIC capture time
 *   40 device_id u64       broker-assigned per grabbed device
 *   48 source u8           INPUTD_V2_SOURCE_*
 *   49 origin u8           INPUTD_V2_ORIGIN_*
 *   50 confidence u8       INPUTD_V2_CONF_*
 *   51 reserved u8
 *   52 send_level i16      -1 = not synthetic; >=0 authoritative level
 *   54 payload_kind u16    INPUTD_V2_PAYLOAD_*
 *   56 payload_len u16     payload bytes after the envelope
 */
#define INPUTD_V2_ENVELOPE_LEN 58u

/* KEY payload, 16 bytes:
 *   0  evdev_code u32
 *   4  vk u16              (0 = not mapped by the broker)
 *   6  sc u16              (0 = not mapped by the broker)
 *   8  unicode_scalar u32  (0 for broker lane key events)
 *   12 phase u8            INPUTD_V2_PHASE_*
 *   13 value u8            raw evdev value (0/1/2)
 *   14 reserved u16
 */
#define INPUTD_V2_KEY_PAYLOAD_LEN 16u

/* C2S HELLO payload, 30 bytes:
 *   0  min_proto u16
 *   2  max_proto u16
 *   4  nonce[16]           per-process random script nonce (not a PID)
 *   20 caps_requested u32
 *   24 event_schema u16    (=1)
 *   26 payload_kinds u16   bitmask INPUTD_V2_PAYLOAD_* (KEY=1 ...)
 *   28 max_rules u16
 */
#define INPUTD_V2_HELLO_PAYLOAD_LEN 30u

/* S2C HELLO_ACK payload, 52 bytes:
 *   0  proto u16           (= INPUTD_V2_PROTO_VERSION)
 *   2  client_id u64       broker-assigned, bound to THIS connection
 *   10 authority_id[16]
 *   26 generation u64
 *   34 caps_granted u32
 *   38 caps_denied u32
 *   42 seq_start u64       current broker event_seq at grant
 *   50 flags u16           INPUTD_V2_ACK_FLAG_*
 */
#define INPUTD_V2_HELLO_ACK_PAYLOAD_LEN 52u

/* C2S SUBSCRIBE payload: count u32 then count x { code u32, suppress u8 }.
 * S2C SUBSCRIBE_ACK payload, 5 bytes: ok u8, granted_count u32.
 * S2C UNSUBSCRIBE_ACK payload, 1 byte: ok u8.
 * S2C ERROR payload: code u32, detail_len u16, detail bytes (bounded).
 * S2C DEVICE_ADDED payload: device_id u64, name_len u16, name bytes.
 * S2C DEVICE_REMOVED payload: device_id u64.
 * S2C BACKEND_DEGRADED payload: none. */

#endif /* AHK_INPUTD_PROTO_H */
