// SPDX-License-Identifier: GPL-3.0-or-later

#include "pages/panel_page.hpp"

#include "logic/sample.hpp"
#include "ui_util.hpp"

#include <gtkmm/scrolledwindow.h>

namespace lcos {
namespace {

int sample_opacity = 80;

}  // namespace

PanelPage::PanelPage(XfconfStore& store)
    : store_(store),
      size_(Gtk::Adjustment::create(32, 16, 128, 1, 8)),
      length_(Gtk::Adjustment::create(100, 1, 100, 1, 10)),
      locked_("Lock the panel"),
      opacity_(Gtk::ORIENTATION_HORIZONTAL) {
  auto* scroll = Gtk::manage(new Gtk::ScrolledWindow());
  prepare_page_scroll(*scroll);
  auto* page = make_vbox(8);
  scroll->add(*page);
  pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);

  page->pack_start(*make_empty_state("preferences-desktop", "Looking for the panel…", &status_),
                   Gtk::PACK_SHRINK);
  page->set_margin_bottom(16);
  body_ = make_vbox(8);

  panel_.append("0", "Panel 1");
  position_.append("top", "Top");
  position_.append("bottom", "Bottom");
  position_.append("left", "Left");
  position_.append("right", "Right");
  autohide_.append("0", "Never");
  autohide_.append("1", "Intelligently");
  autohide_.append("2", "Always");
  background_.append("0", "None");
  background_.append("1", "Solid color");
  for (const auto& choice : panel_plugin_choices()) {
    if (choice.first != "pager") {
      add_kind_.append(choice.first, choice.second);
    }
  }
  add_kind_.set_active(0);
  size_.set_numeric(true);
  length_.set_numeric(true);
  opacity_.set_range(0, 100);
  opacity_.set_digits(0);
  opacity_.set_value(100);
  color_.set_use_alpha(false);

  auto* layout = make_vbox(4);
  FormGrid form;
  panel_caption_ = form.add("Panel", panel_);
  form.add("Position", position_);
  form.add("Size", size_);
  form.add("Length (%)", length_);
  form.add("Autohide", autohide_);
  form.add_control(locked_);
  form.add("Background", background_);
  form.add("Color", color_);
  form.add("Opacity", opacity_);
  layout->pack_start(form.grid(), Gtk::PACK_SHRINK);
  body_->pack_start(*make_frame("Panel", *layout), Gtk::PACK_SHRINK);

  items_list_.set_selection_mode(Gtk::SELECTION_SINGLE);
  auto* list_scroll = Gtk::manage(new Gtk::ScrolledWindow());
  list_scroll->get_style_context()->add_class("lcos-list");
  list_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  list_scroll->set_shadow_type(Gtk::SHADOW_IN);
  list_scroll->set_size_request(-1, 40);
  list_scroll->set_propagate_natural_height(false);
  list_scroll->add(items_list_);
  auto* items = make_vbox(8);
  items->pack_start(*list_scroll, Gtk::PACK_SHRINK);
  auto* actions = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
  actions->get_style_context()->add_class("lcos-actions");
  auto* up = Gtk::manage(new Gtk::Button("_Up", true));
  auto* down = Gtk::manage(new Gtk::Button("_Down", true));
  auto* remove = Gtk::manage(new Gtk::Button("_Remove", true));
  auto* add = Gtk::manage(new Gtk::Button("_Add", true));
  auto* advanced = Gtk::manage(new Gtk::Button("Ad_vanced…", true));
  actions->pack_start(*up, Gtk::PACK_SHRINK);
  actions->pack_start(*down, Gtk::PACK_SHRINK);
  actions->pack_start(*remove, Gtk::PACK_SHRINK);
  actions->pack_start(add_kind_, Gtk::PACK_SHRINK);
  actions->pack_start(*add, Gtk::PACK_SHRINK);
  actions->pack_end(*advanced, Gtk::PACK_SHRINK);
  items->pack_start(*actions, Gtk::PACK_SHRINK);
  body_->pack_start(*make_frame("Items", *items), Gtk::PACK_SHRINK);
  page->pack_start(*body_, Gtk::PACK_SHRINK);

