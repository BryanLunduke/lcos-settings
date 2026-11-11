// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/sound_page.hpp"

#include "logic/themes.hpp"
#include "ui_util.hpp"

#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

namespace lcos {

SoundPage::SoundPage(XfconfStore& store, PulseClient& pulse)
    : store_(store),
      pulse_(pulse),
      output_volume_(Gtk::ORIENTATION_HORIZONTAL),
      input_volume_(Gtk::ORIENTATION_HORIZONTAL) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(4);
  page->set_border_width(12);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  status_ = make_label("Connecting to PulseAudio…");
  status_->set_line_wrap(true);
  page->pack_start(*status_, Gtk::PACK_SHRINK);

  auto tune = [](Gtk::Scale& scale) {
    scale.set_range(0, 150);
    scale.set_increments(1, 10);
    scale.set_digits(0);
    scale.set_value(100);
  };
  tune(output_volume_);
  tune(input_volume_);
  output_mute_.set_label("Mute");
  input_mute_.set_label("Mute");

  auto* out = make_vbox(8);
  FormGrid out_form;
  out_form.add("Device", output_);
  out_form.add("Port", output_port_);
  out_form.add("Volume", output_volume_);
  out_form.add_full(output_mute_);
  out->pack_start(out_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Output", *out), Gtk::PACK_SHRINK);

  auto* in = make_vbox(8);
  FormGrid in_form;
  in_form.add("Device", input_);
  in_form.add("Port", input_port_);
  in_form.add("Volume", input_volume_);
  in_form.add_full(input_mute_);
  in->pack_start(in_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Input", *in), Gtk::PACK_SHRINK);

  auto* card = make_vbox(8);
  FormGrid card_form;
  card_form.add("Profile", profile_);
  card->pack_start(card_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Sound card", *card), Gtk::PACK_SHRINK);

  apps_ = make_vbox(6);
  page->pack_start(*make_frame("Applications", *apps_), Gtk::PACK_SHRINK);

  auto* events = make_vbox(8);
  event_sounds_.set_label("Event sounds");
  input_feedback_.set_label("Input feedback sounds");
  FormGrid event_form;
  event_form.add("Sound theme", sound_theme_);
  event_form.add_full(event_sounds_);
  event_form.add_full(input_feedback_);
  events->pack_start(event_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Event sounds", *events), Gtk::PACK_SHRINK);

  output_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_sink()) {
      touch(SoundField::OutputDevice);
      pulse_.set_default_sink(device->name);
    }
    refresh();
  });
  input_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_source()) {
      touch(SoundField::InputDevice);
      pulse_.set_default_source(device->name);
    }
    refresh();
  });
  output_port_.signal_changed().connect([this] {
    if (freeze_ > 0 || output_port_.get_active_id().empty()) {
      return;
    }
    if (const PaDevice* device = selected_sink()) {
      touch(SoundField::OutputPort);
      pulse_.set_sink_port(device->index, output_port_.get_active_id());
    }
  });
  input_port_.signal_changed().connect([this] {
    if (freeze_ > 0 || input_port_.get_active_id().empty()) {
      return;
    }
    if (const PaDevice* device = selected_source()) {
      touch(SoundField::InputPort);
      pulse_.set_source_port(device->index, input_port_.get_active_id());
    }
  });
  output_volume_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_sink()) {
      touch(SoundField::OutputVolume);
      pulse_.set_sink_volume(device->index, static_cast<int>(output_volume_.get_value()));
    }
  });
  input_volume_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_source()) {
      touch(SoundField::InputVolume);
      pulse_.set_source_volume(device->index, static_cast<int>(input_volume_.get_value()));
    }
  });
  output_mute_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_sink()) {
      touch(SoundField::OutputMute);
      pulse_.set_sink_mute(device->index, output_mute_.get_active());
    }
  });
  input_mute_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_source()) {
      touch(SoundField::InputMute);
      pulse_.set_source_mute(device->index, input_mute_.get_active());
    }
  });
  profile_.signal_changed().connect([this] {
    if (freeze_ > 0 || profile_.get_active_id().empty()) {
      return;
    }
    const PaDevice* device = selected_sink();
    if (device == nullptr) {
      device = selected_source();
    }
    if (device == nullptr) {
      return;
    }
    if (const PaCard* card = card_for(device->card)) {
      touch(SoundField::Profile);
      pulse_.set_card_profile(card->index, profile_.get_active_id());
    }
  });
  output_volume_.signal_button_press_event().connect([this](GdkEventButton*) {
    output_dragging_ = true;
    return false;
  });
  output_volume_.signal_button_release_event().connect([this](GdkEventButton*) {
    output_dragging_ = false;
    return false;
  });
  input_volume_.signal_button_press_event().connect([this](GdkEventButton*) {
    input_dragging_ = true;
    return false;
  });
  input_volume_.signal_button_release_event().connect([this](GdkEventButton*) {
    input_dragging_ = false;
    return false;
  });

  event_sounds_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("xsettings", "/Net/EnableEventSounds", event_sounds_.get_active());
    }
  });
  input_feedback_.signal_toggled().connect([this] {
    if (freeze_ == 0) {
      store_.set_bool("xsettings", "/Net/EnableInputFeedbackSounds", input_feedback_.get_active());
    }
  });
  sound_theme_.signal_changed().connect([this] {
    if (freeze_ == 0 && !sound_theme_.get_active_id().empty()) {
      store_.set_string("xsettings", "/Net/SoundThemeName", sound_theme_.get_active_id());
    }
  });

  {
    Freeze freeze(freeze_);
    const auto themes = scan_sound_themes(data_roots("sounds", false));
    const std::string current = store_.get_string("xsettings", "/Net/SoundThemeName", "default");
    bool saw = false;
    for (const auto& theme : themes) {
      sound_theme_.append(theme.id, theme.name);
      if (theme.id == current) {
        saw = true;
      }
    }
    if (!current.empty() && !saw) {
      sound_theme_.append(current, current);
    }
    sound_theme_.set_active_id(current);
    event_sounds_.set_active(store_.get_bool("xsettings", "/Net/EnableEventSounds", false));
    input_feedback_.set_active(
        store_.get_bool("xsettings", "/Net/EnableInputFeedbackSounds", false));
  }

  pulse_.changed.connect(sigc::mem_fun(*this, &SoundPage::refresh));
  pulse_.connect();
  show_all();
}

