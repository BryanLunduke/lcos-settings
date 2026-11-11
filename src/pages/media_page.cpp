// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/media_page.hpp"

#include "ui_util.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/scrolledwindow.h>

namespace lcos {
namespace {

constexpr const char* kChannel = "thunar-volman";

struct CommandWidgets {
  Gtk::CheckButton* check = nullptr;
  Gtk::Entry* entry = nullptr;
};

CommandWidgets add_command(XfconfStore& store, Gtk::Box& box, int& freeze_, const char* caption,
                           const char* enabled, const char* command,
                           const char* command_label = "Command") {
  auto* check = Gtk::manage(new Gtk::CheckButton(caption));
  auto* entry = Gtk::manage(new Gtk::Entry());
  entry->set_hexpand(true);
  entry->set_placeholder_text("%d device, %m mount point");
  entry->set_tooltip_text("Use %d for the device node and %m for the mount point. The command is saved when you leave the field or press Enter.");
  {
    Freeze hold(freeze_);
    check->set_active(store.get_bool(kChannel, enabled, false));
    entry->set_text(store.get_string(kChannel, command, ""));
    entry->set_sensitive(check->get_active());
  }
  int* depth = &freeze_;
  check->signal_toggled().connect([&store, depth, check, entry, enabled] {
    if (*depth > 0) {
      return;
    }
    store.set_bool(kChannel, enabled, check->get_active());
    entry->set_sensitive(check->get_active());
  });
  auto commit = [&store, depth, entry, command] {
    if (*depth > 0) {
      return;
    }
    store.set_string(kChannel, command, entry->get_text());
  };
  entry->signal_activate().connect(commit);
  entry->signal_focus_out_event().connect(
      [commit](GdkEventFocus*) {
        commit();
        return false;
      },
      false);
  box.pack_start(*check, Gtk::PACK_SHRINK);
  FormGrid form(140);
  form.add(command_label, *entry);
  box.pack_start(form.grid(), Gtk::PACK_SHRINK);
  return {check, entry};
}

void add_check(XfconfStore& store, Gtk::Box& box, int& freeze_, const char* caption,
               const char* prop) {
  auto* check = Gtk::manage(new Gtk::CheckButton(caption));
  {
    Freeze hold(freeze_);
    check->set_active(store.get_bool(kChannel, prop, false));
  }
  int* depth = &freeze_;
  check->signal_toggled().connect([&store, depth, check, prop] {
    if (*depth > 0) {
      return;
    }
    store.set_bool(kChannel, prop, check->get_active());
  });
  box.pack_start(*check, Gtk::PACK_SHRINK);
}

}  // namespace

MediaPage::MediaPage(XfconfStore& store) : store_(store) {
  pack_start(notebook_, Gtk::PACK_EXPAND_WIDGET);

  auto* storage = make_vbox(8);
  storage->set_border_width(12);
  auto* drives = make_vbox(6);
  add_check(store_, *drives, freeze_, "Mount removable drives when hot-plugged",
            "/automount-drives/enabled");
  add_check(store_, *drives, freeze_, "Mount removable media when inserted",
            "/automount-media/enabled");
  add_check(store_, *drives, freeze_, "Browse removable media when inserted", "/autobrowse/enabled");
  add_check(store_, *drives, freeze_, "Auto-run programs on new drives and media",
            "/autorun/enabled");
  add_check(store_, *drives, freeze_, "Auto-open files on new drives and media",
            "/autoopen/enabled");
  storage->pack_start(*make_frame("Removable storage", *drives), Gtk::PACK_SHRINK);
  auto* blank = make_vbox(6);
  auto burn = add_command(store_, *blank, freeze_, "Burn a CD or DVD when a blank disc is inserted",
                          "/autoburn/enabled", "/autoburn/data-cd-command", "Data CD command");
  auto* audio_burn = Gtk::manage(new Gtk::Entry());
  audio_burn->set_hexpand(true);
  audio_burn->set_placeholder_text("%d device, %m mount point");
  audio_burn->set_tooltip_text("Use %d for the device node and %m for the mount point. The command is saved when you leave the field or press Enter.");
  {
    Freeze hold(freeze_);
    audio_burn->set_text(store_.get_string(kChannel, "/autoburn/audio-cd-command", ""));
    audio_burn->set_sensitive(burn.check->get_active());
  }
  audio_burn->signal_activate().connect([this, audio_burn] {
    if (freeze_ > 0) {
      return;
    }
    store_.set_string(kChannel, "/autoburn/audio-cd-command", audio_burn->get_text());
  });
  audio_burn->signal_focus_out_event().connect(
      [this, audio_burn](GdkEventFocus*) {
        if (freeze_ == 0) {
          store_.set_string(kChannel, "/autoburn/audio-cd-command", audio_burn->get_text());
        }
        return false;
      },
      false);
  burn.check->signal_toggled().connect([audio_burn, burn] {
    audio_burn->set_sensitive(burn.check->get_active());
  });
  FormGrid audio_form(140);
  audio_form.add("Audio CD command", *audio_burn);
  blank->pack_start(audio_form.grid(), Gtk::PACK_SHRINK);
  storage->pack_start(*make_frame("Blank CDs and DVDs", *blank), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*storage), "Storage");