  panel_.signal_changed().connect([this] {
    if (freeze_ > 0 || panel_.get_active_id().empty()) {
      return;
    }
    try {
      show_panel(std::stoi(std::string(panel_.get_active_id())));
    } catch (const std::exception&) {
    }
  });
  position_.signal_changed().connect([this] {
    if (freeze_ > 0 || sample_ || panel_id_ < 0) {
      return;
    }
    const std::string id = position_.get_active_id();
    PanelEdge edge = PanelEdge::Bottom;
    if (id == "top") {
      edge = PanelEdge::Top;
    } else if (id == "left") {
      edge = PanelEdge::Left;
    } else if (id == "right") {
      edge = PanelEdge::Right;
    }
    store_.set_string("xfce4-panel", prop("position"), position_for_edge(edge));
  });
  size_.signal_value_changed().connect([this] {
    if (freeze_ == 0 && !sample_ && panel_id_ >= 0) {
      store_.set_uint("xfce4-panel", prop("size"), static_cast<unsigned>(size_.get_value()));
    }
  });
  length_.signal_value_changed().connect([this] {
    if (freeze_ == 0 && !sample_ && panel_id_ >= 0) {
      store_.set_bool("xfce4-panel", prop("length-adjust"), false);
      store_.set_uint("xfce4-panel", prop("length"), static_cast<unsigned>(length_.get_value()));
    }
  });
  autohide_.signal_changed().connect([this] {
    if (freeze_ == 0 && !sample_ && panel_id_ >= 0 && !autohide_.get_active_id().empty()) {
      store_.set_uint("xfce4-panel", prop("autohide-behavior"),
                      static_cast<unsigned>(std::stoi(std::string(autohide_.get_active_id()))));
    }
  });
  locked_.signal_toggled().connect([this] {
    if (freeze_ == 0 && !sample_ && panel_id_ >= 0) {
      store_.set_bool("xfce4-panel", prop("position-locked"), locked_.get_active());
    }
  });
  auto write_background = [this] {
    if (freeze_ > 0 || sample_ || panel_id_ < 0) {
      return;
    }
    const bool solid = background_.get_active_id() == "1";
    store_.set_uint("xfce4-panel", prop("background-style"), solid ? 1u : 0u);
    Gdk::RGBA rgba = color_.get_rgba();
    rgba.set_alpha(opacity_.get_value() / 100.0);
    store_.set_doubles("xfce4-panel", prop("background-rgba"),
                       {rgba.get_red(), rgba.get_green(), rgba.get_blue(), rgba.get_alpha()});
    store_.set_uint("xfce4-panel", prop("leave-opacity"), static_cast<unsigned>(opacity_.get_value()));
    color_.set_sensitive(solid);
  };
  background_.signal_changed().connect(write_background);
  color_.signal_color_set().connect(write_background);
  opacity_.signal_value_changed().connect(write_background);
  up->signal_clicked().connect([this] {
    int index = items_list_.get_selected_row() != nullptr ? items_list_.get_selected_row()->get_index() : -1;
    const PluginEdit edit = shift_plugin(plugin_ids(), index, -1);
    if (edit.ok) {
      write_plugins(edit.ids);
    }
  });
  down->signal_clicked().connect([this] {
    int index = items_list_.get_selected_row() != nullptr ? items_list_.get_selected_row()->get_index() : -1;
    const PluginEdit edit = shift_plugin(plugin_ids(), index, 1);
    if (edit.ok) {
      write_plugins(edit.ids);
    }
  });
  remove->signal_clicked().connect([this] {
    const int index =
        items_list_.get_selected_row() != nullptr ? items_list_.get_selected_row()->get_index() : -1;
    if (index < 0 || index >= static_cast<int>(items_.size())) {
      return;
    }
    const int id = items_[static_cast<std::size_t>(index)].id;
    const PluginEdit edit = drop_plugin(plugin_ids(), index);
    if (!edit.ok) {
      return;
    }
    write_plugins(edit.ids);
    if (!sample_) {
      store_.reset("xfce4-panel", "/plugins/plugin-" + std::to_string(id), true);
    }
    show_panel(panel_id_);
  });
  add->signal_clicked().connect([this] {
    if (add_kind_.get_active_id().empty() || panel_id_ < 0) {
      return;
    }
    std::vector<int> ids = plugin_ids();
    const int id = allocate_plugin_id(known_plugin_ids());
    if (!sample_) {
      store_.set_string("xfce4-panel", "/plugins/plugin-" + std::to_string(id), add_kind_.get_active_id());
    }
    ids.push_back(id);
    write_plugins(ids);
    show_panel(panel_id_);
  });
  advanced->signal_clicked().connect([this] { launch_program(*this, "xfce4-panel --preferences"); });
  show_all();
  load();
}

