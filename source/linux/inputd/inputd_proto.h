/* inputd_proto.h -- wire protocol shared by ahk-inputd and its clients.
 *
 * C2S frames are length-prefixed: u32 payload_len (payload length only), then
 * payload = u8 cmd + args.  S2C frames are fixed-size per cmd (no prefix):
 *   EVENT  = u8 1, u32 code, u8 value(0/1/2), u64 timestamp_us   (14 bytes)
 *   ACK    = u8 2, u8 ok, u32 detail                             (6 bytes)
 *   PONG   = u8 3                                                (1 byte)
 */
#ifndef AHK_INPUTD_PROTO_H
#define AHK_INPUTD_PROTO_H

#define INPUTD_PROTO_VERSION 1u

/* client -> broker */
#define INPUTD_C2S_HELLO 1u
#define INPUTD_C2S_SUBSCRIBE 2u
#define INPUTD_C2S_UNSUBSCRIBE 3u
#define INPUTD_C2S_PING 4u

/* broker -> client */
#define INPUTD_S2C_EVENT 1u
#define INPUTD_S2C_ACK 2u
#define INPUTD_S2C_PONG 3u

#define INPUTD_MAX_RULES 128
#define INPUTD_MAX_FRAME (4 + 1 + 4 + (INPUTD_MAX_RULES * 5))
#define INPUTD_DEFAULT_SOCKET_NAME "ahk-inputd.sock"

#endif /* AHK_INPUTD_PROTO_H */