void SoundPage::on_page_shown() {
  pulse_.connect();
  refresh();
}

void SoundPage::touch(SoundField field, std::uint32_t stream) {
  if (freeze_ > 0) {
    return;
  }
  if (!have_snapshot_) {
    take_snapshot();
  }
  switch (field) {
    case SoundField::OutputDevice:
      edit_.output_device = true;
      break;
    case SoundField::OutputPort:
      edit_.output_port = true;
      break;
    case SoundField::OutputVolume:
      edit_.output_volume = true;
      break;
    case SoundField::OutputMute:
      edit_.output_mute = true;
      break;
    case SoundField::InputDevice:
      edit_.input_device = true;
      break;
    case SoundField::InputPort:
      edit_.input_port = true;
      break;
    case SoundField::InputVolume:
      edit_.input_volume = true;
      break;
    case SoundField::InputMute:
      edit_.input_mute = true;
      break;
    case SoundField::Profile:
      edit_.profile = true;
      break;
    case SoundField::Stream:
      edit_.streams.insert(stream);
      break;
  }
  if (!sound_dirty_) {
    sound_dirty_ = true;
    pending_changed.emit();
  }
}

std::string SoundPage::combo_key(const std::vector<std::pair<std::string, std::string>>& rows) {
  std::string key;
  for (const auto& row : rows) {
    key += row.first;
    key += '\t';
    key += row.second;
    key += '\n';
  }
  return key;
}

