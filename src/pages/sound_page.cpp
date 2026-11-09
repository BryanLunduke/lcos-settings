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
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll->set_shadow_type(Gtk::SHADOW_NONE);
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
      pulse_.set_default_sink(device->name);
    }
    refresh();
  });
  input_.signal_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_source()) {
      pulse_.set_default_source(device->name);
    }
    refresh();
  });
  output_port_.signal_changed().connect([this] {
    if (freeze_ > 0 || output_port_.get_active_id().empty()) {
      return;
    }
    if (const PaDevice* device = selected_sink()) {
      pulse_.set_sink_port(device->index, output_port_.get_active_id());
    }
  });
  input_port_.signal_changed().connect([this] {
    if (freeze_ > 0 || input_port_.get_active_id().empty()) {
      return;
    }
    if (const PaDevice* device = selected_source()) {
      pulse_.set_source_port(device->index, input_port_.get_active_id());
    }
  });
  output_volume_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_sink()) {
      pulse_.set_sink_volume(device->index, static_cast<int>(output_volume_.get_value()));
    }
  });
  input_volume_.signal_value_changed().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_source()) {
      pulse_.set_source_volume(device->index, static_cast<int>(input_volume_.get_value()));
    }
  });
  output_mute_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_sink()) {
      pulse_.set_sink_mute(device->index, output_mute_.get_active());
    }
  });
  input_mute_.signal_toggled().connect([this] {
    if (freeze_ > 0) {
      return;
    }
    if (const PaDevice* device = selected_source()) {
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
      pulse_.set_card_profile(card->index, profile_.get_active_id());
    }
  });
  output_volume_.signal_button_press_event().connect([this](GdkEventButton*) {
    dragging_ = true;
    return false;
  });
  output_volume_.signal_button_release_event().connect([this](GdkEventButton*) {
    dragging_ = false;
    return false;
  });
  input_volume_.signal_button_press_event().connect([this](GdkEventButton*) {
    dragging_ = true;
    return false;
  });
  input_volume_.signal_button_release_event().connect([this](GdkEventButton*) {
    dragging_ = false;
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

void SoundPage::on_page_shown() { refresh(); }

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
  if (dragging_) {
    return;
  }
  Freeze freeze(freeze_);
  const PulseState& state = pulse_.state();
  if (!state.ready) {
    status_->set_text(state.error.empty() ? "Waiting for PulseAudio…"
                                           : "PulseAudio: " + state.error);
  } else {
    status_->set_text("Levels are applied as you move them and kept by PulseAudio.");
  }

  const std::string previous_out = output_.get_active_id();
  output_.remove_all();
  for (const auto& device : state.sinks) {
    output_.append(device.name, device.description);
  }
  if (!state.default_sink.empty()) {
    output_.set_active_id(state.default_sink);
  } else if (!previous_out.empty()) {
    output_.set_active_id(previous_out);
  }
  if (output_.get_active_id().empty() && !state.sinks.empty()) {
    output_.set_active(0);
  }

  const std::string previous_in = input_.get_active_id();
  input_.remove_all();
  for (const auto& device : state.sources) {
    input_.append(device.name, device.description);
  }
  if (!state.default_source.empty()) {
    input_.set_active_id(state.default_source);
  } else if (!previous_in.empty()) {
    input_.set_active_id(previous_in);
  }
  if (input_.get_active_id().empty() && !state.sources.empty()) {
    input_.set_active(0);
  }

  auto fill_ports = [](Gtk::ComboBoxText& combo, const PaDevice* device) {
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
  fill_ports(output_port_, sink);
  fill_ports(input_port_, source);
  if (sink != nullptr) {
    output_volume_.set_value(sink->volume_percent);
    output_mute_.set_active(sink->mute);
  }
  if (source != nullptr) {
    input_volume_.set_value(source->volume_percent);
    input_mute_.set_active(source->mute);
  }

  profile_.remove_all();
  const PaDevice* owner = sink != nullptr ? sink : source;
  if (owner != nullptr) {
    if (const PaCard* card = card_for(owner->card)) {
      for (const auto& item : card->profiles) {
        profile_.append(item.first, item.second);
      }
      profile_.set_active_id(card->active_profile);
      profile_.set_sensitive(!card->profiles.empty());
    }
  } else {
    profile_.set_sensitive(false);
  }
  build_apps();
}

void SoundPage::build_apps() {
  for (auto* child : apps_->get_children()) {
    apps_->remove(*child);
  }
  const auto& inputs = pulse_.state().inputs;
  if (inputs.empty()) {
    auto* empty = make_label("No applications are playing audio.");
    apps_->pack_start(*empty, Gtk::PACK_SHRINK);
    apps_->show_all();
    return;
  }
  for (const auto& input : inputs) {
    auto* row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
    auto* name = make_label(input.application);
    name->set_size_request(160, -1);
    name->set_ellipsize(Pango::ELLIPSIZE_END);
    auto* scale = Gtk::manage(new Gtk::Scale(Gtk::ORIENTATION_HORIZONTAL));
    scale->set_range(0, 150);
    scale->set_digits(0);
    scale->set_value(input.volume_percent);
    scale->set_hexpand(true);
    auto* mute = Gtk::manage(new Gtk::CheckButton("Mute"));
    mute->set_active(input.mute);
    const std::uint32_t index = input.index;
    scale->signal_value_changed().connect([this, scale, index] {
      pulse_.set_input_volume(index, static_cast<int>(scale->get_value()));
    });
    mute->signal_toggled().connect([this, mute, index] {
      pulse_.set_input_mute(index, mute->get_active());
    });
    row->pack_start(*name, Gtk::PACK_SHRINK);
    row->pack_start(*scale, Gtk::PACK_EXPAND_WIDGET);
    row->pack_start(*mute, Gtk::PACK_SHRINK);
    apps_->pack_start(*row, Gtk::PACK_SHRINK);
  }
  apps_->show_all();
}

}  // namespace lcos
