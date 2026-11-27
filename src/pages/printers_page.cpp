// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/printers_page.hpp"

#include "logic/sample.hpp"
#include "ui_util.hpp"

#include <gtkmm/image.h>
#include <gtkmm/scrolledwindow.h>

namespace lcos {
namespace {

Gtk::ListBoxRow* printer_row(const PrinterRecord& printer) {
  auto* row = Gtk::manage(new Gtk::ListBoxRow());
  auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
  box->set_margin_top(4);
  box->set_margin_bottom(4);
  box->set_margin_start(8);
  box->set_margin_end(8);
  auto* icon = Gtk::manage(new Gtk::Image());
  icon->set_from_icon_name("printer", Gtk::ICON_SIZE_LARGE_TOOLBAR);
  auto* text = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 1));
  auto* name = make_label(printer.name);
  name->get_style_context()->add_class("lcos-strong");
  name->set_ellipsize(Pango::ELLIPSIZE_END);
  auto* detail = make_label(printer_status_line(printer));
  detail->get_style_context()->add_class("lcos-row-sub");
  detail->set_ellipsize(Pango::ELLIPSIZE_END);
  text->pack_start(*name, Gtk::PACK_SHRINK);
  text->pack_start(*detail, Gtk::PACK_SHRINK);
  box->pack_start(*icon, Gtk::PACK_SHRINK);
  box->pack_start(*text, Gtk::PACK_EXPAND_WIDGET);
  row->add(*box);
  row->set_name(printer.name);
  return row;
}

Gtk::ListBoxRow* job_row(const PrintJob& job) {
  auto* row = Gtk::manage(new Gtk::ListBoxRow());
  auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 1));
  box->set_margin_top(4);
  box->set_margin_bottom(4);
  box->set_margin_start(8);
  auto* title = make_label(job.title.empty() ? "Print job" : job.title);
  title->set_ellipsize(Pango::ELLIPSIZE_END);
  std::string detail = job_state_line(job.state);
  if (!job.user.empty()) {
    detail += " · " + job.user;
  }
  auto* line = make_label(detail);
  line->get_style_context()->add_class("lcos-row-sub");
  box->pack_start(*title, Gtk::PACK_SHRINK);
  box->pack_start(*line, Gtk::PACK_SHRINK);
  row->add(*box);
  row->set_name(std::to_string(job.id));
  return row;
}

Gtk::ScrolledWindow* list_scroll(Gtk::ListBox& list) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  scroll->get_style_context()->add_class("lcos-list");
  scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll->set_shadow_type(Gtk::SHADOW_IN);
  scroll->set_size_request(-1, 132);
  scroll->set_propagate_natural_height(false);
  scroll->add(list);
  return scroll;
}

}  // namespace

