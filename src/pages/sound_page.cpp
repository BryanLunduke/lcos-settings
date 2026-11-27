// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/sound_page.hpp"

#include "logic/atomic_file.hpp"
#include "logic/sample.hpp"
#include "logic/themes.hpp"
#include "ui_util.hpp"

#include <glib.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>

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
  page->set_border_width(0);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  page->pack_start(*make_empty_state("audio-volume-muted", "Connecting to PulseAudio…", &status_),
                   Gtk::PACK_SHRINK);
  page->set_margin_bottom(16);

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
  output_port_label_ = out_form.add("Port", output_port_);
  out_form.add("Volume", output_volume_);
  out_form.add_control(output_mute_);
  out->pack_start(out_form.grid(), Gtk::PACK_SHRINK);
  page->pack_start(*make_frame("Output", *out), Gtk::PACK_SHRINK);

  auto* in = make_vbox(8);
  FormGrid in_form;
  in_form.add("Device", input_);
  input_port_label_ = in_form.add("Port", input_port_);
  in_form.add("Volume", input_volume_);
  in_form.add_control(input_mute_);
  input_meter_.set_mode(Gtk::LEVEL_BAR_MODE_CONTINUOUS);
  input_meter_.set_min_value(0);
  input_meter_.set_max_value(1);
  input_meter_.set_value(0);
  input_meter_.set_hexpand(true);
  meter_value_ = make_label("0%");
  meter_value_->set_width_chars(4);
  auto* meter_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  meter_row->pack_start(input_meter_, Gtk::PACK_EXPAND_WIDGET);
  meter_row->pack_start(*meter_value_, Gtk::PACK_SHRINK);
  in_form.add("Input level", *meter_row);
  in->pack_start(in_form.grid(), Gtk::PACK_SHRINK);
  input_note_ = make_note("PulseAudio is not available.");
  input_note_->set_no_show_all(true);
  input_note_->hide();
  in->pack_start(*input_note_, Gtk::PACK_SHRINK);
  input_.set_sensitive(false);
  input_port_.set_sensitive(false);
  input_volume_.set_sensitive(false);
  input_mute_.set_sensitive(false);
  page->pack_start(*make_frame("Input", *in), Gtk::PACK_SHRINK);

  auto* card = make_vbox(8);
  FormGrid card_form;
  profile_label_ = card_form.add("Profile", profile_);
  card->pack_start(card_form.grid(), Gtk::PACK_SHRINK);
  card_group_ = make_frame("Sound card", *card);
  card_group_->set_no_show_all(true);
  card_group_->hide();
  page->pack_start(*card_group_, Gtk::PACK_SHRINK);

  apps_ = make_vbox(6);
  page->pack_start(*make_frame("Applications", *apps_), Gtk::PACK_SHRINK);
  recording_ = make_vbox(6);
  page->pack_start(*make_frame("Recording", *recording_), Gtk::PACK_SHRINK);
  auto* advanced = Gtk::manage(new Gtk::Button("Advanced…"));
  advanced->set_halign(Gtk::ALIGN_START);
  advanced->set_tooltip_text("Open the volume control");
  advanced->signal_clicked().connect([this] { launch_program(*this, "pavucontrol"); });
  page->pack_start(*advanced, Gtk::PACK_SHRINK);

  auto* events = make_vbox(8);
  event_sounds_.set_label("Event sounds");
  input_feedback_.set_label("Input feedback sounds");
  FormGrid event_form;
  event_form.add("Sound theme", sound_theme_);
  event_form.add_control(event_sounds_);
  event_form.add_control(input_feedback_);
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
  pulse_.peak.connect([this](float level) {
    if (level < 0) {
      level = 0;
    }
    if (level > 1) {
      level = 1;
    }
    input_meter_.set_value(level);
    if (meter_value_ != nullptr) {
      meter_value_->set_text(std::to_string(static_cast<int>(level * 100.0f + 0.5f)) + "%");
    }
  });
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
    case SoundField::Capture:
      edit_.captures.insert(stream);
      break;
  }
  if (!sound_dirty_) {
    sound_dirty_ = true;
    pending_changed.emit();
  }
  save_sound_journal();
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
  have_profile_card_ = false;
  const PaDevice* owner = sink != nullptr ? sink : source;
  if (owner != nullptr) {
    if (const PaCard* card = card_for(owner->card)) {
      profile_card_ = card->index;
      profile_card_name_ = card->name;
      have_profile_card_ = true;
      profile_snapshot_ = card->active_profile;
    }
  }
  stream_snapshot_.clear();
  for (const auto& input : state.inputs) {
    StreamSnap snap;
    snap.application = input.application;
    snap.media_name = input.name;
    snap.sink = sink_name_for(input.sink);
    snap.index = input.index;
    snap.volumes = input.channel_volumes;
    snap.mute = input.mute;
    stream_snapshot_.push_back(std::move(snap));
  }
  capture_snapshot_.clear();
  for (const auto& capture : state.captures) {
    StreamSnap snap;
    snap.application = capture.application;
    snap.media_name = capture.name;
    snap.index = capture.index;
    snap.volumes = capture.channel_volumes;
    snap.mute = capture.mute;
    capture_snapshot_.push_back(std::move(snap));
  }
  have_snapshot_ = true;
}

