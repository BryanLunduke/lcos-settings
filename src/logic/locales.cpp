// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "locales.hpp"

#include <clocale>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <locale.h>
#include <monetary.h>

namespace lcos {
namespace {

struct Name {
  const char* code;
  const char* name;
};

const Name kLanguages[] = {
    {"aa", "Afar"},       {"af", "Afrikaans"},  {"am", "Amharic"},    {"ar", "Arabic"},
    {"as", "Assamese"},   {"az", "Azerbaijani"}, {"be", "Belarusian"}, {"bg", "Bulgarian"},
    {"bn", "Bangla"},     {"bo", "Tibetan"},    {"br", "Breton"},     {"bs", "Bosnian"},
    {"ca", "Catalan"},    {"cs", "Czech"},      {"cy", "Welsh"},      {"da", "Danish"},
    {"de", "German"},     {"dz", "Dzongkha"},   {"el", "Greek"},      {"en", "English"},
    {"eo", "Esperanto"},  {"es", "Spanish"},    {"et", "Estonian"},   {"eu", "Basque"},
    {"fa", "Persian"},    {"fi", "Finnish"},    {"fo", "Faroese"},    {"fr", "French"},
    {"fy", "Western Frisian"}, {"ga", "Irish"}, {"gd", "Scottish Gaelic"}, {"gl", "Galician"},
    {"gu", "Gujarati"},   {"he", "Hebrew"},     {"hi", "Hindi"},      {"hr", "Croatian"},
    {"hu", "Hungarian"},  {"hy", "Armenian"},   {"id", "Indonesian"}, {"is", "Icelandic"},
    {"it", "Italian"},    {"ja", "Japanese"},   {"ka", "Georgian"},   {"kk", "Kazakh"},
    {"km", "Khmer"},      {"kn", "Kannada"},    {"ko", "Korean"},     {"ku", "Kurdish"},
    {"ky", "Kyrgyz"},     {"lb", "Luxembourgish"}, {"lo", "Lao"},     {"lt", "Lithuanian"},
    {"lv", "Latvian"},    {"mg", "Malagasy"},   {"mi", "Maori"},      {"mk", "Macedonian"},
    {"ml", "Malayalam"},  {"mn", "Mongolian"},  {"mr", "Marathi"},    {"ms", "Malay"},
    {"mt", "Maltese"},    {"my", "Burmese"},    {"nb", "Norwegian Bokmål"}, {"ne", "Nepali"},
    {"nl", "Dutch"},      {"nn", "Norwegian Nynorsk"}, {"oc", "Occitan"}, {"or", "Odia"},
    {"pa", "Punjabi"},    {"pl", "Polish"},     {"ps", "Pashto"},     {"pt", "Portuguese"},
    {"ro", "Romanian"},   {"ru", "Russian"},    {"rw", "Kinyarwanda"}, {"se", "Northern Sami"},
    {"si", "Sinhala"},    {"sk", "Slovak"},     {"sl", "Slovenian"},  {"sq", "Albanian"},
    {"sr", "Serbian"},    {"sv", "Swedish"},    {"sw", "Swahili"},    {"ta", "Tamil"},
    {"te", "Telugu"},     {"tg", "Tajik"},      {"th", "Thai"},       {"tk", "Turkmen"},
    {"tl", "Tagalog"},    {"tr", "Turkish"},    {"tt", "Tatar"},      {"ug", "Uyghur"},
    {"uk", "Ukrainian"},  {"ur", "Urdu"},       {"uz", "Uzbek"},      {"vi", "Vietnamese"},
    {"wa", "Walloon"},    {"wo", "Wolof"},      {"xh", "Xhosa"},      {"zh", "Chinese"},
    {"zu", "Zulu"},
};

const Name kCountries[] = {
    {"AD", "Andorra"},     {"AE", "United Arab Emirates"}, {"AF", "Afghanistan"},
    {"AL", "Albania"},     {"AM", "Armenia"},  {"AR", "Argentina"},  {"AT", "Austria"},
    {"AU", "Australia"},   {"AZ", "Azerbaijan"}, {"BA", "Bosnia and Herzegovina"},
    {"BD", "Bangladesh"},  {"BE", "Belgium"},  {"BG", "Bulgaria"},   {"BH", "Bahrain"},
    {"BO", "Bolivia"},     {"BR", "Brazil"},   {"BT", "Bhutan"},     {"BW", "Botswana"},
    {"BY", "Belarus"},     {"CA", "Canada"},   {"CH", "Switzerland"}, {"CL", "Chile"},
    {"CN", "China"},       {"CO", "Colombia"}, {"CR", "Costa Rica"}, {"CU", "Cuba"},
    {"CY", "Cyprus"},      {"CZ", "Czechia"},  {"DE", "Germany"},    {"DK", "Denmark"},
    {"DO", "Dominican Republic"}, {"DZ", "Algeria"}, {"EC", "Ecuador"}, {"EE", "Estonia"},
    {"EG", "Egypt"},       {"ES", "Spain"},    {"ET", "Ethiopia"},   {"FI", "Finland"},
    {"FO", "Faroe Islands"}, {"FR", "France"}, {"GB", "United Kingdom"}, {"GE", "Georgia"},
    {"GR", "Greece"},      {"GT", "Guatemala"}, {"HK", "Hong Kong"}, {"HR", "Croatia"},
    {"HU", "Hungary"},     {"ID", "Indonesia"}, {"IE", "Ireland"},   {"IL", "Israel"},
    {"IN", "India"},       {"IQ", "Iraq"},     {"IR", "Iran"},       {"IS", "Iceland"},
    {"IT", "Italy"},       {"JO", "Jordan"},   {"JP", "Japan"},      {"KE", "Kenya"},
    {"KG", "Kyrgyzstan"},  {"KH", "Cambodia"}, {"KR", "South Korea"}, {"KW", "Kuwait"},
    {"KZ", "Kazakhstan"},  {"LA", "Laos"},     {"LB", "Lebanon"},    {"LK", "Sri Lanka"},
    {"LT", "Lithuania"},   {"LU", "Luxembourg"}, {"LV", "Latvia"},   {"LY", "Libya"},
    {"MA", "Morocco"},     {"MD", "Moldova"},  {"ME", "Montenegro"}, {"MK", "North Macedonia"},
    {"MM", "Myanmar"},     {"MN", "Mongolia"}, {"MT", "Malta"},      {"MX", "Mexico"},
    {"MY", "Malaysia"},    {"NG", "Nigeria"},  {"NL", "Netherlands"}, {"NO", "Norway"},
    {"NP", "Nepal"},       {"NZ", "New Zealand"}, {"OM", "Oman"},    {"PA", "Panama"},
    {"PE", "Peru"},        {"PH", "Philippines"}, {"PK", "Pakistan"}, {"PL", "Poland"},
    {"PT", "Portugal"},    {"PY", "Paraguay"}, {"QA", "Qatar"},      {"RO", "Romania"},
    {"RS", "Serbia"},      {"RU", "Russia"},   {"SA", "Saudi Arabia"}, {"SE", "Sweden"},
    {"SG", "Singapore"},   {"SI", "Slovenia"}, {"SK", "Slovakia"},   {"SY", "Syria"},
    {"TH", "Thailand"},    {"TJ", "Tajikistan"}, {"TM", "Turkmenistan"}, {"TN", "Tunisia"},
    {"TR", "Turkey"},      {"TW", "Taiwan"},   {"TZ", "Tanzania"},   {"UA", "Ukraine"},
    {"US", "United States"}, {"UY", "Uruguay"}, {"UZ", "Uzbekistan"}, {"VE", "Venezuela"},
    {"VN", "Vietnam"},     {"ZA", "South Africa"}, {"ZW", "Zimbabwe"},
};

const char* lookup(const Name* table, std::size_t count, const std::string& code) {
  for (std::size_t i = 0; i < count; ++i) {
    if (code == table[i].code) {
      return table[i].name;
    }
  }
  return nullptr;
}

}  // namespace

std::string locale_display_name(const std::string& code) {
  if (code.empty() || code == "C" || code == "POSIX" || code == "C.UTF-8") {
    return code;
  }
  std::string body = code;
  const auto dot = body.find('.');
  if (dot != std::string::npos) {
    body.erase(dot);
  }
  std::string modifier;
  const auto at = body.find('@');
  if (at != std::string::npos) {
    modifier = body.substr(at + 1);
    body.erase(at);
  }
  const auto split = body.find('_');
  const std::string lang = split == std::string::npos ? body : body.substr(0, split);
  const std::string country = split == std::string::npos ? std::string() : body.substr(split + 1);
  const char* language = lookup(kLanguages, sizeof kLanguages / sizeof kLanguages[0], lang);
  const char* place =
      country.empty() ? nullptr
                      : lookup(kCountries, sizeof kCountries / sizeof kCountries[0], country);
  std::string out;
  if (language != nullptr && place != nullptr) {
    out = std::string(language) + " (" + place + ")";
  } else if (language != nullptr) {
    out = language;
  } else {
    return code;
  }
  if (!modifier.empty()) {
    out += ", ";
    out += modifier;
  }
  return out;
}

std::string locale_sample(const std::string& code) {
  if (code.empty()) {
    return {};
  }
  std::string spec = code;
  locale_t loc = newlocale(LC_TIME_MASK | LC_NUMERIC_MASK | LC_MONETARY_MASK, spec.c_str(),
                           static_cast<locale_t>(0));
  if (loc == static_cast<locale_t>(0) && spec.find('.') == std::string::npos && spec != "C" &&
      spec != "POSIX") {
    spec += ".UTF-8";
    loc = newlocale(LC_TIME_MASK | LC_NUMERIC_MASK | LC_MONETARY_MASK, spec.c_str(),
                    static_cast<locale_t>(0));
  }
  if (loc == static_cast<locale_t>(0)) {
    return {};
  }
  const locale_t previous = uselocale(loc);
  char date[80] = {};
  std::tm when{};
  when.tm_year = 2026 - 1900;
  when.tm_mon = 2;
  when.tm_mday = 14;
  std::strftime(date, sizeof date, "%x", &when);
  char number[80] = {};
  std::snprintf(number, sizeof number, "%'.2f", 1234.56);
  char money[80] = {};
  if (strfmon(money, sizeof money, "%n", 1234.56) < 0) {
    money[0] = '\0';
  }
  uselocale(previous == static_cast<locale_t>(0) ? LC_GLOBAL_LOCALE : previous);
  freelocale(loc);
  std::string out = date;
  if (number[0] != '\0') {
    out += "    ";
    out += number;
  }
  if (money[0] != '\0') {
    out += "    ";
    out += money;
  }
  return out;
}

}  // namespace lcos