void PanelPage::tell(const std::string& message) { report_error(*this, "Panel", message); }

std::string PanelPage::prop(const std::string& leaf) const {
  return "/panels/panel-" + std::to_string(panel_id_) + "/" + leaf;
}

std::vector<int> PanelPage::plugin_ids() const {
  std::vector<int> ids;
  for (const auto& item : items_) {
    ids.push_back(item.id);
  }
  return ids;
}

std::vector<int> PanelPage::known_plugin_ids() const {
  std::vector<int> ids = plugin_ids();
  if (sample_) {
    return ids;
  }
  for (const auto& row : store_.list("xfce4-panel", "/plugins")) {
    const std::string prefix = "/plugins/plugin-";
    if (row.first.rfind(prefix, 0) == 0) {
      try {
        ids.push_back(std::stoi(row.first.substr(prefix.size())));
      } catch (const std::exception&) {
      }
    }
  }
  return ids;
}

void PanelPage::write_plugins(const std::vector<int>& ids) {
  if (sample_) {
    items_.clear();
    for (int id : ids) {
      Item item;
      item.id = id;
      item.name = "item";
      for (const auto& existing : items_) {
        (void)existing;
      }
      items_.push_back(item);
    }
    return;
  }
  store_.set_ints("xfce4-panel", prop("plugin-ids"), ids);
}

void PanelPage::load() {
  sample_ = settings_sample();
  panels_.clear();
  if (sample_) {
    panels_ = {0, 1};
    if (auto* banner = status_->get_parent()) {
      banner->hide();
    }
    body_->set_sensitive(true);
  } else if (!store_.available()) {
    status_->set_text("Panel settings are not available.");
    if (auto* banner = status_->get_parent()) {
      banner->show();
    }
    body_->set_sensitive(false);
    panel_id_ = -1;
    return;
  } else {
    panels_ = store_.get_ints("xfce4-panel", "/panels");
    if (panels_.empty() && (store_.has("xfce4-panel", "/panels/panel-0/size") ||
                            store_.has("xfce4-panel", "/panels/panel-0/position"))) {
      panels_.push_back(0);
    }
    if (panels_.empty()) {
      status_->set_text("No panel is configured.");
      if (auto* banner = status_->get_parent()) {
        banner->show();
      }
      body_->set_sensitive(false);
      panel_id_ = -1;
      return;
    }
    if (auto* banner = status_->get_parent()) {
      banner->hide();
    }
    body_->set_sensitive(true);
  }
  Freeze freeze(freeze_);
  const std::string previous = panel_.get_active_id();
  panel_.remove_all();
  for (std::size_t index = 0; index < panels_.size(); ++index) {
    panel_.append(std::to_string(panels_[index]), "Panel " + std::to_string(index + 1));
  }
  panel_.set_visible(panels_.size() > 1);
  if (panel_caption_ != nullptr) {
    panel_caption_->set_visible(panels_.size() > 1);
  }
  if (!previous.empty()) {
    panel_.set_active_id(previous);
  }
  if (panel_.get_active_id().empty()) {
    panel_.set_active(0);
  }
  try {
    show_panel(std::stoi(std::string(panel_.get_active_id())));
  } catch (const std::exception&) {
    show_panel(panels_.front());
  }
}

