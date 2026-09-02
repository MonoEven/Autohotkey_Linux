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
/* M4 broker-owned injection (check0901 P0-3 §3.3C): a client submits a
 * transaction plan, the broker validates it, submits the events to its own
 * output device and publishes the same transaction's normalized synthetic
 * events on the internal stream with authoritative provenance. */
#define INPUTD_V2_INJECT_BEGIN 14u    /* C2S */
#define INPUTD_V2_INJECT_EVENT 15u    /* C2S */
#define INPUTD_V2_INJECT_COMMIT 16u   /* C2S */
#define INPUTD_V2_INJECT_ABORT 17u    /* C2S */
#define INPUTD_V2_INJECT_ACK 18u      /* S2C */
/* M4b static arbitration (check_detail0901 §7): registration/lease ownership
 * and decision traces.  Dynamic HotIf DECISION_REQUEST/REPLY is intentionally
 * deferred to M5's unified event pipeline; keyboard default remains fail-open. */
#define INPUTD_V2_ARB_REGISTER 19u    /* C2S */
#define INPUTD_V2_ARB_REGISTER_ACK 20u /* S2C */
#define INPUTD_V2_ARB_UNREGISTER 21u  /* C2S */
#define INPUTD_V2_CONFLICT 22u        /* S2C */
#define INPUTD_V2_ARB_DECISION 23u    /* S2C */
#define INPUTD_V2_BACKEND_HEALTH 24u  /* S2C */
#define INPUTD_V2_DECISION_REQUEST 25u /* S2C: dynamic HotIf */
#define INPUTD_V2_DECISION_REPLY 26u   /* C2S */

/* capability bits (HELLO caps_requested / HELLO_ACK caps_granted|denied) */
#define INPUTD_V2_CAP_OBSERVE 0x1u
#define INPUTD_V2_CAP_SUPPRESS 0x2u
#define INPUTD_V2_CAP_EXCLUSIVE 0x4u /* P1-4 */
#define INPUTD_V2_CAP_INJECT 0x8u    /* M4: owner/root only */

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

/* EVENT envelope, 82 bytes:
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
 *   58 producer_client_id u64   0 for physical events
 *   66 transaction_id u64       0 for physical events
 *   74 parent_transaction_id u64 0 unless part of a remap chain (M4+)
 */
#define INPUTD_V2_ENVELOPE_LEN 82u

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

/* M4 injection transaction (check0901 P0-3 §3.3C):
 * C2S INJECT_BEGIN payload, 26 bytes:
 *   0  transaction_id u64  client-chosen, unique per client+connection
 *   8  send_level i16      0..100 (synthetic events always carry a level)
 *   10 flags u16           0 for now
 *   12 ttl_ms u16          0 = broker default (2000); transaction deadline
 *   14 event_count u32     preflight: total INJECT_EVENT frames expected
 *   18 parent_transaction_id u64  0 = top-level transaction
 * C2S INJECT_EVENT payload, 24 bytes: transaction_id u64 + KEY payload 16.
 * C2S INJECT_COMMIT / INJECT_ABORT payload, 8 bytes: transaction_id u64.
 * S2C INJECT_ACK payload: transaction_id u64, status u8, detail_len u16,
 *   detail bytes (bounded).  Status codes: */
#define INPUTD_V2_INJECT_BEGIN_PAYLOAD_LEN 26u
#define INPUTD_V2_INJECT_OK_BEGIN 0u
#define INPUTD_V2_INJECT_OK_COMMIT 1u
#define INPUTD_V2_INJECT_OK_ABORT 2u
#define INPUTD_V2_INJECT_OK_EVENT 8u    /* one INJECT_EVENT accepted */
#define INPUTD_V2_INJECT_STALE 3u      /* unknown txn id (restart/replay) */
#define INPUTD_V2_INJECT_DENIED 4u     /* capability not granted */
#define INPUTD_V2_INJECT_QUOTA 5u      /* too many concurrent transactions */
#define INPUTD_V2_INJECT_BAD_FRAME 6u  /* malformed payload / bad event_count */
#define INPUTD_V2_INJECT_DEGRADED 7u   /* replay lane unavailable */

