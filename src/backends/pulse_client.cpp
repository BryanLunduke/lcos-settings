// SPDX-License-Identifier: GPL-3.0-or-later

#include "pulse_client.hpp"

#include "logic/volume.hpp"

#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace lcos {
namespace {

void unref_op(pa_operation* operation) {
  if (operation != nullptr) {
    pa_operation_unref(operation);
  }
}

void sink_cb(pa_context*, const pa_sink_info* info, int eol, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  if (eol != 0) {
    self->list_finished();
    return;
  }
  if (info == nullptr || info->name == nullptr) {
    return;
  }
  PaDevice device;
  device.index = info->index;
  device.name = info->name;
  device.description = info->description != nullptr ? info->description : info->name;
  device.card = info->card;
  device.channels = std::max(1, static_cast<int>(info->volume.channels));
  device.volume_percent = volume_to_percent(pa_cvolume_max(&info->volume));
  device.mute = info->mute != 0;
  for (uint8_t channel = 0; channel < info->volume.channels; ++channel) {
    device.channel_volumes.push_back(info->volume.values[channel]);
  }
  if (info->active_port != nullptr && info->active_port->name != nullptr) {
    device.active_port = info->active_port->name;
  }
  if (info->ports != nullptr) {
    for (pa_sink_port_info** port = info->ports; *port != nullptr; ++port) {
      if ((*port)->name == nullptr) {
        continue;
      }
      PaPort stored;
      stored.name = (*port)->name;
      stored.description =
          (*port)->description != nullptr ? (*port)->description : (*port)->name;
      device.ports.push_back(std::move(stored));
    }
  }
  self->add_sink(std::move(device));
}

void source_cb(pa_context*, const pa_source_info* info, int eol, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  if (eol != 0) {
    self->list_finished();
    return;
  }
  if (info == nullptr || info->name == nullptr) {
    return;
  }
  if (info->monitor_of_sink != PA_INVALID_INDEX) {
    return;
  }
  PaDevice device;
  device.index = info->index;
  device.name = info->name;
  device.description = info->description != nullptr ? info->description : info->name;
  device.card = info->card;
  device.channels = std::max(1, static_cast<int>(info->volume.channels));
  device.volume_percent = volume_to_percent(pa_cvolume_max(&info->volume));
  device.mute = info->mute != 0;
  for (uint8_t channel = 0; channel < info->volume.channels; ++channel) {
    device.channel_volumes.push_back(info->volume.values[channel]);
  }
  if (info->active_port != nullptr && info->active_port->name != nullptr) {
    device.active_port = info->active_port->name;
  }
  if (info->ports != nullptr) {
    for (pa_source_port_info** port = info->ports; *port != nullptr; ++port) {
      if ((*port)->name == nullptr) {
        continue;
      }
      PaPort stored;
      stored.name = (*port)->name;
      stored.description =
          (*port)->description != nullptr ? (*port)->description : (*port)->name;
      device.ports.push_back(std::move(stored));
    }
  }
  self->add_source(std::move(device));
}

void input_cb(pa_context*, const pa_sink_input_info* info, int eol, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  if (eol != 0) {
    self->list_finished();
    return;
  }
  if (info == nullptr) {
    return;
  }
  PaInput input;
  input.index = info->index;
  input.sink = info->sink;
  input.name = info->name != nullptr ? info->name : "Audio stream";
  input.channels = std::max(1, static_cast<int>(info->volume.channels));
  input.volume_percent = volume_to_percent(pa_cvolume_max(&info->volume));
  input.mute = info->mute != 0;
  for (uint8_t channel = 0; channel < info->volume.channels; ++channel) {
    input.channel_volumes.push_back(info->volume.values[channel]);
  }
  if (info->proplist != nullptr) {
    const char* app = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
    if (app != nullptr) {
      input.application = app;
    }
  }
  if (input.application.empty()) {
    input.application = input.name;
  }
  self->add_input(std::move(input));
}

void capture_cb(pa_context*, const pa_source_output_info* info, int eol, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  if (eol != 0) {
    self->list_finished();
    return;
  }
  if (info == nullptr) {
    return;
  }
  PaCapture capture;
  capture.index = info->index;
  capture.source = info->source;
  capture.name = info->name != nullptr ? info->name : "Recording";
  capture.channels = std::max(1, static_cast<int>(info->volume.channels));
  capture.volume_percent = volume_to_percent(pa_cvolume_max(&info->volume));
  capture.mute = info->mute != 0;
  for (uint8_t channel = 0; channel < info->volume.channels; ++channel) {
    capture.channel_volumes.push_back(info->volume.values[channel]);
  }
  if (info->proplist != nullptr) {
    const char* app = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
    if (app != nullptr) {
      capture.application = app;
    }
  }
  if (capture.application.empty()) {
    capture.application = capture.name;
  }
  self->add_capture(std::move(capture));
}

void peak_cb(pa_stream* stream, size_t, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  const void* data = nullptr;
  size_t bytes = 0;
  while (pa_stream_peek(stream, &data, &bytes) == 0 && bytes > 0) {
    float level = 0;
    if (data != nullptr && bytes >= sizeof(float)) {
      std::memcpy(&level, data, sizeof(float));
    }
    pa_stream_drop(stream);
    if (level < 0) {
      level = 0;
    }
    if (level > 1) {
      level = 1;
    }
    self->note_peak(level);
  }
}

void card_cb(pa_context*, const pa_card_info* info, int eol, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  if (eol != 0) {
    self->list_finished();
    return;
  }
  if (info == nullptr || info->name == nullptr) {
    return;
  }
  PaCard card;
  card.index = info->index;
  card.name = info->name;
  card.description =
      info->proplist != nullptr
          ? (pa_proplist_gets(info->proplist, PA_PROP_DEVICE_DESCRIPTION) != nullptr
                 ? pa_proplist_gets(info->proplist, PA_PROP_DEVICE_DESCRIPTION)
                 : info->name)
          : info->name;
  if (info->active_profile2 != nullptr && info->active_profile2->name != nullptr) {
    card.active_profile = info->active_profile2->name;
  }
  if (info->profiles2 != nullptr) {
    for (pa_card_profile_info2** profile = info->profiles2; *profile != nullptr; ++profile) {
      if ((*profile)->name == nullptr) {
        continue;
      }
      const char* description =
          (*profile)->description != nullptr ? (*profile)->description : (*profile)->name;
      card.profiles.emplace_back((*profile)->name, description);
    }
  }
  self->add_card(std::move(card));
}

void server_cb(pa_context*, const pa_server_info* info, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  if (info != nullptr) {
    self->note_server(info->default_sink_name != nullptr ? info->default_sink_name : "",
                      info->default_source_name != nullptr ? info->default_source_name : "");
  }
  self->list_finished();
}

void subscribe_cb(pa_context*, pa_subscription_event_type_t, uint32_t, void* user) {
  static_cast<PulseClient*>(user)->on_ready();
}

void state_cb(pa_context* context, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY:
      pa_context_set_subscribe_callback(context, subscribe_cb, self);
      unref_op(pa_context_subscribe(context,
                                    static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_ALL),
                                    nullptr, nullptr));
      self->on_ready();
      break;
    case PA_CONTEXT_FAILED:
      self->on_failed(pa_strerror(pa_context_errno(context)));
      break;
    case PA_CONTEXT_TERMINATED:
      self->on_failed("PulseAudio disconnected");
      break;
    default:
      break;
  }
}