std::string SoundPage::sink_name_for(std::uint32_t index) const {
  for (const auto& device : pulse_.state().sinks) {
    if (device.index == index) {
      return device.name;
    }
  }
  return {};
}

bool SoundPage::discard_pending() {
  if (!sound_dirty_) {
    return false;
  }
  revert_page();
  return true;
}

std::string SoundPage::pending_detail() const {
  if (!sound_dirty_) {
    return {};
  }
  const bool levels = edit_.output_volume || edit_.input_volume || edit_.output_mute ||
                      edit_.input_mute || !edit_.streams.empty() || !edit_.captures.empty();
  const bool route = edit_.output_device || edit_.input_device || edit_.output_port ||
                     edit_.input_port || edit_.profile;
  if (edit_.profile && !levels) {
    return "The sound profile has not been kept.";
  }
  if (levels && !route) {
    return "Sound levels have not been kept.";
  }
  return "Sound changes have not been kept.";
}

int SoundPage::output_volume_percent() const {
  if (const PaDevice* sink = selected_sink()) {
    return sink->volume_percent;
  }
  return static_cast<int>(output_volume_.get_value());
}

void SoundPage::set_output_volume_percent(int percent) {
  if (!have_snapshot_) {
    take_snapshot();
  }
  const int clamped = std::max(0, std::min(150, percent));
  output_volume_.set_value(clamped);
}

bool SoundPage::output_muted() const { return output_mute_.get_active(); }

void SoundPage::set_output_muted(bool muted) {
  if (output_mute_.get_active() == muted) {
    return;
  }
  output_mute_.set_active(muted);
}

bool SoundPage::output_ready() const {
  return pulse_.state().ready && selected_sink() != nullptr;
}