PrintersPage::PrintersPage() : pause_("_Pause", true), remove_("_Remove", true), cancel_job_("_Cancel job", true) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(8);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  page->pack_start(*make_empty_state("printer", "Looking for CUPS…", &status_), Gtk::PACK_SHRINK);
  page->set_margin_bottom(16);

  body_ = make_vbox(8);
  printers_.set_selection_mode(Gtk::SELECTION_SINGLE);
  jobs_.set_selection_mode(Gtk::SELECTION_SINGLE);
  auto* queues = make_vbox(8);
  queues->pack_start(*list_scroll(printers_), Gtk::PACK_SHRINK);
  auto* actions = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  auto* set_default = Gtk::manage(new Gtk::Button("Set as _default", true));
  auto* test = Gtk::manage(new Gtk::Button("Print _test page", true));
  auto* add = Gtk::manage(new Gtk::Button("Add _printer…", true));
  auto* refresh_button = Gtk::manage(new Gtk::Button("_Refresh", true));
  add->set_tooltip_text("Opens system-config-printer to add a printer");
  actions->pack_start(*set_default, Gtk::PACK_SHRINK);
  actions->pack_start(pause_, Gtk::PACK_SHRINK);
  actions->pack_start(*test, Gtk::PACK_SHRINK);
  actions->pack_start(remove_, Gtk::PACK_SHRINK);
  actions->pack_start(*add, Gtk::PACK_SHRINK);
  actions->pack_end(*refresh_button, Gtk::PACK_SHRINK);
  queues->pack_start(*actions, Gtk::PACK_SHRINK);
  queues->pack_start(*make_note("Adding a printer opens system-config-printer. The queue itself is managed here."),
                     Gtk::PACK_SHRINK);
  body_->pack_start(*make_frame("Printers", *queues), Gtk::PACK_SHRINK);

  auto* queue = make_vbox(8);
  queue->pack_start(*list_scroll(jobs_), Gtk::PACK_SHRINK);
  queue->pack_start(cancel_job_, Gtk::PACK_SHRINK);
  body_->pack_start(*make_frame("Jobs", *queue), Gtk::PACK_SHRINK);
  page->pack_start(*body_, Gtk::PACK_SHRINK);

  printers_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (freeze_ > 0) {
      return;
    }
    selected_name_ = row != nullptr ? std::string(row->get_name()) : std::string();
    refresh();
  });
  jobs_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (freeze_ > 0 || row == nullptr) {
      selected_job_ = row == nullptr ? -1 : selected_job_;
      return;
    }
    try {
      selected_job_ = std::stoi(std::string(row->get_name()));
    } catch (const std::exception&) {
      selected_job_ = -1;
    }
  });
  set_default->signal_clicked().connect([this] {
    const PrinterRecord* printer = selected();
    if (printer == nullptr) {
      return;
    }
    if (!have_default_) {
      for (const auto& item : snapshot_.printers) {
        if (item.is_default) {
          original_default_ = item.name;
        }
      }
      have_default_ = true;
    }
    const std::string error = cups_.set_default(printer->name);
    if (!error.empty()) {
      tell(error);
      return;
    }
    dirty_ = true;
    pending_changed.emit();
    refresh();
  });
  pause_.signal_clicked().connect([this] {
    const PrinterRecord* printer = selected();
    if (printer == nullptr) {
      return;
    }
    if (original_paused_.count(printer->name) == 0) {
      original_paused_[printer->name] = printer->paused;
    }
    const std::string error = cups_.set_paused(printer->name, !printer->paused);
    if (!error.empty()) {
      tell(error);
      return;
    }
    dirty_ = true;
    pending_changed.emit();
    refresh();
  });
  test->signal_clicked().connect([this] {
    const PrinterRecord* printer = selected();
    if (printer == nullptr) {
      return;
    }
    const std::string error = cups_.print_test_page(printer->name);
    if (!error.empty()) {
      tell(error);
      return;
    }
    refresh();
  });
  remove_.signal_clicked().connect([this] {
    const PrinterRecord* printer = selected();
    if (printer == nullptr) {
      return;
    }
    const std::string name = printer->name;
    if (!confirm_action(*this, "Remove " + name + "?",
                        "This deletes the queue. Jobs still in it are discarded.", "_Remove")) {
      return;
    }
    const std::string error = cups_.remove_printer(name);
    if (!error.empty()) {
      tell(error);
      return;
    }
    original_paused_.erase(name);
    if (selected_name_ == name) {
      selected_name_.clear();
    }
    refresh();
  });
  add->signal_clicked().connect([this] { launch_program(*this, "system-config-printer"); });
  refresh_button->signal_clicked().connect([this] { refresh(); });
  cancel_job_.signal_clicked().connect([this] {
    const PrintJob* job = selected_job();
    if (job == nullptr) {
      return;
    }
    const std::string error = cups_.cancel_job(job->printer, job->id);
    if (!error.empty()) {
      tell(error);
      return;
    }
    refresh();
  });
  show_all();
  refresh();
}

void PrintersPage::on_page_shown() { refresh(); }

void PrintersPage::tell(const std::string& message) {
  report_error(*this, "Printers", message);
}