/* M4 injection quotas (check_detail0901 §3.5) */
#define INPUTD_V2_INJECT_MAX_PER_CLIENT 4u
#define INPUTD_V2_INJECT_MAX_TOTAL 16u
#define INPUTD_V2_INJECT_MAX_EVENTS 256u
#define INPUTD_V2_INJECT_DEFAULT_TTL_MS 2000u

/* M4b static arbitration registrations (check_detail0901 §7):
 * C2S ARB_REGISTER payload, 30 bytes:
 *   0  registration_id u64   client-chosen, unique per connection
 *   8  code u32              KEY_* selector (keyboard-only v2 schema)
 *   12 mode u8               OBSERVE/SUPPRESS/EXCLUSIVE/REMAP
 *   13 conflict_policy u8    REJECT or PREEMPT_LOWER
 *   14 priority i16          higher wins inside one authorized principal
 *   16 input_level i16       documented policy metadata (0..100)
 *   18 replacement_send_level i16 (REMAP output; 0..100)
 *   20 lease_ms u32          0=30s default; bounded to 300s
 *   24 replacement_code u32  REMAP only, otherwise 0
 *   28 flags u16             0 for now
 * C2S ARB_UNREGISTER payload, 8 bytes: registration_id u64.
 * S2C ARB_REGISTER_ACK payload, 33 bytes:
 *   registration_id u64, status u8, owner_registration_id u64,
 *   acceptance_seq u64, lease_expiry_ms u64.
 * S2C CONFLICT payload, 33 bytes:
 *   requested_id u64, owner_registration_id u64, owner_client_id u64,
 *   code u32, reason u8, owner_priority i16, requester_priority i16.
 * S2C ARB_DECISION payload, 40 bytes:
 *   source_event_seq u64, source_transaction_id u64, code u32,
 *   action u8, reason u8, winner_registration_id u64,
 *   replacement_transaction_id u64, winner_priority i16.
 */
#define INPUTD_V2_ARB_REGISTER_PAYLOAD_LEN 30u
#define INPUTD_V2_ARB_REGISTER_ACK_PAYLOAD_LEN 33u
#define INPUTD_V2_CONFLICT_PAYLOAD_LEN 33u
#define INPUTD_V2_ARB_DECISION_PAYLOAD_LEN 40u

#define INPUTD_V2_ARB_OBSERVE 0u
#define INPUTD_V2_ARB_SUPPRESS 1u
#define INPUTD_V2_ARB_EXCLUSIVE 2u
#define INPUTD_V2_ARB_REMAP 3u

#define INPUTD_V2_ARB_CONFLICT_REJECT 0u
#define INPUTD_V2_ARB_CONFLICT_PREEMPT_LOWER 1u

#define INPUTD_V2_ARB_GRANTED 0u
#define INPUTD_V2_ARB_REFRESHED 1u
#define INPUTD_V2_ARB_UNREGISTERED 2u
#define INPUTD_V2_ARB_CONFLICTED 3u
#define INPUTD_V2_ARB_DENIED 4u
#define INPUTD_V2_ARB_BAD_FRAME 5u
#define INPUTD_V2_ARB_QUOTA 6u
#define INPUTD_V2_ARB_EXPIRED 7u

#define INPUTD_V2_CONFLICT_OWNER_EXISTS 0u
#define INPUTD_V2_CONFLICT_PREEMPTED 1u
#define INPUTD_V2_CONFLICT_LEASE_EXPIRED 2u
#define INPUTD_V2_CONFLICT_OWNER_GONE 3u
#define INPUTD_V2_CONFLICT_CROSS_UID 4u
#define INPUTD_V2_CONFLICT_SLOW_CLIENT 5u

#define INPUTD_V2_DECISION_REPLAY 0u
#define INPUTD_V2_DECISION_SUPPRESS 1u
#define INPUTD_V2_DECISION_REMAP 2u
#define INPUTD_V2_DECISION_REPLACEMENT_FAILED 3u

