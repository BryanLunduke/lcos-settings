// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LCOS_PULSE_CLIENT_HPP
#define LCOS_PULSE_CLIENT_HPP

#include <sigc++/signal.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lcos {

struct PaPort {
  std::string name;
  std::string description;
};

struct PaDevice {
  std::uint32_t index = 0;
  std::string name;
  std::string description;
  std::uint32_t card = 0;
  int channels = 2;
  int volume_percent = 0;
  bool mute = false;
  std::string active_port;
  std::vector<PaPort> ports;
};

struct PaInput {
  std::uint32_t index = 0;
  std::uint32_t sink = 0;
  std::string name;
  std::string application;
  int channels = 2;
  int volume_percent = 0;
  bool mute = false;
};

struct PaCard {
  std::uint32_t index = 0;
  std::string name;
  std::string description;
  std::string active_profile;
  std::vector<std::pair<std::string, std::string>> profiles;
};

struct PulseState {
  bool ready = false;
  std::string error;
  std::string default_sink;
  std::string default_source;
  std::vector<PaDevice> sinks;
  std::vector<PaDevice> sources;
  std::vector<PaInput> inputs;
  std::vector<PaCard> cards;
};

// PulseAudio client on the glib main loop. LCOS 1.0 uses PulseAudio 17,
// not PipeWire, as the sound server. Volumes are persisted by PulseAudio
// itself (~/.config/pulse), which is what pavucontrol writes.
class PulseClient {
 public:
  PulseClient();
  ~PulseClient();

  PulseClient(const PulseClient&) = delete;
  PulseClient& operator=(const PulseClient&) = delete;

  void connect();
  const PulseState& state() const { return state_; }
  sigc::signal<void> changed;

  void set_sink_volume(std::uint32_t index, int percent);
  void set_sink_mute(std::uint32_t index, bool mute);
  void set_sink_port(std::uint32_t index, const std::string& port);
  void set_default_sink(const std::string& name);

  void set_source_volume(std::uint32_t index, int percent);
  void set_source_mute(std::uint32_t index, bool mute);
  void set_source_port(std::uint32_t index, const std::string& port);
  void set_default_source(const std::string& name);

  void set_input_volume(std::uint32_t index, int percent);
  void set_input_mute(std::uint32_t index, bool mute);
  void set_card_profile(std::uint32_t index, const std::string& profile);

  // Used by the callbacks in the implementation file.
  void on_ready();
  void on_failed(const std::string& message);
  void note_server(const std::string& sink, const std::string& source);
  void add_sink(PaDevice device);
  void add_source(PaDevice device);
  void add_input(PaInput input);
  void add_card(PaCard card);
  void list_finished();

 private:
  void refresh();
  const PaDevice* find_sink(std::uint32_t index) const;
  const PaDevice* find_source(std::uint32_t index) const;
  const PaInput* find_input(std::uint32_t index) const;

  struct Impl;
  Impl* impl_ = nullptr;
  PulseState state_;
  PulseState incoming_;
  int pending_ = 0;
  bool refreshing_ = false;
  bool refresh_again_ = false;
};

}  // namespace lcos

#endif