bool SoundPage::commit_pending() {
  // The next edit snapshots the levels as they are then, including changes
  // made by volume keys after this Apply.
  edit_ = {};
  have_snapshot_ = false;
  clear_sound_journal();
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
  if (edit_.profile && have_profile_card_ && !profile_snapshot_.empty()) {
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
    if (match == nullptr || edit_.streams.count(snap.index) == 0) {
      continue;
    }
    if (!snap.volumes.empty()) {
      pulse_.set_input_volumes(match->index, snap.volumes);
    }
    pulse_.set_input_mute(match->index, snap.mute);
    if (!snap.sink.empty()) {
      pulse_.move_input_to_sink(match->index, snap.sink);
    }
  }
  std::vector<bool> capture_used(pulse_.state().captures.size(), false);
  for (const auto& snap : capture_snapshot_) {
    const PaCapture* match = nullptr;
    for (std::size_t i = 0; i < pulse_.state().captures.size(); ++i) {
      const auto& capture = pulse_.state().captures[i];
      if (capture_used[i] || edit_.captures.count(snap.index) == 0) {
        continue;
      }
      if (capture.index == snap.index ||
          (capture.application == snap.application && capture.name == snap.media_name)) {
        match = &capture;
        capture_used[i] = true;
        break;
      }
    }
    if (match == nullptr) {
      continue;
    }
    if (!snap.volumes.empty()) {
      pulse_.set_capture_volumes(match->index, snap.volumes);
    }
    pulse_.set_capture_mute(match->index, snap.mute);
  }
  edit_ = {};
  have_snapshot_ = false;
  sound_dirty_ = false;
  clear_sound_journal();
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

SoundPage::~SoundPage() {
  if (sound_dirty_) {
    revert_page();
  }
}

std::string SoundPage::sound_journal_path() {
  const char* dir = g_get_user_config_dir();
  const std::string root = dir != nullptr && dir[0] != '\0' ? dir : "/tmp";
  return root + "/lcos-settings/sound.rollback";
}

void SoundPage::clear_sound_journal() { ::unlink(sound_journal_path().c_str()); }

void SoundPage::save_sound_journal() const {
  if (!have_snapshot_ || !edit_.any()) {
    clear_sound_journal();
    return;
  }
  SoundRollback rollback;
  rollback.output_device = edit_.output_device;
  rollback.output_name = output_snapshot_.name;
  rollback.output_volume = edit_.output_volume;
  rollback.output_volumes = output_snapshot_.volumes;
  rollback.output_mute_set = edit_.output_mute;
  rollback.output_mute = output_snapshot_.mute;
  rollback.output_port = edit_.output_port;
  rollback.output_port_name = output_snapshot_.port;
  rollback.input_device = edit_.input_device;
  rollback.input_name = input_snapshot_.name;
  rollback.input_volume = edit_.input_volume;
  rollback.input_volumes = input_snapshot_.volumes;
  rollback.input_mute_set = edit_.input_mute;
  rollback.input_mute = input_snapshot_.mute;
  rollback.input_port = edit_.input_port;
  rollback.input_port_name = input_snapshot_.port;
  rollback.profile = edit_.profile && have_profile_card_;
  rollback.profile_card = profile_card_name_;
  rollback.profile_name = profile_snapshot_;
  for (const auto& snap : stream_snapshot_) {
    if (edit_.streams.count(snap.index) == 0) {
      continue;
    }
    SoundRollbackStream stream;
    stream.application = snap.application;
    stream.media = snap.media_name;
    stream.mute = snap.mute;
    stream.volumes = snap.volumes;
    stream.sink = snap.sink;
    rollback.streams.push_back(std::move(stream));
  }
  for (const auto& snap : capture_snapshot_) {
    if (edit_.captures.count(snap.index) == 0) {
      continue;
    }
    SoundRollbackStream stream;
    stream.application = snap.application;
    stream.media = snap.media_name;
    stream.mute = snap.mute;
    stream.volumes = snap.volumes;
    rollback.captures.push_back(std::move(stream));
  }
  const std::string path = sound_journal_path();
  const auto slash = path.find_last_of('/');
  if (slash != std::string::npos) {
    g_mkdir_with_parents(path.substr(0, slash).c_str(), 0700);
  }
  ReplaceRequest request;
  request.mode_if_new = 0600;
  std::string error;
  replace_regular_file(path, format_sound_rollback(rollback), request, &error);
}

void SoundPage::restore_sound_journal() {
  if (journal_checked_ || !pulse_.state().ready) {
    return;
  }
  journal_checked_ = true;
  const std::string path = sound_journal_path();
  std::ifstream in(path);
  if (!in) {
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  SoundRollback rollback;
  std::string error;
  if (!parse_sound_rollback(text, rollback, &error) || !rollback.any()) {
    clear_sound_journal();
    return;
  }
  if (rollback.output_device && !rollback.output_name.empty()) {
    pulse_.set_default_sink(rollback.output_name);
  }
  if (rollback.input_device && !rollback.input_name.empty()) {
    pulse_.set_default_source(rollback.input_name);
  }
  for (const auto& device : pulse_.state().sinks) {
    if (device.name != rollback.output_name) {
      continue;
    }
    if (rollback.output_volume && !rollback.output_volumes.empty()) {
      pulse_.set_sink_volumes(device.index, rollback.output_volumes);
    }
    if (rollback.output_mute_set) {
      pulse_.set_sink_mute(device.index, rollback.output_mute);
    }
    if (rollback.output_port && !rollback.output_port_name.empty()) {
      pulse_.set_sink_port(device.index, rollback.output_port_name);
    }
    break;
  }
  for (const auto& device : pulse_.state().sources) {
    if (device.name != rollback.input_name) {
      continue;
    }
    if (rollback.input_volume && !rollback.input_volumes.empty()) {
      pulse_.set_source_volumes(device.index, rollback.input_volumes);
    }
    if (rollback.input_mute_set) {
      pulse_.set_source_mute(device.index, rollback.input_mute);
    }
    if (rollback.input_port && !rollback.input_port_name.empty()) {
      pulse_.set_source_port(device.index, rollback.input_port_name);
    }
    break;
  }
  if (rollback.profile && !rollback.profile_name.empty()) {
    for (const auto& card : pulse_.state().cards) {
      if (card.name == rollback.profile_card) {
        pulse_.set_card_profile(card.index, rollback.profile_name);
        break;
      }
    }
  }
  std::vector<bool> used(pulse_.state().inputs.size(), false);
  for (const auto& stream : rollback.streams) {
    for (std::size_t i = 0; i < pulse_.state().inputs.size(); ++i) {
      const auto& input = pulse_.state().inputs[i];
      if (used[i] || input.application != stream.application || input.name != stream.media) {
        continue;
      }
      used[i] = true;
      if (!stream.volumes.empty()) {
        pulse_.set_input_volumes(input.index, stream.volumes);
      }
      pulse_.set_input_mute(input.index, stream.mute);
      if (!stream.sink.empty()) {
        pulse_.move_input_to_sink(input.index, stream.sink);
      }
      break;
    }
  }
  std::vector<bool> capture_used(pulse_.state().captures.size(), false);
  for (const auto& stream : rollback.captures) {
    for (std::size_t i = 0; i < pulse_.state().captures.size(); ++i) {
      const auto& capture = pulse_.state().captures[i];
      if (capture_used[i] || capture.application != stream.application || capture.name != stream.media) {
        continue;
      }
      capture_used[i] = true;
      if (!stream.volumes.empty()) {
        pulse_.set_capture_volumes(capture.index, stream.volumes);
      }
      pulse_.set_capture_mute(capture.index, stream.mute);
      break;
    }
  }
  clear_sound_journal();
}

void SoundPage::refresh() {
  restore_sound_journal();
  const PulseState& state = pulse_.state();
  if (!state.ready && settings_sample()) {
    show_sample();
    return;
  }
  Freeze freeze(freeze_);
  if (!state.ready) {
    status_->set_text(state.error.empty() ? "PulseAudio is not available." : state.error);
    if (auto* banner = status_->get_parent()) {
      banner->show();
    }
  } else {
    status_->set_text("");
    if (auto* banner = status_->get_parent()) {
      banner->hide();
    }
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
      combo.set_visible(false);
      return;
    }
    for (const auto& port : device->ports) {
      combo.append(port.name, port.description);
    }
    const bool show = !device->ports.empty();
    combo.set_visible(show);
    combo.set_sensitive(show);
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
  if (output_port_label_ != nullptr) {
    output_port_label_->set_visible(output_port_.get_visible());
  }
  if (input_port_label_ != nullptr) {
    input_port_label_->set_visible(input_port_.get_visible());
  }
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
  const PaCard* card = nullptr;
  if (owner != nullptr) {
    card = card_for(owner->card);
  }
  if (card == nullptr) {
    for (const auto& item : state.cards) {
      if (!item.profiles.empty()) {
        card = &item;
        break;
      }
    }
  }
  std::vector<std::pair<std::string, std::string>> profile_rows;
  std::string active_profile;
  if (card != nullptr) {
    active_profile = card->active_profile;
    for (const auto& item : card->profiles) {
      profile_rows.emplace_back(item.first, item.second);
    }
  }
  const std::string profile_key = combo_key(profile_rows);
  if (profile_key != profile_rows_) {
    profile_rows_ = profile_key;
    profile_.remove_all();
    for (const auto& row : profile_rows) {
      profile_.append(row.first, row.second);
    }
    profile_.set_sensitive(!profile_rows.empty());
  }
  if (!active_profile.empty() && profile_.get_active_id() != active_profile) {
    profile_.set_active_id(active_profile);
  }
  const bool show_profile = !profile_rows.empty();
  profile_.set_visible(show_profile);
  if (profile_label_ != nullptr) {
    profile_label_->set_visible(show_profile);
  }
  if (card_group_ != nullptr) {
    card_group_->set_visible(show_profile);
  }
  const bool pulse_up = state.ready && state.error.empty();
  const bool have_input = pulse_up && !state.sources.empty();
  input_.set_sensitive(have_input);
  input_volume_.set_sensitive(have_input);
  input_mute_.set_sensitive(have_input);
  if (!have_input) {
    input_port_.set_sensitive(false);
  }
  if (input_note_ != nullptr) {
    if (have_input) {
      input_note_->hide();
    } else {
      input_note_->set_text(pulse_up ? "No input device is connected."
                                     : "PulseAudio is not available.");
      input_note_->show();
    }
  }
  const std::string peak_name = source != nullptr ? source->name : std::string();
  if (peak_name != peak_source_) {
    peak_source_ = peak_name;
    pulse_.set_peak_source(peak_name);
  }
  if (peak_name.empty() && meter_value_ != nullptr) {
    input_meter_.set_value(0);
    meter_value_->set_text("0%");
  }
  update_apps();
  update_captures();
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
      row.device = Gtk::manage(new Gtk::ComboBoxText());
      cap_combo_width(*row.device);
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
      row.device->signal_changed().connect([this, index] {
        if (freeze_ > 0) {
          return;
        }
        auto row_at = app_rows_.find(index);
        if (row_at == app_rows_.end() || row_at->second.device == nullptr ||
            row_at->second.device->get_active_id().empty()) {
          return;
        }
        touch(SoundField::Stream, index);
        pulse_.move_input_to_sink(index, row_at->second.device->get_active_id());
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
      row.row->pack_start(*row.device, Gtk::PACK_SHRINK);
      row.row->pack_start(*row.mute, Gtk::PACK_SHRINK);
      apps_->pack_start(*row.row, Gtk::PACK_SHRINK);
      app_rows_.emplace(index, row);
    } else if (!found->second.dragging) {
      found->second.scale->set_value(input.volume_percent);
      found->second.mute->set_active(input.mute);
    }
    auto live_row = found == app_rows_.end() ? app_rows_.find(input.index) : found;
    AppRow* row = live_row == app_rows_.end() ? nullptr : &live_row->second;
    if (row->device != nullptr) {
      std::string key;
      for (const auto& device : pulse_.state().sinks) {
        key += device.name;
        key.push_back('\n');
      }
      if (key != row->device->get_name()) {
        row->device->remove_all();
        for (const auto& device : pulse_.state().sinks) {
          row->device->append(device.name, device.description);
        }
        row->device->set_name(key);
      }
      const std::string sink = sink_name_for(input.sink);
      if (!sink.empty() && row->device->get_active_id() != sink) {
        row->device->set_active_id(sink);
      }
    }
  }
  apps_->show_all();
}

void SoundPage::update_captures() {
  const auto& captures = pulse_.state().captures;
  std::map<std::uint32_t, const PaCapture*> live;
  for (const auto& capture : captures) {
    live[capture.index] = &capture;
  }
  std::vector<std::uint32_t> gone;
  for (const auto& entry : capture_rows_) {
    if (live.count(entry.first) == 0 && !entry.second.dragging) {
      gone.push_back(entry.first);
    }
  }
  for (const std::uint32_t index : gone) {
    auto found = capture_rows_.find(index);
    if (found == capture_rows_.end()) {
      continue;
    }
    recording_->remove(*found->second.row);
    capture_rows_.erase(found);
  }
  if (captures.empty() && capture_rows_.empty()) {
    if (recording_->get_children().empty()) {
      auto* note = make_label(pulse_.state().ready ? "No application is recording."
                                                   : "PulseAudio is not available.");
      recording_->pack_start(*note, Gtk::PACK_SHRINK);
      recording_->show_all();
    }
    return;
  }
  for (auto* child : recording_->get_children()) {
    bool keep = false;
    for (const auto& entry : capture_rows_) {
      if (entry.second.row == child) {
        keep = true;
      }
    }
    if (!keep) {
      recording_->remove(*child);
    }
  }
  for (const auto& capture : captures) {
    auto found = capture_rows_.find(capture.index);
    if (found == capture_rows_.end()) {
      AppRow row;
      row.row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
      auto* name = make_label(capture.application);
      name->set_size_request(160, -1);
      name->set_ellipsize(Pango::ELLIPSIZE_END);
      row.scale = Gtk::manage(new Gtk::Scale(Gtk::ORIENTATION_HORIZONTAL));
      row.scale->set_range(0, 150);
      row.scale->set_digits(0);
      row.scale->set_value(capture.volume_percent);
      row.scale->set_hexpand(true);
      row.mute = Gtk::manage(new Gtk::CheckButton("Mute"));
      row.mute->set_active(capture.mute);
      const std::uint32_t index = capture.index;
      row.scale->signal_value_changed().connect([this, index] {
        if (freeze_ > 0) {
          return;
        }
        auto row_at = capture_rows_.find(index);
        if (row_at == capture_rows_.end()) {
          return;
        }
        touch(SoundField::Capture, index);
        pulse_.set_capture_volume(index, static_cast<int>(row_at->second.scale->get_value()));
      });
      row.mute->signal_toggled().connect([this, index] {
        if (freeze_ > 0) {
          return;
        }
        auto row_at = capture_rows_.find(index);
        if (row_at == capture_rows_.end()) {
          return;
        }
        touch(SoundField::Capture, index);
        pulse_.set_capture_mute(index, row_at->second.mute->get_active());
      });
      row.scale->signal_button_press_event().connect([this, index](GdkEventButton*) {
        auto row_at = capture_rows_.find(index);
        if (row_at != capture_rows_.end()) {
          row_at->second.dragging = true;
        }
        return false;
      });
      row.scale->signal_button_release_event().connect([this, index](GdkEventButton*) {
        auto row_at = capture_rows_.find(index);
        if (row_at != capture_rows_.end()) {
          row_at->second.dragging = false;
        }
        refresh();
        return false;
      });
      row.row->pack_start(*name, Gtk::PACK_SHRINK);
      row.row->pack_start(*row.scale, Gtk::PACK_EXPAND_WIDGET);
      row.row->pack_start(*row.mute, Gtk::PACK_SHRINK);
      recording_->pack_start(*row.row, Gtk::PACK_SHRINK);
      capture_rows_.emplace(index, row);
    } else if (!found->second.dragging) {
      found->second.scale->set_value(capture.volume_percent);
      found->second.mute->set_active(capture.mute);
    }
  }
  recording_->show_all();
}

void SoundPage::show_sample() {
  Freeze freeze(freeze_);
  status_->set_text("");
  if (auto* banner = status_->get_parent()) {
    banner->hide();
  }
  output_.remove_all();
  output_.append("alsa_output.built_in", "Built-in Audio");
  output_.append("alsa_output.headset", "USB Headset");
  output_.set_active(0);
  input_.remove_all();
  input_.append("alsa_input.built_in", "Built-in Microphone");
  input_.set_active(0);
  input_.set_sensitive(true);
  output_port_.remove_all();
  output_port_.append("analog-output-speaker", "Speakers");
  output_port_.append("analog-output-headphones", "Headphones");
  output_port_.set_active(0);
  output_port_.set_visible(true);
  input_port_.remove_all();
  input_port_.append("analog-input-mic", "Microphone");
  input_port_.set_active(0);
  input_port_.set_visible(true);
  if (output_port_label_ != nullptr) {
    output_port_label_->show();
  }
  if (input_port_label_ != nullptr) {
    input_port_label_->show();
  }
  output_volume_.set_value(70);
  input_volume_.set_value(55);
  output_mute_.set_active(false);
  input_mute_.set_active(false);
  input_volume_.set_sensitive(true);
  input_mute_.set_sensitive(true);
  profile_.remove_all();
  profile_.append("output:analog-stereo+input:analog-stereo", "Analog Stereo Duplex");
  profile_.set_active(0);
  profile_.show();
  if (profile_label_ != nullptr) {
    profile_label_->show();
  }
  if (card_group_ != nullptr) {
    card_group_->show();
  }
  input_meter_.set_value(0.42);
  if (meter_value_ != nullptr) {
    meter_value_->set_text("42%");
  }
  if (input_note_ != nullptr) {
    input_note_->hide();
  }
  for (auto* child : apps_->get_children()) {
    apps_->remove(*child);
  }
  app_rows_.clear();
  auto* playback = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  auto* playback_name = make_label("Music");
  playback_name->set_size_request(160, -1);
  auto* playback_scale = Gtk::manage(new Gtk::Scale(Gtk::ORIENTATION_HORIZONTAL));
  playback_scale->set_range(0, 150);
  playback_scale->set_digits(0);
  playback_scale->set_value(80);
  playback_scale->set_hexpand(true);
  auto* playback_device = Gtk::manage(new Gtk::ComboBoxText());
  playback_device->append("alsa_output.built_in", "Built-in Audio");
  playback_device->append("alsa_output.headset", "USB Headset");
  playback_device->set_active(0);
  cap_combo_width(*playback_device);
  playback->pack_start(*playback_name, Gtk::PACK_SHRINK);
  playback->pack_start(*playback_scale, Gtk::PACK_EXPAND_WIDGET);
  playback->pack_start(*playback_device, Gtk::PACK_SHRINK);
  playback->pack_start(*Gtk::manage(new Gtk::CheckButton("Mute")), Gtk::PACK_SHRINK);
  apps_->pack_start(*playback, Gtk::PACK_SHRINK);
  apps_->show_all();
  for (auto* child : recording_->get_children()) {
    recording_->remove(*child);
  }
  capture_rows_.clear();
  auto* record = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  auto* record_name = make_label("Voice");
  record_name->set_size_request(160, -1);
  auto* record_scale = Gtk::manage(new Gtk::Scale(Gtk::ORIENTATION_HORIZONTAL));
  record_scale->set_range(0, 150);
  record_scale->set_digits(0);
  record_scale->set_value(60);
  record_scale->set_hexpand(true);
  record->pack_start(*record_name, Gtk::PACK_SHRINK);
  record->pack_start(*record_scale, Gtk::PACK_EXPAND_WIDGET);
  record->pack_start(*Gtk::manage(new Gtk::CheckButton("Mute")), Gtk::PACK_SHRINK);
  recording_->pack_start(*record, Gtk::PACK_SHRINK);
  recording_->show_all();
}

}  // namespace lcos
