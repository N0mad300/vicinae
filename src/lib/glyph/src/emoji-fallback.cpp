#include "glyph/emoji.hpp"
#include "glyph/glyph.hpp"
#include <array>
#include <string>

namespace emoji {

static constexpr std::array<SkinToneInfo, skinToneCount> skinToneInfos = {
    SkinToneInfo{.tone = SkinTone::Default, .id = "default", .displayName = "Default", .utf8 = ""},
    SkinToneInfo{.tone = SkinTone::Light, .id = "light", .displayName = "Light", .utf8 = "\xF0\x9F\x8F\xBB"},
    SkinToneInfo{.tone = SkinTone::MediumLight,
                 .id = "medium-light",
                 .displayName = "Medium Light",
                 .utf8 = "\xF0\x9F\x8F\xBC"},
    SkinToneInfo{
        .tone = SkinTone::Medium, .id = "medium", .displayName = "Medium", .utf8 = "\xF0\x9F\x8F\xBD"},
    SkinToneInfo{.tone = SkinTone::MediumDark,
                 .id = "medium-dark",
                 .displayName = "Medium Dark",
                 .utf8 = "\xF0\x9F\x8F\xBE"},
    SkinToneInfo{.tone = SkinTone::Dark, .id = "dark", .displayName = "Dark", .utf8 = "\xF0\x9F\x8F\xBF"},
};

std::span<const SkinToneInfo> skinTones() { return skinToneInfos; }

SkinToneInfo skinToneInfo(SkinTone tone) { return skinToneInfos.at(static_cast<std::uint8_t>(tone)); }

static constexpr std::string_view kVariationSelector16 = "\xEF\xB8\x8F";

static std::string stripVariationSelectors(std::string_view str) {
  std::string result;
  result.reserve(str.size());

  size_t pos = 0;
  while (pos < str.size()) {
    if (pos + kVariationSelector16.size() <= str.size() &&
        str.substr(pos, kVariationSelector16.size()) == kVariationSelector16) {
      pos += kVariationSelector16.size();
    } else {
      result.push_back(str[pos]);
      pos++;
    }
  }

  return result;
}

static size_t firstUtf8CodepointSize(std::string_view str) {
  if (str.empty()) return 0;

  auto const first = static_cast<unsigned char>(str.front());
  if ((first & 0x80) == 0) return 1;
  if ((first & 0xE0) == 0xC0) return std::min<size_t>(2, str.size());
  if ((first & 0xF0) == 0xE0) return std::min<size_t>(3, str.size());
  if ((first & 0xF8) == 0xF0) return std::min<size_t>(4, str.size());
  return 1;
}

std::string applySkinTone(std::string_view emoji, SkinTone tone) {
  auto const info = skinToneInfo(tone);
  auto const firstSize = firstUtf8CodepointSize(emoji);

  std::string result;
  result.reserve(emoji.size() + info.utf8.size());
  result.append(emoji.substr(0, firstSize));
  result.append(info.utf8);
  result.append(stripVariationSelectors(emoji.substr(firstSize)));
  return result;
}

bool isUtf8EncodedEmoji(std::string_view str) {
  auto const *item = glyph::lookup(str);
  return item && item->kind == glyph::Kind::Emoji;
}

} // namespace emoji