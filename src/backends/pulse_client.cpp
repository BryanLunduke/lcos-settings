// SPDX-License-Identifier: GPL-3.0-or-later

#include "pulse_client.hpp"

#include "logic/volume.hpp"

#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>

#include <algorithm>
#include <utility>

namespace lcos {
namespace {

void sink_cb(pa_context*, const pa_sink_info* info, int eol, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  if (eol > 0) {
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
  if (eol > 0) {
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
  if (eol > 0) {
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

void card_cb(pa_context*, const pa_card_info* info, int eol, void* user) {
  auto* self = static_cast<PulseClient*>(user);
  if (eol > 0) {
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
      pa_context_subscribe(context,
                           static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_ALL),
                           nullptr, nullptr);
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

pa_cvolume volume_for(int channels, int percent) {
  pa_cvolume volume;
  pa_cvolume_init(&volume);
  pa_cvolume_set(&volume, static_cast<unsigned>(std::max(1, channels)),
                 percent_to_volume(percent));
  return volume;
}

}  // namespace

struct PulseClient::Impl {
  pa_glib_mainloop* loop = nullptr;
  pa_context* context = nullptr;
};

PulseClient::PulseClient() : impl_(new Impl) {}

PulseClient::~PulseClient() {
  if (impl_->context != nullptr) {
    pa_context_disconnect(impl_->context);
    pa_context_unref(impl_->context);
  }
  if (impl_->loop != nullptr) {
    pa_glib_mainloop_free(impl_->loop);
  }
  delete impl_;
}

void PulseClient::connect() {
  if (impl_->context != nullptr) {
    return;
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
void PulseClient::add_card(PaCard card) { incoming_.cards.push_back(std::move(card)); }

void PulseClient::list_finished() {
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
  pending_ = 5;
  pa_context_get_server_info(impl_->context, server_cb, this);
  pa_context_get_sink_info_list(impl_->context, sink_cb, this);
  pa_context_get_source_info_list(impl_->context, source_cb, this);
  pa_context_get_sink_input_info_list(impl_->context, input_cb, this);
  pa_context_get_card_info_list(impl_->context, card_cb, this);
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

void PulseClient::set_sink_volume(std::uint32_t index, int percent) {
  const PaDevice* device = find_sink(index);
  if (impl_->context == nullptr || device == nullptr) {
    return;
  }
  const pa_cvolume volume = volume_for(device->channels, percent);
  pa_context_set_sink_volume_by_index(impl_->context, index, &volume, nullptr, nullptr);
}

void PulseClient::set_sink_mute(std::uint32_t index, bool mute) {
  if (impl_->context == nullptr) {
    return;
  }
  pa_context_set_sink_mute_by_index(impl_->context, index, mute ? 1 : 0, nullptr, nullptr);
}

void PulseClient::set_sink_port(std::uint32_t index, const std::string& port) {
  if (impl_->context == nullptr || port.empty()) {
    return;
  }
  pa_context_set_sink_port_by_index(impl_->context, index, port.c_str(), nullptr, nullptr);
}

void PulseClient::set_default_sink(const std::string& name) {
  if (impl_->context == nullptr || name.empty()) {
    return;
  }
  pa_context_set_default_sink(impl_->context, name.c_str(), nullptr, nullptr);
}

void PulseClient::set_source_volume(std::uint32_t index, int percent) {
  const PaDevice* device = find_source(index);
  if (impl_->context == nullptr || device == nullptr) {
    return;
  }
  const pa_cvolume volume = volume_for(device->channels, percent);
  pa_context_set_source_volume_by_index(impl_->context, index, &volume, nullptr, nullptr);
}

void PulseClient::set_source_mute(std::uint32_t index, bool mute) {
  if (impl_->context == nullptr) {
    return;
  }
  pa_context_set_source_mute_by_index(impl_->context, index, mute ? 1 : 0, nullptr, nullptr);
}

void PulseClient::set_source_port(std::uint32_t index, const std::string& port) {
  if (impl_->context == nullptr || port.empty()) {
    return;
  }
  pa_context_set_source_port_by_index(impl_->context, index, port.c_str(), nullptr, nullptr);
}

void PulseClient::set_default_source(const std::string& name) {
  if (impl_->context == nullptr || name.empty()) {
    return;
  }
  pa_context_set_default_source(impl_->context, name.c_str(), nullptr, nullptr);
}

void PulseClient::set_input_volume(std::uint32_t index, int percent) {
  const PaInput* input = find_input(index);
  if (impl_->context == nullptr || input == nullptr) {
    return;
  }
  const pa_cvolume volume = volume_for(input->channels, percent);
  pa_context_set_sink_input_volume(impl_->context, index, &volume, nullptr, nullptr);
}

void PulseClient::set_input_mute(std::uint32_t index, bool mute) {
  if (impl_->context == nullptr) {
    return;
  }
  pa_context_set_sink_input_mute(impl_->context, index, mute ? 1 : 0, nullptr, nullptr);
}

void PulseClient::set_card_profile(std::uint32_t index, const std::string& profile) {
  if (impl_->context == nullptr || profile.empty()) {
    return;
  }
  pa_context_set_card_profile_by_index(impl_->context, index, profile.c_str(), nullptr,
                                       nullptr);
}

}  // namespace lcos