pa_cvolume volume_from_channels(const std::vector<std::uint32_t>& volumes) {
  pa_cvolume volume;
  pa_cvolume_init(&volume);
  const unsigned count =
      std::min<unsigned>(static_cast<unsigned>(volumes.size()), PA_CHANNELS_MAX);
  volume.channels = static_cast<uint8_t>(std::max(1u, count));
  if (volumes.empty()) {
    volume.values[0] = PA_VOLUME_NORM;
    return volume;
  }
  for (unsigned i = 0; i < count; ++i) {
    volume.values[i] = volumes[i];
  }
  return volume;
}

}  // namespace

struct PulseClient::Impl {
  pa_glib_mainloop* loop = nullptr;
  pa_context* context = nullptr;
  pa_stream* peak = nullptr;
  std::string peak_source;
};

PulseClient::PulseClient() : impl_(new Impl) {}

PulseClient::~PulseClient() {
  if (impl_->peak != nullptr) {
    pa_stream_set_read_callback(impl_->peak, nullptr, nullptr);
    pa_stream_disconnect(impl_->peak);
    pa_stream_unref(impl_->peak);
    impl_->peak = nullptr;
  }
  if (impl_->context != nullptr) {
    // Drop callbacks before disconnect so a synchronous TERMINATED event
    // cannot call back into a client that is already going away.
    pa_context_set_state_callback(impl_->context, nullptr, nullptr);
    pa_context_set_subscribe_callback(impl_->context, nullptr, nullptr);
    pa_context_disconnect(impl_->context);
    pa_context_unref(impl_->context);
    impl_->context = nullptr;
  }
  if (impl_->loop != nullptr) {
    pa_glib_mainloop_free(impl_->loop);
  }
  delete impl_;
}

