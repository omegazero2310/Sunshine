// src/bw_controller.h
// ─────────────────────────────────────────────────────────────────────────────
// Bandwidth controller — runs on the server (Sunshine).
// Takes FeedbackReports from the Moonlight client every ~200ms and outputs
// a smoothed target bitrate in bits-per-second.
// Thread-safe: read from encode thread, written from RTCP receive thread.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <atomic>
#include <cstdint>
#include <algorithm>

class BandwidthController {
public:
  BandwidthController(uint32_t initial_bps, uint32_t min_bps, uint32_t max_bps)
    : _target_bps(initial_bps), _smoothed_bps(initial_bps),
      _min_bps(min_bps), _max_bps(max_bps),
      _prev_pkts_sent(0), _prev_pkts_recv(0),
      _delay_trend(0.0f), _state(State::HOLD), _hold_count(0) {}

  uint32_t update(uint32_t pkts_sent, uint32_t pkts_recv,
                  uint32_t rtt_us, uint32_t jitter_us) {

    // ── Loss fraction over the last interval ─────────────────────────────────
    uint32_t delta_sent = pkts_sent - _prev_pkts_sent;
    uint32_t delta_recv = pkts_recv - _prev_pkts_recv;
    _prev_pkts_sent = pkts_sent;
    _prev_pkts_recv = pkts_recv;

    float loss_frac = 0.0f;
    if (delta_sent > 0) {
      uint32_t lost = (delta_recv > delta_sent) ? 0 : (delta_sent - delta_recv);
      loss_frac = static_cast<float>(lost) / static_cast<float>(delta_sent);
    }

    // ── Delay trend — rising RTT signals queue buildup before loss occurs ────
    float rtt_ms = static_cast<float>(rtt_us) / 1000.0f;
    if (_prev_rtt_ms > 0.0f) {
      float d = rtt_ms - _prev_rtt_ms;
      _delay_trend = 0.85f * _delay_trend + 0.15f * d;
    }
    _prev_rtt_ms = rtt_ms;

    // ── State machine ─────────────────────────────────────────────────────────
    if (loss_frac > 0.05f || _delay_trend > 15.0f) {
      _target_bps = static_cast<uint32_t>(_target_bps * 0.75f);
      _state = State::DECREASE;
      _hold_count = 0;
    } else if (loss_frac > 0.01f || _delay_trend > 5.0f) {
      if (_state != State::HOLD) { _state = State::HOLD; _hold_count = 0; }
      ++_hold_count;
    } else {
      if (_state == State::DECREASE || (_state == State::HOLD && _hold_count < 10)) {
        ++_hold_count;
      } else {
        _target_bps = static_cast<uint32_t>(_target_bps * 1.05f);
        _state = State::INCREASE;
      }
    }

    _target_bps = std::clamp(_target_bps, _min_bps, _max_bps);

    // ── IIR smooth — prevents step changes reaching the encoder ──────────────
    float s = 0.75f * static_cast<float>(_smoothed_bps)
            + 0.25f * static_cast<float>(_target_bps);
    _smoothed_bps.store(static_cast<uint32_t>(s), std::memory_order_relaxed);
    return _smoothed_bps.load(std::memory_order_relaxed);
  }

  uint32_t current_target_bps() const {
    return _smoothed_bps.load(std::memory_order_relaxed);
  }

  void set_max_bps(uint32_t max_bps) { _max_bps = max_bps; }

private:
  enum class State { INCREASE, HOLD, DECREASE };

  uint32_t              _target_bps;
  std::atomic<uint32_t> _smoothed_bps;
  uint32_t              _min_bps, _max_bps;
  uint32_t              _prev_pkts_sent, _prev_pkts_recv;
  float                 _delay_trend, _prev_rtt_ms = 0.0f;
  State                 _state;
  int                   _hold_count;
};