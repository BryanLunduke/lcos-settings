// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/button_layout.hpp"

#include <cctype>

namespace lcos {
namespace {

char canonical_button(char code) {
  const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(code)));
  return title_button_known(upper) ? upper : '\0';
}

void push_unique(std::string& side, char code) {
  if (code != '\0' && side.find(code) == std::string::npos) {
    side.push_back(code);
  }
}

std::string without(const std::string& side, char code) {
  std::string out;
  for (char letter : side) {
    if (letter != code) {
      out.push_back(letter);
    }
  }
  return out;
}

void move_within(std::string& side, char code, int delta) {
  const auto pos = side.find(code);
  if (pos == std::string::npos) {
    return;
  }
  const int next = static_cast<int>(pos) + delta;
  if (next < 0 || next >= static_cast<int>(side.size())) {
    return;
  }
  std::swap(side[pos], side[static_cast<std::size_t>(next)]);
}

}  // namespace

bool title_button_known(char code) {
  for (const char* letter = kTitleButtonLetters; *letter != '\0'; ++letter) {
    if (*letter == code) {
      return true;
    }
  }
  return false;
}

const char* title_button_label(char code) {
  switch (code) {
    case 'O':
      return "Menu";
    case 'T':
      return "Stick";
    case 'H':
      return "Hide";
    case 'S':
      return "Shade";
    case 'M':
      return "Maximize";
    case 'C':
      return "Close";
    default:
      return "";
  }
}

TitleBarButtons parse_button_layout(const std::string& layout) {
  TitleBarButtons buttons;
  bool right = false;
  std::string seen;
  for (char raw : layout) {
    if (raw == '|') {
      right = true;
      continue;
    }
    const char code = canonical_button(raw);
    if (code == '\0' || seen.find(code) != std::string::npos) {
      continue;
    }
    seen.push_back(code);
    push_unique(right ? buttons.right : buttons.left, code);
  }
  return buttons;
}

std::string format_button_layout(const TitleBarButtons& buttons) {
  return buttons.left + "|" + buttons.right;
}

std::string hidden_title_buttons(const TitleBarButtons& buttons) {
  std::string hidden;
  for (const char* letter = kTitleButtonLetters; *letter != '\0'; ++letter) {
    if (buttons.left.find(*letter) == std::string::npos &&
        buttons.right.find(*letter) == std::string::npos) {
      hidden.push_back(*letter);
    }
  }
  return hidden;
}

TitleBarButtons adjust_button_layout(TitleBarButtons buttons, char code, const char* action) {
  code = canonical_button(code);
  if (code == '\0' || action == nullptr) {
    return buttons;
  }
  const std::string op = action;
  const bool on_left = buttons.left.find(code) != std::string::npos;
  const bool on_right = buttons.right.find(code) != std::string::npos;
  if (op == "up" || op == "down") {
    const int delta = op == "up" ? -1 : 1;
    if (on_left) {
      move_within(buttons.left, code, delta);
    } else if (on_right) {
      move_within(buttons.right, code, delta);
    }
    return buttons;
  }
  if (op == "hide") {
    buttons.left = without(buttons.left, code);
    buttons.right = without(buttons.right, code);
    return buttons;
  }
  if (op == "left") {
    if (on_left) {
      return buttons;
    }
    buttons.right = without(buttons.right, code);
    push_unique(buttons.left, code);
    return buttons;
  }
  if (op == "right") {
    if (on_right) {
      return buttons;
    }
    buttons.left = without(buttons.left, code);
    push_unique(buttons.right, code);
    return buttons;
  }
  return buttons;
}

}  // namespace lcos