void SoundPage::take_snapshot() {
  const PulseState& state = pulse_.state();
  if (!state.ready) {
    return;
  }
  output_snapshot_ = {};
  input_snapshot_ = {};
  output_snapshot_.name = state.default_sink;
  input_snapshot_.name = state.default_source;
  const PaDevice* sink = nullptr;
  for (const auto& device : state.sinks) {
    if (device.name == state.default_sink) {
      sink = &device;
      break;
    }
  }
  if (sink != nullptr) {
    output_snapshot_.volumes = sink->channel_volumes;
    output_snapshot_.mute = sink->mute;
    output_snapshot_.port = sink->active_port;
  }
  const PaDevice* source = nullptr;
  for (const auto& device : state.sources) {
    if (device.name == state.default_source) {
      source = &device;
      break;
    }
  }
  if (source != nullptr) {
    input_snapshot_.volumes = source->channel_volumes;
    input_snapshot_.mute = source->mute;
    input_snapshot_.port = source->active_port;
  }
  profile_snapshot_.clear();
  profile_card_ = 0;
  const PaDevice* owner = sink != nullptr ? sink : source;
  if (owner != nullptr) {
    if (const PaCard* card = card_for(owner->card)) {
      profile_card_ = card->index;
      profile_snapshot_ = card->active_profile;
    }
  }
  stream_snapshot_.clear();
  for (const auto& input : state.inputs) {
    StreamSnap snap;
    snap.application = input.application;
    snap.media_name = input.name;
    snap.index = input.index;
    snap.volumes = input.channel_volumes;
    snap.mute = input.mute;
    stream_snapshot_.push_back(std::move(snap));
  }
  have_snapshot_ = true;
}

bool SoundPage::commit_pending() {
  // The next edit snapshots the levels as they are then, including changes
  // made by volume keys after this Apply.
  edit_ = {};
  have_snapshot_ = false;
  if (sound_dirty_) {
    sound_dirty_ = false;
    pending_changed.emit();
  }
  return true;
}

void SoundPage::revert_page() {
  if (!sound_dirty_ || !have_snapshot_) {
    return;
  }
  if (edit_.output_device && !output_snapshot_.name.empty()) {
    pulse_.set_default_sink(output_snapshot_.name);
  }
  if (edit_.input_device && !input_snapshot_.name.empty()) {
    pulse_.set_default_source(input_snapshot_.name);
  }
  const PaDevice* sink = nullptr;
  for (const auto& device : pulse_.state().sinks) {
    if (device.name == output_snapshot_.name) {
      sink = &device;
      break;
    }
  }
  if (sink != nullptr) {
    if (edit_.output_volume && !output_snapshot_.volumes.empty()) {
      pulse_.set_sink_volumes(sink->index, output_snapshot_.volumes);
    }
    if (edit_.output_mute) {
      pulse_.set_sink_mute(sink->index, output_snapshot_.mute);
    }
    if (edit_.output_port && !output_snapshot_.port.empty()) {
      pulse_.set_sink_port(sink->index, output_snapshot_.port);
    }
  }
  const PaDevice* source = nullptr;
  for (const auto& device : pulse_.state().sources) {
    if (device.name == input_snapshot_.name) {
      source = &device;
      break;
    }
  }
  if (source != nullptr) {
    if (edit_.input_volume && !input_snapshot_.volumes.empty()) {
      pulse_.set_source_volumes(source->index, input_snapshot_.volumes);
    }
    if (edit_.input_mute) {
      pulse_.set_source_mute(source->index, input_snapshot_.mute);
    }
    if (edit_.input_port && !input_snapshot_.port.empty()) {
      pulse_.set_source_port(source->index, input_snapshot_.port);
    }
  }
  if (edit_.profile && profile_card_ != 0 && !profile_snapshot_.empty()) {
    pulse_.set_card_profile(profile_card_, profile_snapshot_);
  }
  std::vector<bool> used(pulse_.state().inputs.size(), false);
  for (const auto& snap : stream_snapshot_) {
    const PaInput* match = nullptr;
    for (std::size_t i = 0; i < pulse_.state().inputs.size(); ++i) {
      const auto& input = pulse_.state().inputs[i];
      if (used[i]) {
        continue;
      }
      if (input.index == snap.index && input.application == snap.application) {
        match = &input;
        used[i] = true;
        break;
      }
    }
    if (match == nullptr) {
      for (std::size_t i = 0; i < pulse_.state().inputs.size(); ++i) {
        const auto& input = pulse_.state().inputs[i];
        if (used[i]) {
          continue;
        }
        if (input.application == snap.application && input.name == snap.media_name) {
          match = &input;
          used[i] = true;
          break;
        }
      }
    }
    if (match == nullptr || snap.volumes.empty() || edit_.streams.count(snap.index) == 0) {
      continue;
    }
    pulse_.set_input_volumes(match->index, snap.volumes);
    pulse_.set_input_mute(match->index, snap.mute);
  }
  edit_ = {};
  have_snapshot_ = false;
  sound_dirty_ = false;
  pending_changed.emit();
}