#define INPUTD_V2_DECISION_NONE 0u
#define INPUTD_V2_DECISION_LEGACY_SUPPRESS 1u
#define INPUTD_V2_DECISION_STATIC_SUPPRESS 2u
#define INPUTD_V2_DECISION_EXCLUSIVE 3u
#define INPUTD_V2_DECISION_STATIC_REMAP 4u
#define INPUTD_V2_DECISION_STICKY_KEYUP 5u
#define INPUTD_V2_DECISION_DYNAMIC_TRUE 6u
#define INPUTD_V2_DECISION_DYNAMIC_FALSE 7u
#define INPUTD_V2_DECISION_DYNAMIC_TIMEOUT 8u
#define INPUTD_V2_DECISION_DYNAMIC_DISCONNECT 9u
#define INPUTD_V2_DECISION_DYNAMIC_SLOW_DOWNGRADE 10u

#define INPUTD_V2_ARB_FLAG_DYNAMIC_DECISION 0x1u
#define INPUTD_V2_DYNAMIC_PASS 0u
#define INPUTD_V2_DYNAMIC_SUPPRESS 1u
#define INPUTD_V2_DYNAMIC_DEADLINE_MS 60u
#define INPUTD_V2_DYNAMIC_SLOW_LIMIT 3u
/* DECISION_REQUEST payload, 46 bytes: event_seq u64, source_txn u64,
 * registration_id u64, code u32, value u8, reserved u8,
 * deadline_us u64, registration_acceptance_seq u64.
 * DECISION_REPLY payload, 33 bytes: event_seq u64, source_txn u64,
 * registration_id u64, registration_acceptance_seq u64, action u8. */
#define INPUTD_V2_DECISION_REQUEST_PAYLOAD_LEN 46u
#define INPUTD_V2_DECISION_REPLY_PAYLOAD_LEN 33u

#define INPUTD_V2_ARB_MAX_RULES 256u
#define INPUTD_V2_ARB_MAX_PER_CLIENT 64u
#define INPUTD_V2_ARB_DEFAULT_LEASE_MS 30000u
#define INPUTD_V2_ARB_MAX_LEASE_MS 300000u

/* M2 BACKEND_HEALTH payload, base 50 bytes + bounded UTF-8 reason:
 *   state u8, permission u8, flags u16,
 *   authority_generation u64, health_seq u64, last_success_us u64,
 *   last_errno i32, device_count u32, grabbed_count u32,
 *   registration_count u32, active_transaction_count u32,
 *   reason_len u16, reason bytes.
 */
#define INPUTD_V2_HEALTH_BASE_LEN 50u
#define INPUTD_V2_HEALTH_FLAG_REPLAY_AVAILABLE 0x1u
#define INPUTD_V2_HEALTH_FLAG_REGISTRATIONS_RECONCILED 0x2u
#define INPUTD_V2_HEALTH_FLAG_HELD_STATE_RECONCILED 0x4u
#define INPUTD_V2_HEALTH_FLAG_AUTHORITATIVE 0x8u

#define INPUTD_V2_HEALTH_UNINITIALIZED 0u
#define INPUTD_V2_HEALTH_PROBING 1u
#define INPUTD_V2_HEALTH_AVAILABLE 2u
#define INPUTD_V2_HEALTH_BINDING 3u
#define INPUTD_V2_HEALTH_HEALTHY 4u
#define INPUTD_V2_HEALTH_DEGRADED 5u
#define INPUTD_V2_HEALTH_DISCONNECTED 6u
#define INPUTD_V2_HEALTH_RETRY_WAIT 7u
#define INPUTD_V2_HEALTH_RESUBSCRIBING 8u
#define INPUTD_V2_HEALTH_RECONCILING_STATE 9u
#define INPUTD_V2_HEALTH_PERMISSION_DENIED 10u
#define INPUTD_V2_HEALTH_UNSUPPORTED 11u
#define INPUTD_V2_HEALTH_REAUTH_REQUIRED 12u
#define INPUTD_V2_HEALTH_SHUTDOWN 13u

#define INPUTD_V2_PERMISSION_UNKNOWN 0u
#define INPUTD_V2_PERMISSION_GRANTED 1u
#define INPUTD_V2_PERMISSION_DENIED 2u
#define INPUTD_V2_PERMISSION_REAUTH_REQUIRED 3u

#endif /* AHK_INPUTD_PROTO_H */
