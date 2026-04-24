// src/rtcp_feedback.h
// ─────────────────────────────────────────────────────────────────────────────
// Custom RTCP APP packet for bandwidth feedback.
// Packet type PT=204 (APP), name="BWFB" (4 bytes, ASCII).
// All multi-byte fields are big-endian on the wire (network byte order).
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <cstdint>
#include <cstring>

namespace bwfb {

static constexpr char APP_NAME[4] = {'B','W','F','B'};

#pragma pack(push, 1)
struct FeedbackReport {
  uint32_t pkts_sent;       // cumulative RTP packets sent (host→client)
  uint32_t pkts_received;   // cumulative packets received by client
  uint32_t rtt_us;          // smoothed round-trip time in microseconds
  uint32_t jitter_us;       // inter-arrival jitter in microseconds (RFC 3550)
  uint32_t send_ts_us;      // client wall-clock at send time (for RTT calc)
  uint32_t reserved;        // pad to 24 bytes; set to zero
};
#pragma pack(pop)

static_assert(sizeof(FeedbackReport) == 24,
  "FeedbackReport must be exactly 24 bytes for RTCP APP alignment");

inline int serialize(uint8_t* buf, uint32_t ssrc, const FeedbackReport& r) {
  buf[0] = 0x80; buf[1] = 204; buf[2] = 0x00; buf[3] = 0x07;
  buf[4] = (ssrc >> 24) & 0xFF; buf[5] = (ssrc >> 16) & 0xFF;
  buf[6] = (ssrc >>  8) & 0xFF; buf[7] = (ssrc      ) & 0xFF;
  std::memcpy(buf + 8, APP_NAME, 4);
  auto* p = reinterpret_cast<uint32_t*>(buf + 12);
  p[0] = htonl(r.pkts_sent);   p[1] = htonl(r.pkts_received);
  p[2] = htonl(r.rtt_us);      p[3] = htonl(r.jitter_us);
  p[4] = htonl(r.send_ts_us);  p[5] = 0;
  return 32;
}

inline bool deserialize(const uint8_t* buf, int len, FeedbackReport& out) {
  if (len < 32) return false;
  if (buf[1] != 204) return false;
  if (std::memcmp(buf + 8, APP_NAME, 4) != 0) return false;
  const auto* p = reinterpret_cast<const uint32_t*>(buf + 12);
  out.pkts_sent     = ntohl(p[0]); out.pkts_received = ntohl(p[1]);
  out.rtt_us        = ntohl(p[2]); out.jitter_us     = ntohl(p[3]);
  out.send_ts_us    = ntohl(p[4]); out.reserved      = 0;
  return true;
}

} // namespace bwfb