const PaDevice* SoundPage::selected_sink() const {
  const std::string name = output_.get_active_id();
  for (const auto& device : pulse_.state().sinks) {
    if (device.name == name) {
      return &device;
    }
  }
  return pulse_.state().sinks.empty() ? nullptr : &pulse_.state().sinks.front();
}

const PaDevice* SoundPage::selected_source() const {
  const std::string name = input_.get_active_id();
  for (const auto& device : pulse_.state().sources) {
    if (device.name == name) {
      return &device;
    }
  }
  return pulse_.state().sources.empty() ? nullptr : &pulse_.state().sources.front();
}

const PaCard* SoundPage::card_for(std::uint32_t index) const {
  for (const auto& card : pulse_.state().cards) {
    if (card.index == index) {
      return &card;
    }
  }
  return nullptr;
}

void SoundPage::refresh() {
  Freeze freeze(freeze_);
  const PulseState& state = pulse_.state();
  if (!state.ready) {
    status_->set_text(state.error.empty() ? "Waiting for PulseAudio…"
                                           : "PulseAudio: " + state.error);
  } else {
    status_->set_text("Levels apply as you move them. Cancel restores only the levels changed in this window.");
  }

  std::vector<std::pair<std::string, std::string>> sink_rows;
  for (const auto& device : state.sinks) {
    sink_rows.emplace_back(device.name, device.description);
  }
  const std::string sink_key = combo_key(sink_rows);
  if (sink_key != output_rows_) {
    output_rows_ = sink_key;
    const std::string previous_out = output_.get_active_id();
    output_.remove_all();
    for (const auto& row : sink_rows) {
      output_.append(row.first, row.second);
    }
    if (!state.default_sink.empty()) {
      output_.set_active_id(state.default_sink);
    } else if (!previous_out.empty()) {
      output_.set_active_id(previous_out);
    }
    if (output_.get_active_id().empty() && !state.sinks.empty()) {
      output_.set_active(0);
    }
  }

  std::vector<std::pair<std::string, std::string>> source_rows;
  for (const auto& device : state.sources) {
    source_rows.emplace_back(device.name, device.description);
  }
  const std::string source_key = combo_key(source_rows);
  if (source_key != input_rows_) {
    input_rows_ = source_key;
    const std::string previous_in = input_.get_active_id();
    input_.remove_all();
    for (const auto& row : source_rows) {
      input_.append(row.first, row.second);
    }
    if (!state.default_source.empty()) {
      input_.set_active_id(state.default_source);
    } else if (!previous_in.empty()) {
      input_.set_active_id(previous_in);
    }
    if (input_.get_active_id().empty() && !state.sources.empty()) {
      input_.set_active(0);
    }
  }

  auto fill_ports = [](Gtk::ComboBoxText& combo, const PaDevice* device, std::string& cache) {
    std::vector<std::pair<std::string, std::string>> rows;
    if (device != nullptr) {
      for (const auto& port : device->ports) {
        rows.emplace_back(port.name, port.description);
      }
    }
    const std::string key = combo_key(rows);
    if (key == cache && device != nullptr) {
      if (!device->active_port.empty() && combo.get_active_id() != device->active_port) {
        combo.set_active_id(device->active_port);
      }
      return;
    }
    cache = key;
    const std::string previous = combo.get_active_id();
    combo.remove_all();
    if (device == nullptr) {
      combo.set_sensitive(false);
      return;
    }
    for (const auto& port : device->ports) {
      combo.append(port.name, port.description);
    }
    combo.set_sensitive(!device->ports.empty());
    if (!device->active_port.empty()) {
      combo.set_active_id(device->active_port);
    } else if (!previous.empty()) {
      combo.set_active_id(previous);
    }
  };
  const PaDevice* sink = selected_sink();
  const PaDevice* source = selected_source();
  fill_ports(output_port_, sink, output_port_rows_);
  fill_ports(input_port_, source, input_port_rows_);
  if (sink != nullptr) {
    if (!output_dragging_) {
      output_volume_.set_value(sink->volume_percent);
    }
    output_mute_.set_active(sink->mute);
  }
  if (source != nullptr) {
    if (!input_dragging_) {
      input_volume_.set_value(source->volume_percent);
    }
    input_mute_.set_active(source->mute);
  }

  const PaDevice* owner = sink != nullptr ? sink : source;
  std::vector<std::pair<std::string, std::string>> profile_rows;
  std::string active_profile;
  if (owner != nullptr) {
    if (const PaCard* card = card_for(owner->card)) {
      active_profile = card->active_profile;
      for (const auto& item : card->profiles) {
        profile_rows.emplace_back(item.first, item.second);
      }
    }
  }
  const std::string profile_key = combo_key(profile_rows);
  if (profile_key != profile_rows_) {
    profile_rows_ = profile_key;
    profile_.remove_all();
    for (const auto& row : profile_rows) {
      profile_.append(row.first, row.second);
    }
    if (!active_profile.empty()) {
      profile_.set_active_id(active_profile);
    }
    profile_.set_sensitive(!profile_rows.empty());
  }
  update_apps();
}