void PanelPage::show_panel(int id) {
  panel_id_ = id;
  Freeze freeze(freeze_);
  std::string position = "p=11;x=0;y=0";
  unsigned size = 32;
  unsigned length = 100;
  unsigned autohide = 0;
  bool locked = false;
  unsigned background = 0;
  std::vector<double> rgba = {0.15, 0.15, 0.15, 1};
  unsigned opacity = 100;
  items_.clear();
  if (sample_) {
    position = id == 1 ? "p=2;x=0;y=0" : "p=11;x=0;y=0";
    size = id == 1 ? 28 : 36;
    length = 100;
    autohide = id == 1 ? 1 : 0;
    locked = id == 0;
    background = 1;
    opacity = static_cast<unsigned>(sample_opacity);
    rgba[3] = opacity / 100.0;
    const char* names[] = {"whiskermenu", "tasklist", "systray", "clock"};
    int plugin = 1;
    for (const char* name : names) {
      items_.push_back({plugin++, name});
    }
  } else {
    position = store_.get_string("xfce4-panel", prop("position"), position);
    size = store_.get_uint("xfce4-panel", prop("size"), size);
    length = store_.get_uint("xfce4-panel", prop("length"), length);
    autohide = store_.get_uint("xfce4-panel", prop("autohide-behavior"), autohide);
    locked = store_.get_bool("xfce4-panel", prop("position-locked"), false);
    background = store_.get_uint("xfce4-panel", prop("background-style"), 0);
    const auto stored = store_.get_doubles("xfce4-panel", prop("background-rgba"));
    if (stored.size() >= 4) {
      rgba = stored;
    }
    opacity = store_.get_uint("xfce4-panel", prop("leave-opacity"),
                              stored.size() >= 4 ? static_cast<unsigned>(rgba[3] * 100) : 100);
    for (int plugin : store_.get_ints("xfce4-panel", prop("plugin-ids"))) {
      Item item;
      item.id = plugin;
      item.name = store_.get_string("xfce4-panel", "/plugins/plugin-" + std::to_string(plugin), "item");
      items_.push_back(std::move(item));
    }
  }
  const PanelEdge edge = edge_from_position(position);
  position_.set_active_id(edge == PanelEdge::Top      ? "top"
                          : edge == PanelEdge::Left   ? "left"
                          : edge == PanelEdge::Right  ? "right"
                                                      : "bottom");
  size_.set_value(clamp_panel_size(static_cast<int>(size)));
  length_.set_value(clamp_panel_length(static_cast<int>(length)));
  autohide_.set_active_id(std::to_string(clamp_panel_autohide(static_cast<int>(autohide))));
  locked_.set_active(locked);
  background_.set_active_id(background == 1 ? "1" : "0");
  Gdk::RGBA color;
  color.set_rgba(rgba.size() > 0 ? rgba[0] : 0.15, rgba.size() > 1 ? rgba[1] : 0.15,
                 rgba.size() > 2 ? rgba[2] : 0.15, 1);
  color_.set_rgba(color);
  color_.set_sensitive(background == 1);
  opacity_.set_value(clamp_panel_opacity(static_cast<int>(opacity)));
  for (auto* child : items_list_.get_children()) {
    items_list_.remove(*child);
  }
  for (const auto& item : items_) {
    auto* row = Gtk::manage(new Gtk::ListBoxRow());
    auto* label = make_label(panel_plugin_title(item.name));
    label->set_margin_top(4);
    label->set_margin_bottom(4);
    label->set_margin_start(8);
    row->add(*label);
    items_list_.append(*row);
  }
  items_list_.show_all();
}

}  // namespace lcos
