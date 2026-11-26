// SPDX-License-Identifier: GPL-3.0-or-later

#include "logic/rollback_journal.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace lcos {
namespace {

std::string encode(const std::string& text) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (unsigned char character : text) {
    const bool plain = std::isalnum(character) || character == '-' || character == '_' ||
                       character == '.' || character == '/' || character == ' ';
    if (plain) {
      out.push_back(static_cast<char>(character));
    } else {
      out.push_back('%');
      out.push_back(hex[character >> 4]);
      out.push_back(hex[character & 0x0f]);
    }
  }
  return out;
}

bool decode(const std::string& text, std::string& out, std::string* error) {
  out.clear();
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '%') {
      out.push_back(text[i]);
      continue;
    }
    if (i + 2 >= text.size() || !std::isxdigit(static_cast<unsigned char>(text[i + 1])) ||
        !std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
      if (error != nullptr) {
        *error = "Bad escape in rollback journal.";
      }
      return false;
    }
    const int value = std::stoi(text.substr(i + 1, 2), nullptr, 16);
    out.push_back(static_cast<char>(value));
    i += 2;
  }
  return true;
}

const char* type_name(JournalType type) {
  switch (type) {
    case JournalType::Absent:
      return "absent";
    case JournalType::Bool:
      return "bool";
    case JournalType::Int:
      return "int";
    case JournalType::Uint:
      return "uint";
    case JournalType::Double:
      return "double";
    case JournalType::String:
      return "string";
    case JournalType::Ints:
      return "ints";
    case JournalType::Doubles:
      return "doubles";
    case JournalType::Strings:
      return "strings";
  }
  return "absent";
}

bool type_from(const std::string& name, JournalType& type) {
  if (name == "absent") {
    type = JournalType::Absent;
  } else if (name == "bool") {
    type = JournalType::Bool;
  } else if (name == "int") {
    type = JournalType::Int;
  } else if (name == "uint") {
    type = JournalType::Uint;
  } else if (name == "double") {
    type = JournalType::Double;
  } else if (name == "string") {
    type = JournalType::String;
  } else if (name == "ints") {
    type = JournalType::Ints;
  } else if (name == "doubles") {
    type = JournalType::Doubles;
  } else if (name == "strings") {
    type = JournalType::Strings;
  } else {
    return false;
  }
  return true;
}

std::string payload_of(const JournalEntry& entry) {
  switch (entry.type) {
    case JournalType::Absent:
      return {};
    case JournalType::Bool:
      return entry.b ? "1" : "0";
    case JournalType::Int:
      return std::to_string(entry.i);
    case JournalType::Uint:
      return std::to_string(entry.u);
    case JournalType::Double: {
      char buffer[64];
      std::snprintf(buffer, sizeof buffer, "%.17g", entry.d);
      return buffer;
    }
    case JournalType::String:
      return encode(entry.s);
    case JournalType::Ints: {
      std::ostringstream out;
      for (std::size_t i = 0; i < entry.ints.size(); ++i) {
        if (i != 0) {
          out << ',';
        }
        out << entry.ints[i];
      }
      return out.str();
    }
    case JournalType::Doubles: {
      std::ostringstream out;
      for (std::size_t i = 0; i < entry.doubles.size(); ++i) {
        if (i != 0) {
          out << ',';
        }
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.17g", entry.doubles[i]);
        out << buffer;
      }
      return out.str();
    }
    case JournalType::Strings: {
      std::ostringstream out;
      for (std::size_t i = 0; i < entry.strings.size(); ++i) {
        if (i != 0) {
          out << ',';
        }
        out << encode(entry.strings[i]);
      }
      return out.str();
    }
  }
  return {};
}

