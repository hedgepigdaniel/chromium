// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/font_fallback.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/lazy_instance.h"
#include "base/memory/ptr_util.h"
#include "third_party/icu/source/common/unicode/uscript.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/ports/SkFontMgr.h"
#include "ui/gfx/platform_font_linux.h"

namespace vr_shell {

namespace {

enum KnownGlyph {
  UNDEFINED,
  UNKNOWN,
  KNOWN,
};

class CachedFont {
 public:
  static std::unique_ptr<CachedFont> CreateForTypeface(
      sk_sp<SkTypeface> typeface) {
    return base::WrapUnique<CachedFont>(new CachedFont(std::move(typeface)));
  }

  bool HasGlyphForCharacter(UChar32 character) {
    // In order to increase cache hits, the cache is script based rather than
    // character based. This also limits the cache size to the number of Unicode
    // scripts (174 at the time of writing).
    UErrorCode err = UErrorCode::U_ZERO_ERROR;
    UScriptCode script = uscript_getScript(character, &err);
    if (!U_SUCCESS(err) || script == UScriptCode::USCRIPT_INVALID_CODE)
      return false;
    auto& supported = supported_scripts_[script];
    if (supported != UNDEFINED)
      return supported == KNOWN;
    uint16_t glyph_id;
    paint_.textToGlyphs(&character, sizeof(UChar32), &glyph_id);
    supported = glyph_id ? KNOWN : UNKNOWN;
    return glyph_id;
  }
  std::string GetFontName() { return name_; }

 private:
  explicit CachedFont(sk_sp<SkTypeface> skia_face) {
    SkString sk_name;
    skia_face->getFamilyName(&sk_name);
    name_ = std::string(sk_name.c_str(), sk_name.size());
    paint_.setTypeface(std::move(skia_face));
    paint_.setTextEncoding(SkPaint::kUTF32_TextEncoding);
  }

  SkPaint paint_;
  std::map<UScriptCode, KnownGlyph> supported_scripts_;
  std::string name_;

  DISALLOW_COPY_AND_ASSIGN(CachedFont);
};

using FontCache = std::map<SkFontID, std::unique_ptr<CachedFont>>;
base::LazyInstance<FontCache>::Leaky g_fonts = LAZY_INSTANCE_INITIALIZER;

class CachedFontSet {
 public:
  CachedFontSet() : locale_() {}
  ~CachedFontSet() = default;

  void SetLocale(const std::string& locale) {
    // Store font list for one locale at a time.
    if (locale != locale_) {
      font_ids_.clear();
      unknown_chars_.clear();
      locale_ = locale;
    }
  }

  std::string GetFallbackFontNameForChar(UChar32 c) {
    if (unknown_chars_.find(c) != unknown_chars_.end())
      return "";
    for (SkFontID font_id : font_ids_) {
      std::unique_ptr<CachedFont>& font = g_fonts.Get()[font_id];
      if (font->HasGlyphForCharacter(c))
        return font->GetFontName();
    }
    sk_sp<SkFontMgr> font_mgr(SkFontMgr::RefDefault());
    const char* bcp47_locales[] = {locale_.c_str()};
    sk_sp<SkTypeface> tf(font_mgr->matchFamilyStyleCharacter(
        nullptr, SkFontStyle(), locale_.empty() ? nullptr : bcp47_locales,
        locale_.empty() ? 0 : 1, c));
    if (tf) {
      SkFontID font_id = tf->uniqueID();
      font_ids_.push_back(font_id);
      std::unique_ptr<CachedFont>& cached_font = g_fonts.Get()[font_id];
      if (!cached_font)
        cached_font = CachedFont::CreateForTypeface(tf);
      return cached_font->GetFontName();
    }
    unknown_chars_.insert(c);
    return std::string();
  }

 private:
  std::string locale_;
  std::vector<SkFontID> font_ids_;
  std::set<UChar32> unknown_chars_;

  DISALLOW_COPY_AND_ASSIGN(CachedFontSet);
};

base::LazyInstance<CachedFontSet>::Leaky g_cached_font_set =
    LAZY_INSTANCE_INITIALIZER;

bool FontSupportsChar(const gfx::Font& font, UChar32 c) {
  sk_sp<SkTypeface> typeface =
      static_cast<gfx::PlatformFontLinux*>(font.platform_font())->typeface();
  std::unique_ptr<CachedFont>& cached_font =
      g_fonts.Get()[typeface->uniqueID()];
  if (!cached_font)
    cached_font = CachedFont::CreateForTypeface(std::move(typeface));
  return cached_font->HasGlyphForCharacter(c);
}

}  // namespace

std::string GetFallbackFontNameForChar(const gfx::Font& default_font,
                                       UChar32 c,
                                       const std::string& locale) {
  if (FontSupportsChar(default_font, c))
    return std::string();
  CachedFontSet& cached_font_set = g_cached_font_set.Get();
  cached_font_set.SetLocale(locale);
  return cached_font_set.GetFallbackFontNameForChar(c);
}

}  // namespace vr_shell