void SoundPage::update_apps() {
  const auto& inputs = pulse_.state().inputs;
  std::map<std::uint32_t, const PaInput*> live;
  for (const auto& input : inputs) {
    live[input.index] = &input;
  }
  std::vector<std::uint32_t> gone;
  for (auto& entry : app_rows_) {
    if (live.count(entry.first) == 0 && !entry.second.dragging) {
      gone.push_back(entry.first);
    }
  }
  for (const std::uint32_t index : gone) {
    auto found = app_rows_.find(index);
    if (found == app_rows_.end()) {
      continue;
    }
    apps_->remove(*found->second.row);
    app_rows_.erase(found);
  }
  if (inputs.empty() && app_rows_.empty()) {
    if (apps_->get_children().empty()) {
      apps_->pack_start(*make_label("No applications are playing audio."), Gtk::PACK_SHRINK);
      apps_->show_all();
    }
    return;
  }
  for (auto* child : apps_->get_children()) {
    bool keep = false;
    for (const auto& entry : app_rows_) {
      if (entry.second.row == child) {
        keep = true;
      }
    }
    if (!keep) {
      apps_->remove(*child);
    }
  }
  for (const auto& input : inputs) {
    auto found = app_rows_.find(input.index);
    if (found == app_rows_.end()) {
      AppRow row;
      row.row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
      auto* name = make_label(input.application);
      name->set_size_request(160, -1);
      name->set_ellipsize(Pango::ELLIPSIZE_END);
      row.scale = Gtk::manage(new Gtk::Scale(Gtk::ORIENTATION_HORIZONTAL));
      row.scale->set_range(0, 150);
      row.scale->set_digits(0);
      row.scale->set_value(input.volume_percent);
      row.scale->set_hexpand(true);
      row.mute = Gtk::manage(new Gtk::CheckButton("Mute"));
      row.mute->set_active(input.mute);
      const std::uint32_t index = input.index;
      row.scale->signal_value_changed().connect([this, index] {
        if (freeze_ > 0) {
          return;
        }
        auto row_at = app_rows_.find(index);
        if (row_at == app_rows_.end()) {
          return;
        }
        touch(SoundField::Stream, index);
        pulse_.set_input_volume(index, static_cast<int>(row_at->second.scale->get_value()));
      });
      row.mute->signal_toggled().connect([this, index] {
        if (freeze_ > 0) {
          return;
        }
        auto row_at = app_rows_.find(index);
        if (row_at == app_rows_.end()) {
          return;
        }
        touch(SoundField::Stream, index);
        pulse_.set_input_mute(index, row_at->second.mute->get_active());
      });
      row.scale->signal_button_press_event().connect([this, index](GdkEventButton*) {
        auto row_at = app_rows_.find(index);
        if (row_at != app_rows_.end()) {
          row_at->second.dragging = true;
        }
        return false;
      });
      row.scale->signal_button_release_event().connect([this, index](GdkEventButton*) {
        auto row_at = app_rows_.find(index);
        if (row_at != app_rows_.end()) {
          row_at->second.dragging = false;
        }
        refresh();
        return false;
      });
      row.row->pack_start(*name, Gtk::PACK_SHRINK);
      row.row->pack_start(*row.scale, Gtk::PACK_EXPAND_WIDGET);
      row.row->pack_start(*row.mute, Gtk::PACK_SHRINK);
      apps_->pack_start(*row.row, Gtk::PACK_SHRINK);
      app_rows_.emplace(index, row);
    } else if (!found->second.dragging) {
      found->second.scale->set_value(input.volume_percent);
      found->second.mute->set_active(input.mute);
    }
  }
  apps_->show_all();
}

}  // namespace lcos