const PrinterRecord* PrintersPage::selected() const {
  for (const auto& printer : snapshot_.printers) {
    if (printer.name == selected_name_) {
      return &printer;
    }
  }
  return snapshot_.printers.empty() ? nullptr : &snapshot_.printers.front();
}

const PrintJob* PrintersPage::selected_job() const {
  for (const auto& job : snapshot_.jobs) {
    if (job.id == selected_job_) {
      return &job;
    }
  }
  return nullptr;
}

void PrintersPage::refresh() {
  const std::string jobs_for = selected_name_;
  snapshot_ = cups_.snapshot(jobs_for, settings_sample());
  if (!snapshot_.available) {
    status_->set_text(snapshot_.error.empty() ? "CUPS is not running." : snapshot_.error);
    if (auto* banner = status_->get_parent()) {
      banner->show();
    }
    if (body_ != nullptr) {
      body_->set_sensitive(false);
    }
  } else if (snapshot_.printers.empty()) {
    status_->set_text("No printers are configured.");
    if (auto* banner = status_->get_parent()) {
      banner->show();
    }
    if (body_ != nullptr) {
      body_->set_sensitive(true);
    }
  } else {
    if (auto* banner = status_->get_parent()) {
      banner->hide();
    }
    if (body_ != nullptr) {
      body_->set_sensitive(true);
    }
  }
  rebuild();
}

void PrintersPage::rebuild() {
  Freeze freeze(freeze_);
  for (auto* child : printers_.get_children()) {
    printers_.remove(*child);
  }
  for (auto* child : jobs_.get_children()) {
    jobs_.remove(*child);
  }
  for (const auto& printer : snapshot_.printers) {
    printers_.append(*printer_row(printer));
  }
  if (snapshot_.jobs.empty()) {
    auto* row = Gtk::manage(new Gtk::ListBoxRow());
    row->set_selectable(false);
    auto* label = make_note(snapshot_.available ? "No jobs in this queue." : "Jobs appear when CUPS is running.");
    label->set_margin_top(8);
    label->set_margin_bottom(8);
    row->add(*label);
    jobs_.append(*row);
  } else {
    for (const auto& job : snapshot_.jobs) {
      jobs_.append(*job_row(job));
    }
  }
  printers_.show_all();
  jobs_.show_all();
  bool found = false;
  for (auto* row : printers_.get_children()) {
    if (row->get_name() == selected_name_) {
      printers_.select_row(*static_cast<Gtk::ListBoxRow*>(row));
      found = true;
    }
  }
  if (!found && !snapshot_.printers.empty()) {
    selected_name_ = snapshot_.printers.front().name;
    if (auto* row = printers_.get_row_at_index(0)) {
      printers_.select_row(*row);
    }
  }
  const PrinterRecord* printer = selected();
  pause_.set_label(printer != nullptr && printer->paused ? "Resume" : "Pause");
  const bool have = printer != nullptr && snapshot_.available;
  pause_.set_sensitive(have);
  remove_.set_sensitive(have);
  cancel_job_.set_sensitive(snapshot_.available && selected_job() != nullptr);
}

bool PrintersPage::commit_pending() {
  dirty_ = false;
  have_default_ = false;
  original_default_.clear();
  original_paused_.clear();
  pending_changed.emit();
  return true;
}

void PrintersPage::revert_page() {
  if (!dirty_) {
    return;
  }
  if (have_default_) {
    cups_.set_default(original_default_);
  }
  for (const auto& item : original_paused_) {
    cups_.set_paused(item.first, item.second);
  }
  dirty_ = false;
  have_default_ = false;
  original_default_.clear();
  original_paused_.clear();
  pending_changed.emit();
}

bool PrintersPage::discard_pending() {
  if (!dirty_) {
    return false;
  }
  revert_page();
  refresh();
  return true;
}

std::string PrintersPage::pending_detail() const {
  return dirty_ ? "Printer changes have not been kept." : std::string();
}

}  // namespace lcos