void PulseClient::connect() {
  if (impl_->context != nullptr) {
    const pa_context_state_t state = pa_context_get_state(impl_->context);
    if (state != PA_CONTEXT_FAILED && state != PA_CONTEXT_TERMINATED) {
      return;
    }
    pa_context_set_state_callback(impl_->context, nullptr, nullptr);
    pa_context_set_subscribe_callback(impl_->context, nullptr, nullptr);
    pa_context_unref(impl_->context);
    impl_->context = nullptr;
    pending_ = 0;
    refreshing_ = false;
    refresh_again_ = false;
    if (impl_->loop != nullptr) {
      pa_glib_mainloop_free(impl_->loop);
      impl_->loop = nullptr;
    }
  }
  impl_->loop = pa_glib_mainloop_new(g_main_context_default());
  pa_mainloop_api* api = pa_glib_mainloop_get_api(impl_->loop);
  impl_->context = pa_context_new(api, "LCOS Command Center");
  pa_context_set_state_callback(impl_->context, state_cb, this);
  if (pa_context_connect(impl_->context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
    on_failed(pa_strerror(pa_context_errno(impl_->context)));
  }
}

void PulseClient::on_ready() { refresh(); }

void PulseClient::on_failed(const std::string& message) {
  pending_ = 0;
  refreshing_ = false;
  refresh_again_ = false;
  state_.ready = false;
  state_.error = message;
  changed.emit();
}

void PulseClient::note_server(const std::string& sink, const std::string& source) {
  incoming_.default_sink = sink;
  incoming_.default_source = source;
}

void PulseClient::add_sink(PaDevice device) { incoming_.sinks.push_back(std::move(device)); }
void PulseClient::add_source(PaDevice device) {
  incoming_.sources.push_back(std::move(device));
}
void PulseClient::add_input(PaInput input) { incoming_.inputs.push_back(std::move(input)); }
void PulseClient::add_capture(PaCapture capture) {
  incoming_.captures.push_back(std::move(capture));
}
void PulseClient::note_peak(float level) { peak.emit(level); }
void PulseClient::add_card(PaCard card) { incoming_.cards.push_back(std::move(card)); }

void PulseClient::list_finished() {
  if (!refreshing_) {
    return;
  }
  if (pending_ > 0) {
    --pending_;
  }
  if (pending_ > 0) {
    return;
  }
  incoming_.ready = true;
  incoming_.error.clear();
  state_ = incoming_;
  refreshing_ = false;
  changed.emit();
  if (refresh_again_) {
    refresh_again_ = false;
    refresh();
  }
}

void PulseClient::refresh() {
  if (impl_->context == nullptr ||
      pa_context_get_state(impl_->context) != PA_CONTEXT_READY) {
    return;
  }
  if (refreshing_) {
    refresh_again_ = true;
    return;
  }
  refreshing_ = true;
  incoming_ = {};
  pending_ = 6;
  unref_op(pa_context_get_server_info(impl_->context, server_cb, this));
  unref_op(pa_context_get_sink_info_list(impl_->context, sink_cb, this));
  unref_op(pa_context_get_source_info_list(impl_->context, source_cb, this));
  unref_op(pa_context_get_sink_input_info_list(impl_->context, input_cb, this));
  unref_op(pa_context_get_source_output_info_list(impl_->context, capture_cb, this));
  unref_op(pa_context_get_card_info_list(impl_->context, card_cb, this));
}

const PaDevice* PulseClient::find_sink(std::uint32_t index) const {
  for (const auto& device : state_.sinks) {
    if (device.index == index) {
      return &device;
    }
  }
  return nullptr;
}

const PaDevice* PulseClient::find_source(std::uint32_t index) const {
  for (const auto& device : state_.sources) {
    if (device.index == index) {
      return &device;
    }
  }
  return nullptr;
}

const PaInput* PulseClient::find_input(std::uint32_t index) const {
  for (const auto& input : state_.inputs) {
    if (input.index == index) {
      return &input;
    }
  }
  return nullptr;
}

const PaCapture* PulseClient::find_capture(std::uint32_t index) const {
  for (const auto& capture : state_.captures) {
    if (capture.index == index) {
      return &capture;
    }
  }
  return nullptr;
}

void PulseClient::set_sink_volume(std::uint32_t index, int percent) {
  const PaDevice* device = find_sink(index);
  if (device == nullptr) {
    return;
  }
  set_sink_volumes(index, scale_channel_volumes(device->channel_volumes, percent));
}

void PulseClient::set_sink_volumes(std::uint32_t index, const std::vector<std::uint32_t>& volumes) {
  if (impl_->context == nullptr || volumes.empty()) {
    return;
  }
  const pa_cvolume volume = volume_from_channels(volumes);
  unref_op(pa_context_set_sink_volume_by_index(impl_->context, index, &volume, nullptr, nullptr));
}

void PulseClient::set_sink_mute(std::uint32_t index, bool mute) {
  if (impl_->context == nullptr) {
    return;
  }
  unref_op(pa_context_set_sink_mute_by_index(impl_->context, index, mute ? 1 : 0, nullptr, nullptr));
}

void PulseClient::set_sink_port(std::uint32_t index, const std::string& port) {
  if (impl_->context == nullptr || port.empty()) {
    return;
  }
  unref_op(pa_context_set_sink_port_by_index(impl_->context, index, port.c_str(), nullptr, nullptr));
}

void PulseClient::set_default_sink(const std::string& name) {
  if (impl_->context == nullptr || name.empty()) {
    return;
  }
  unref_op(pa_context_set_default_sink(impl_->context, name.c_str(), nullptr, nullptr));
}

void PulseClient::set_source_volume(std::uint32_t index, int percent) {
  const PaDevice* device = find_source(index);
  if (device == nullptr) {
    return;
  }
  set_source_volumes(index, scale_channel_volumes(device->channel_volumes, percent));
}

void PulseClient::set_source_volumes(std::uint32_t index, const std::vector<std::uint32_t>& volumes) {
  if (impl_->context == nullptr || volumes.empty()) {
    return;
  }
  const pa_cvolume volume = volume_from_channels(volumes);
  unref_op(pa_context_set_source_volume_by_index(impl_->context, index, &volume, nullptr, nullptr));
}

void PulseClient::set_source_mute(std::uint32_t index, bool mute) {
  if (impl_->context == nullptr) {
    return;
  }
  unref_op(pa_context_set_source_mute_by_index(impl_->context, index, mute ? 1 : 0, nullptr, nullptr));
}

void PulseClient::set_source_port(std::uint32_t index, const std::string& port) {
  if (impl_->context == nullptr || port.empty()) {
    return;
  }
  unref_op(pa_context_set_source_port_by_index(impl_->context, index, port.c_str(), nullptr, nullptr));
}

void PulseClient::set_default_source(const std::string& name) {
  if (impl_->context == nullptr || name.empty()) {
    return;
  }
  unref_op(pa_context_set_default_source(impl_->context, name.c_str(), nullptr, nullptr));
}

void PulseClient::set_input_volume(std::uint32_t index, int percent) {
  const PaInput* input = find_input(index);
  if (input == nullptr) {
    return;
  }
  set_input_volumes(index, scale_channel_volumes(input->channel_volumes, percent));
}

void PulseClient::set_input_volumes(std::uint32_t index, const std::vector<std::uint32_t>& volumes) {
  if (impl_->context == nullptr || volumes.empty()) {
    return;
  }
  const pa_cvolume volume = volume_from_channels(volumes);
  unref_op(pa_context_set_sink_input_volume(impl_->context, index, &volume, nullptr, nullptr));
}

void PulseClient::set_input_mute(std::uint32_t index, bool mute) {
  if (impl_->context == nullptr) {
    return;
  }
  unref_op(pa_context_set_sink_input_mute(impl_->context, index, mute ? 1 : 0, nullptr, nullptr));
}

void PulseClient::move_input_to_sink(std::uint32_t index, const std::string& sink_name) {
  if (impl_->context == nullptr || sink_name.empty()) {
    return;
  }
  unref_op(pa_context_move_sink_input_by_name(impl_->context, index, sink_name.c_str(), nullptr, nullptr));
}

void PulseClient::set_capture_volume(std::uint32_t index, int percent) {
  const PaCapture* capture = find_capture(index);
  if (capture == nullptr) {
    return;
  }
  set_capture_volumes(index, scale_channel_volumes(capture->channel_volumes, percent));
}

void PulseClient::set_capture_volumes(std::uint32_t index, const std::vector<std::uint32_t>& volumes) {
  if (impl_->context == nullptr || volumes.empty()) {
    return;
  }
  const pa_cvolume volume = volume_from_channels(volumes);
  unref_op(pa_context_set_source_output_volume(impl_->context, index, &volume, nullptr, nullptr));
}

void PulseClient::set_capture_mute(std::uint32_t index, bool mute) {
  if (impl_->context == nullptr) {
    return;
  }
  unref_op(pa_context_set_source_output_mute(impl_->context, index, mute ? 1 : 0, nullptr, nullptr));
}

void PulseClient::set_peak_source(const std::string& source_name) {
  if (source_name == impl_->peak_source && impl_->peak != nullptr) {
    return;
  }
  impl_->peak_source = source_name;
  if (impl_->peak != nullptr) {
    pa_stream_set_read_callback(impl_->peak, nullptr, nullptr);
    pa_stream_disconnect(impl_->peak);
    pa_stream_unref(impl_->peak);
    impl_->peak = nullptr;
  }
  if (source_name.empty() || impl_->context == nullptr ||
      pa_context_get_state(impl_->context) != PA_CONTEXT_READY) {
    return;
  }
  pa_sample_spec spec;
  spec.format = PA_SAMPLE_FLOAT32NE;
  spec.rate = 25;
  spec.channels = 1;
  pa_buffer_attr attr;
  std::memset(&attr, 0, sizeof(attr));
  attr.fragsize = sizeof(float);
  attr.maxlength = static_cast<std::uint32_t>(-1);
  impl_->peak = pa_stream_new(impl_->context, "LCOS input meter", &spec, nullptr);
  if (impl_->peak == nullptr) {
    return;
  }
  pa_stream_set_read_callback(impl_->peak, peak_cb, this);
  const pa_stream_flags_t flags = static_cast<pa_stream_flags_t>(PA_STREAM_PEAK_DETECT | PA_STREAM_ADJUST_LATENCY);
  if (pa_stream_connect_record(impl_->peak, source_name.c_str(), &attr, flags) < 0) {
    pa_stream_unref(impl_->peak);
    impl_->peak = nullptr;
  }
}

void PulseClient::set_card_profile(std::uint32_t index, const std::string& profile) {
  if (impl_->context == nullptr || profile.empty()) {
    return;
  }
  unref_op(pa_context_set_card_profile_by_index(impl_->context, index, profile.c_str(), nullptr,
                                              nullptr));
}

}  // namespace lcos