std::vector<std::string> split_char(const std::string& text, char mark) {
  std::vector<std::string> parts;
  std::string current;
  for (char character : text) {
    if (character == mark) {
      parts.push_back(current);
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  parts.push_back(current);
  return parts;
}

bool fill_payload(JournalEntry& entry, const std::string& payload, std::string* error) {
  try {
    switch (entry.type) {
      case JournalType::Absent:
        return true;
      case JournalType::Bool:
        if (payload != "0" && payload != "1") {
          if (error != nullptr) {
            *error = "Bad boolean in rollback journal.";
          }
          return false;
        }
        entry.b = payload == "1";
        return true;
      case JournalType::Int:
        entry.i = std::stoi(payload);
        return true;
      case JournalType::Uint:
        entry.u = static_cast<unsigned>(std::stoul(payload));
        return true;
      case JournalType::Double:
        entry.d = std::stod(payload);
        return std::isfinite(entry.d);
      case JournalType::String:
        return decode(payload, entry.s, error);
      case JournalType::Ints: {
        if (payload.empty()) {
          return true;
        }
        for (const auto& part : split_char(payload, ',')) {
          entry.ints.push_back(std::stoi(part));
        }
        return true;
      }
      case JournalType::Doubles: {
        if (payload.empty()) {
          return true;
        }
        for (const auto& part : split_char(payload, ',')) {
          const double value = std::stod(part);
          if (!std::isfinite(value)) {
            return false;
          }
          entry.doubles.push_back(value);
        }
        return true;
      }
      case JournalType::Strings: {
        if (payload.empty()) {
          return true;
        }
        for (const auto& part : split_char(payload, ',')) {
          std::string value;
          if (!decode(part, value, error)) {
            return false;
          }
          entry.strings.push_back(value);
        }
        return true;
      }
    }
  } catch (const std::exception&) {
    if (error != nullptr) {
      *error = "Bad number in rollback journal.";
    }
    return false;
  }
  return false;
}

std::vector<std::string> lines_of(const std::string& text) {
  std::vector<std::string> lines;
  std::string current;
  for (char character : text) {
    if (character == '\n') {
      if (!current.empty() && current.back() == '\r') {
        current.pop_back();
      }
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  return lines;
}

std::string volumes_text(const std::vector<unsigned>& volumes) {
  std::ostringstream out;
  for (std::size_t i = 0; i < volumes.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    out << volumes[i];
  }
  return out.str();
}

bool volumes_from(const std::string& text, std::vector<unsigned>& volumes) {
  volumes.clear();
  if (text.empty()) {
    return true;
  }
  try {
    for (const auto& part : split_char(text, ',')) {
      volumes.push_back(static_cast<unsigned>(std::stoul(part)));
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

}  // namespace

std::string format_rollback_journal(const std::vector<JournalEntry>& entries) {
  std::ostringstream out;
  out << "lcos-rollback 1\n" << entries.size() << '\n';
  for (const auto& entry : entries) {
    out << encode(entry.channel) << '\t' << encode(entry.property) << '\t' << encode(entry.page)
        << '\t' << type_name(entry.type) << '\t' << payload_of(entry) << '\n';
  }
  return out.str();
}

bool parse_rollback_journal(const std::string& text, std::vector<JournalEntry>& entries,
                            std::string* error) {
  entries.clear();
  const auto lines = lines_of(text);
  if (lines.empty() || lines[0] != "lcos-rollback 1") {
    if (error != nullptr) {
      *error = "Rollback journal is missing or has an unknown version.";
    }
    return false;
  }
  if (lines.size() < 2) {
    if (error != nullptr) {
      *error = "Rollback journal is truncated.";
    }
    return false;
  }
  int count = 0;
  try {
    std::size_t used = 0;
    count = std::stoi(lines[1], &used);
    if (used != lines[1].size() || count < 0) {
      throw std::invalid_argument("count");
    }
  } catch (const std::exception&) {
    if (error != nullptr) {
      *error = "Rollback journal has a bad count.";
    }
    return false;
  }
  if (static_cast<int>(lines.size()) < count + 2) {
    if (error != nullptr) {
      *error = "Rollback journal is truncated.";
    }
    return false;
  }
  entries.reserve(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index) {
    const auto fields = split_char(lines[static_cast<std::size_t>(index + 2)], '\t');
    if (fields.size() != 5) {
      if (error != nullptr) {
        *error = "Rollback journal has a short record.";
      }
      return false;
    }
    JournalEntry entry;
    if (!decode(fields[0], entry.channel, error) || !decode(fields[1], entry.property, error) ||
        !decode(fields[2], entry.page, error) || !type_from(fields[3], entry.type) ||
        !fill_payload(entry, fields[4], error)) {
      if (error != nullptr && error->empty()) {
        *error = "Rollback journal has a bad record.";
      }
      entries.clear();
      return false;
    }
    entries.push_back(std::move(entry));
  }
  return true;
}

bool SoundRollback::any() const {
  return output_device || output_volume || output_mute_set || output_port || input_device ||
         input_volume || input_mute_set || input_port || profile || !streams.empty();
}

std::string format_sound_rollback(const SoundRollback& rollback) {
  std::ostringstream out;
  out << "lcos-sound-rollback 1\n";
  out << "output-device\t" << (rollback.output_device ? '1' : '0') << '\t'
      << encode(rollback.output_name) << '\n';
  out << "output-volume\t" << (rollback.output_volume ? '1' : '0') << '\t'
      << volumes_text(rollback.output_volumes) << '\n';
  out << "output-mute\t" << (rollback.output_mute_set ? '1' : '0') << '\t'
      << (rollback.output_mute ? '1' : '0') << '\n';
  out << "output-port\t" << (rollback.output_port ? '1' : '0') << '\t'
      << encode(rollback.output_port_name) << '\n';
  out << "input-device\t" << (rollback.input_device ? '1' : '0') << '\t'
      << encode(rollback.input_name) << '\n';
  out << "input-volume\t" << (rollback.input_volume ? '1' : '0') << '\t'
      << volumes_text(rollback.input_volumes) << '\n';
  out << "input-mute\t" << (rollback.input_mute_set ? '1' : '0') << '\t'
      << (rollback.input_mute ? '1' : '0') << '\n';
  out << "input-port\t" << (rollback.input_port ? '1' : '0') << '\t'
      << encode(rollback.input_port_name) << '\n';
  out << "profile\t" << (rollback.profile ? '1' : '0') << '\t' << encode(rollback.profile_card)
      << '\t' << encode(rollback.profile_name) << '\n';
  out << "streams\t" << rollback.streams.size() << '\n';
  for (const auto& stream : rollback.streams) {
    out << "stream\t" << encode(stream.application) << '\t' << encode(stream.media) << '\t'
        << (stream.mute ? '1' : '0') << '\t' << volumes_text(stream.volumes) << '\n';
  }
  return out.str();
}

bool parse_sound_rollback(const std::string& text, SoundRollback& rollback, std::string* error) {
  rollback = {};
  const auto lines = lines_of(text);
  if (lines.empty() || lines[0] != "lcos-sound-rollback 1") {
    if (error != nullptr) {
      *error = "Sound rollback is missing or has an unknown version.";
    }
    return false;
  }
  int expect_streams = -1;
  for (std::size_t i = 1; i < lines.size(); ++i) {
    const auto fields = split_char(lines[i], '\t');
    if (fields.empty()) {
      continue;
    }
    const std::string& key = fields[0];
    auto flag = [&](std::size_t index, bool& on) {
      if (fields.size() <= index || (fields[index] != "0" && fields[index] != "1")) {
        return false;
      }
      on = fields[index] == "1";
      return true;
    };
    try {
      if (key == "output-device" || key == "input-device" || key == "output-port" ||
          key == "input-port") {
        bool on = false;
        std::string name;
        if (fields.size() < 3 || !flag(1, on) || !decode(fields[2], name, error)) {
          return false;
        }
        if (key == "output-device") {
          rollback.output_device = on;
          rollback.output_name = name;
        } else if (key == "input-device") {
          rollback.input_device = on;
          rollback.input_name = name;
        } else if (key == "output-port") {
          rollback.output_port = on;
          rollback.output_port_name = name;
        } else {
          rollback.input_port = on;
          rollback.input_port_name = name;
        }
      } else if (key == "output-volume" || key == "input-volume") {
        bool on = false;
        std::vector<unsigned> volumes;
        if (fields.size() < 3 || !flag(1, on) || !volumes_from(fields[2], volumes)) {
          if (error != nullptr) {
            *error = "Sound rollback has a bad volume.";
          }
          return false;
        }
        if (key == "output-volume") {
          rollback.output_volume = on;
          rollback.output_volumes = volumes;
        } else {
          rollback.input_volume = on;
          rollback.input_volumes = volumes;
        }
      } else if (key == "output-mute" || key == "input-mute") {
        bool on = false;
        bool muted = false;
        if (fields.size() < 3 || !flag(1, on) || !flag(2, muted)) {
          return false;
        }
        if (key == "output-mute") {
          rollback.output_mute_set = on;
          rollback.output_mute = muted;
        } else {
          rollback.input_mute_set = on;
          rollback.input_mute = muted;
        }
      } else if (key == "profile") {
        if (fields.size() < 4 || !flag(1, rollback.profile) ||
            !decode(fields[2], rollback.profile_card, error) ||
            !decode(fields[3], rollback.profile_name, error)) {
          return false;
        }
      } else if (key == "streams") {
        if (fields.size() < 2) {
          return false;
        }
        expect_streams = std::stoi(fields[1]);
        if (expect_streams < 0) {
          return false;
        }
      } else if (key == "stream") {
        SoundRollbackStream stream;
        if (fields.size() < 5 || !decode(fields[1], stream.application, error) ||
            !decode(fields[2], stream.media, error) || !flag(3, stream.mute) ||
            !volumes_from(fields[4], stream.volumes)) {
          return false;
        }
        rollback.streams.push_back(std::move(stream));
      } else {
        if (error != nullptr) {
          *error = "Sound rollback has an unknown record.";
        }
        return false;
      }
    } catch (const std::exception&) {
      if (error != nullptr) {
        *error = "Sound rollback has a bad record.";
      }
      return false;
    }
  }
  if (expect_streams >= 0 && static_cast<int>(rollback.streams.size()) != expect_streams) {
    if (error != nullptr) {
      *error = "Sound rollback is missing a stream.";
    }
    return false;
  }
  return true;
}

}  // namespace lcos