  auto* multimedia = make_vbox(8);
  multimedia->set_border_width(12);
  auto* audio = make_vbox(6);
  add_command(store_, *audio, freeze_, "Play audio CDs when inserted",
              "/autoplay-audio-cds/enabled", "/autoplay-audio-cds/command");
  multimedia->pack_start(*make_frame("Audio CDs", *audio), Gtk::PACK_SHRINK);
  auto* video = make_vbox(6);
  add_command(store_, *video, freeze_, "Play video CDs, DVDs, and Blu-rays when inserted",
              "/autoplay-video-cds/enabled", "/autoplay-video-cds/command");
  multimedia->pack_start(*make_frame("Video CDs, DVDs, and Blu-rays", *video), Gtk::PACK_SHRINK);
  auto* players = make_vbox(6);
  add_command(store_, *players, freeze_, "Play music files when connected", "/autoipod/enabled",
              "/autoipod/command");
  multimedia->pack_start(*make_frame("Portable music players", *players), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*multimedia), "Multimedia");

  auto* cameras = make_vbox(8);
  cameras->set_border_width(12);
  auto* photos = make_vbox(6);
  add_command(store_, *photos, freeze_, "Import digital photographs when connected",
              "/autophoto/enabled", "/autophoto/command");
  cameras->pack_start(*make_frame("Digital cameras", *photos), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*cameras), "Cameras");

  auto* printers = make_vbox(8);
  printers->set_border_width(12);
  auto* printer = make_vbox(6);
  add_command(store_, *printer, freeze_, "Automatically run a program when a printer is connected",
              "/autoprinter/enabled", "/autoprinter/command");
  printers->pack_start(*make_frame("Printers", *printer), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*printers), "Printers");

  auto* input = make_vbox(8);
  input->set_border_width(12);
  auto* keyboard = make_vbox(6);
  add_command(store_, *keyboard, freeze_,
              "Automatically run a program when a USB keyboard is connected",
              "/autokeyboard/enabled", "/autokeyboard/command");
  input->pack_start(*make_frame("Keyboards", *keyboard), Gtk::PACK_SHRINK);
  auto* mice = make_vbox(6);
  add_command(store_, *mice, freeze_, "Automatically run a program when a USB mouse is connected",
              "/automouse/enabled", "/automouse/command");
  input->pack_start(*make_frame("Mice", *mice), Gtk::PACK_SHRINK);
  auto* tablets = make_vbox(6);
  add_command(store_, *tablets, freeze_, "Automatically run a program when a tablet is connected",
              "/autotablet/enabled", "/autotablet/command");
  input->pack_start(*make_frame("Tablets", *tablets), Gtk::PACK_SHRINK);
  notebook_.append_page(*make_page_scroll(*input), "Input Devices");
}

void MediaPage::prepare_screenshot(int index) {
  if (index >= 0 && index < notebook_.get_n_pages()) {
    notebook_.set_current_page(index);
  }
}

std::string MediaPage::screenshot_suffix(int index) const {
  switch (index) {
    case 0:
      return "storage";
    case 1:
      return "multimedia";
    case 2:
      return "cameras";
    case 3:
      return "printers";
    case 4:
      return "input";
    default:
      return {};
  }
}

}  // namespace lcos
