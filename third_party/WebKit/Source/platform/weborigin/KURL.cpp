/*
 * Copyright (C) 2004, 2007, 2008, 2011, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2012 Research In Motion Limited. All rights reserved.
 * Copyright (C) 2008, 2009, 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/weborigin/KURL.h"

#include <algorithm>
#include "platform/weborigin/KnownPorts.h"
#include "platform/wtf/MathExtras.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/StringHash.h"
#include "platform/wtf/text/StringStatics.h"
#include "platform/wtf/text/StringUTF8Adaptor.h"
#include "platform/wtf/text/TextEncoding.h"
#include "url/url_util.h"
#ifndef NDEBUG
#include <stdio.h>
#endif

namespace blink {

static const int kMaximumValidPortNumber = 0xFFFE;
static const int kInvalidPortNumber = 0xFFFF;

#if DCHECK_IS_ON()
static void AssertProtocolIsGood(const StringView protocol) {
  DCHECK(protocol != "");
  for (size_t i = 0; i < protocol.length(); ++i) {
    LChar c = protocol.Characters8()[i];
    DCHECK(c > ' ' && c < 0x7F && !(c >= 'A' && c <= 'Z'));
  }
}
#endif

// Note: You must ensure that |spec| is a valid canonicalized URL before calling
// this function.
static const char* AsURLChar8Subtle(const String& spec) {
  DCHECK(spec.Is8Bit());
  // characters8 really return characters in Latin-1, but because we
  // canonicalize URL strings, we know that everything before the fragment
  // identifier will actually be ASCII, which means this cast is safe as long as
  // you don't look at the fragment component.
  return reinterpret_cast<const char*>(spec.Characters8());
}

// Returns the characters for the given string, or a pointer to a static empty
// string if the input string is null. This will always ensure we have a non-
// null character pointer since ReplaceComponents has special meaning for null.
static const char* CharactersOrEmpty(const StringUTF8Adaptor& string) {
  static const char kZero = 0;
  return string.Data() ? string.Data() : &kZero;
}

static bool IsSchemeFirstChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool IsSchemeChar(char c) {
  return IsSchemeFirstChar(c) || (c >= '0' && c <= '9') || c == '.' ||
         c == '-' || c == '+';
}

static bool IsUnicodeEncoding(const WTF::TextEncoding* encoding) {
  return encoding->EncodingForFormSubmission() == UTF8Encoding();
}

namespace {

class KURLCharsetConverter final : public url::CharsetConverter {
  DISALLOW_NEW();

 public:
  // The encoding parameter may be 0, but in this case the object must not be
  // called.
  explicit KURLCharsetConverter(const WTF::TextEncoding* encoding)
      : encoding_(encoding) {}

  void ConvertFromUTF16(const base::char16* input,
                        int input_length,
                        url::CanonOutput* output) override {
    CString encoded = encoding_->Encode(
        String(input, input_length), WTF::kURLEncodedEntitiesForUnencodables);
    output->Append(encoded.data(), static_cast<int>(encoded.length()));
  }

 private:
  const WTF::TextEncoding* encoding_;
};

}  // namespace

bool IsValidProtocol(const String& protocol) {
  // RFC3986: ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
  if (protocol.IsEmpty())
    return false;
  if (!IsSchemeFirstChar(protocol[0]))
    return false;
  unsigned protocol_length = protocol.length();
  for (unsigned i = 1; i < protocol_length; i++) {
    if (!IsSchemeChar(protocol[i]))
      return false;
  }
  return true;
}

void KURL::Initialize() {
  // This must be called before we create other threads to
  // avoid racy static local initialization.
  BlankURL();
}

String KURL::StrippedForUseAsReferrer() const {
  if (!ProtocolIsInHTTPFamily())
    return String();

  if (parsed_.username.is_nonempty() || parsed_.password.is_nonempty() ||
      parsed_.ref.is_valid()) {
    KURL referrer(*this);
    referrer.SetUser(String());
    referrer.SetPass(String());
    referrer.RemoveFragmentIdentifier();
    return referrer.GetString();
  }
  return GetString();
}

String KURL::StrippedForUseAsHref() const {
  if (parsed_.username.is_nonempty() || parsed_.password.is_nonempty()) {
    KURL href(*this);
    href.SetUser(String());
    href.SetPass(String());
    return href.GetString();
  }
  return GetString();
}

bool KURL::IsLocalFile() const {
  // Including feed here might be a bad idea since drag and drop uses this check
  // and including feed would allow feeds to potentially let someone's blog
  // read the contents of the clipboard on a drag, even without a drop.
  // Likewise with using the FrameLoader::shouldTreatURLAsLocal() function.
  return ProtocolIs("file");
}

bool ProtocolIsJavaScript(const String& url) {
  return ProtocolIs(url, "javascript");
}

const KURL& BlankURL() {
  DEFINE_STATIC_LOCAL(KURL, static_blank_url,
                      (kParsedURLString, "about:blank"));
  return static_blank_url;
}

bool KURL::IsAboutBlankURL() const {
  return *this == BlankURL();
}

const KURL& SrcdocURL() {
  DEFINE_STATIC_LOCAL(KURL, static_srcdoc_url,
                      (kParsedURLString, "about:srcdoc"));
  return static_srcdoc_url;
}

bool KURL::IsAboutSrcdocURL() const {
  return *this == SrcdocURL();
}

String KURL::ElidedString() const {
  if (GetString().length() <= 1024)
    return GetString();

  return GetString().Left(511) + "..." + GetString().Right(510);
}

KURL::KURL() : is_valid_(false), protocol_is_in_http_family_(false) {}

// Initializes with a string representing an absolute URL. No encoding
// information is specified. This generally happens when a KURL is converted
// to a string and then converted back. In this case, the URL is already
// canonical and in proper escaped form so needs no encoding. We treat it as
// UTF-8 just in case.
KURL::KURL(ParsedURLStringTag, const String& url) {
  if (!url.IsNull())
    Init(KURL(), url, 0);
  else {
    // WebCore expects us to preserve the nullness of strings when this
    // constructor is used. In all other cases, it expects a non-null
    // empty string, which is what init() will create.
    is_valid_ = false;
    protocol_is_in_http_family_ = false;
  }
}

KURL KURL::CreateIsolated(ParsedURLStringTag, const String& url) {
  // FIXME: We should be able to skip this extra copy and created an
  // isolated KURL more efficiently.
  return KURL(kParsedURLString, url).Copy();
}

// Constructs a new URL given a base URL and a possibly relative input URL.
// This assumes UTF-8 encoding.
KURL::KURL(const KURL& base, const String& relative) {
  Init(base, relative, 0);
}

// Constructs a new URL given a base URL and a possibly relative input URL.
// Any query portion of the relative URL will be encoded in the given encoding.
KURL::KURL(const KURL& base,
           const String& relative,
           const WTF::TextEncoding& encoding) {
  Init(base, relative, &encoding.EncodingForFormSubmission());
}

KURL::KURL(const AtomicString& canonical_string,
           const url::Parsed& parsed,
           bool is_valid)
    : is_valid_(is_valid),
      protocol_is_in_http_family_(false),
      parsed_(parsed),
      string_(canonical_string) {
  InitProtocolMetadata();
  InitInnerURL();
}

KURL::KURL(WTF::HashTableDeletedValueType)
    : is_valid_(false),
      protocol_is_in_http_family_(false),
      string_(WTF::kHashTableDeletedValue) {}

KURL::KURL(const KURL& other)
    : is_valid_(other.is_valid_),
      protocol_is_in_http_family_(other.protocol_is_in_http_family_),
      protocol_(other.protocol_),
      parsed_(other.parsed_),
      string_(other.string_) {
  if (other.inner_url_.get())
    inner_url_ = WTF::WrapUnique(new KURL(other.inner_url_->Copy()));
}

KURL::~KURL() {}

KURL& KURL::operator=(const KURL& other) {
  is_valid_ = other.is_valid_;
  protocol_is_in_http_family_ = other.protocol_is_in_http_family_;
  protocol_ = other.protocol_;
  parsed_ = other.parsed_;
  string_ = other.string_;
  if (other.inner_url_)
    inner_url_ = WTF::WrapUnique(new KURL(other.inner_url_->Copy()));
  else
    inner_url_.reset();
  return *this;
}

KURL KURL::Copy() const {
  KURL result;
  result.is_valid_ = is_valid_;
  result.protocol_is_in_http_family_ = protocol_is_in_http_family_;
  result.protocol_ = protocol_.IsolatedCopy();
  result.parsed_ = parsed_;
  result.string_ = string_.IsolatedCopy();
  if (inner_url_)
    result.inner_url_ = WTF::WrapUnique(new KURL(inner_url_->Copy()));
  return result;
}

bool KURL::IsNull() const {
  return string_.IsNull();
}

bool KURL::IsEmpty() const {
  return string_.IsEmpty();
}

bool KURL::IsValid() const {
  return is_valid_;
}

bool KURL::HasPort() const {
  return HostEnd() < PathStart();
}

bool KURL::ProtocolIsJavaScript() const {
  return ComponentStringView(parsed_.scheme) == "javascript";
}

bool KURL::ProtocolIsInHTTPFamily() const {
  return protocol_is_in_http_family_;
}

bool KURL::HasPath() const {
  // Note that http://www.google.com/" has a path, the path is "/". This can
  // return false only for invalid or nonstandard URLs.
  return parsed_.path.len >= 0;
}

String KURL::LastPathComponent() const {
  if (!is_valid_)
    return StringViewForInvalidComponent().ToString();
  DCHECK(!string_.IsNull());

  // When the output ends in a slash, WebCore has different expectations than
  // the GoogleURL library. For "/foo/bar/" the library will return the empty
  // string, but WebCore wants "bar".
  url::Component path = parsed_.path;
  if (path.len > 0 && string_[path.end() - 1] == '/')
    path.len--;

  url::Component file;
  if (string_.Is8Bit())
    url::ExtractFileName(AsURLChar8Subtle(string_), path, &file);
  else
    url::ExtractFileName(string_.Characters16(), path, &file);

  // Bug: https://bugs.webkit.org/show_bug.cgi?id=21015 this function returns
  // a null string when the path is empty, which we duplicate here.
  if (!file.is_nonempty())
    return String();
  return ComponentString(file);
}

String KURL::Protocol() const {
  DCHECK_EQ(ComponentString(parsed_.scheme), protocol_);
  return protocol_;
}

String KURL::Host() const {
  return ComponentString(parsed_.host);
}

// Returns 0 when there is no port.
//
// We treat URL's with out-of-range port numbers as invalid URLs, and they will
// be rejected by the canonicalizer. KURL.cpp will allow them in parsing, but
// return invalidPortNumber from this port() function, so we mirror that
// behavior here.
unsigned short KURL::Port() const {
  if (!is_valid_ || parsed_.port.len <= 0)
    return 0;
  DCHECK(!string_.IsNull());
  int port = string_.Is8Bit()
                 ? url::ParsePort(AsURLChar8Subtle(string_), parsed_.port)
                 : url::ParsePort(string_.Characters16(), parsed_.port);
  DCHECK_NE(port, url::PORT_UNSPECIFIED);  // Checked port.len <= 0 before.

  if (port == url::PORT_INVALID ||
      port > kMaximumValidPortNumber)  // Mimic KURL::port()
    port = kInvalidPortNumber;

  return static_cast<unsigned short>(port);
}

// TODO(csharrison): Migrate pass() and user() to return a StringView. Most
// consumers just need to know if the string is empty.

String KURL::Pass() const {
  // Bug: https://bugs.webkit.org/show_bug.cgi?id=21015 this function returns
  // a null string when the password is empty, which we duplicate here.
  if (!parsed_.password.is_nonempty())
    return String();
  return ComponentString(parsed_.password);
}

String KURL::User() const {
  return ComponentString(parsed_.username);
}

String KURL::FragmentIdentifier() const {
  // Empty but present refs ("foo.com/bar#") should result in the empty
  // string, which componentString will produce. Nonexistent refs
  // should be the null string.
  if (!parsed_.ref.is_valid())
    return String();
  return ComponentString(parsed_.ref);
}

bool KURL::HasFragmentIdentifier() const {
  return parsed_.ref.len >= 0;
}

String KURL::BaseAsString() const {
  // FIXME: There is probably a more efficient way to do this?
  return string_.Left(PathAfterLastSlash());
}

String KURL::Query() const {
  if (parsed_.query.len >= 0)
    return ComponentString(parsed_.query);

  // Bug: https://bugs.webkit.org/show_bug.cgi?id=21015 this function returns
  // an empty string when the query is empty rather than a null (not sure
  // which is right).
  // Returns a null if the query is not specified, instead of empty.
  if (parsed_.query.is_valid())
    return g_empty_string;
  return String();
}

String KURL::GetPath() const {
  return ComponentString(parsed_.path);
}

bool KURL::SetProtocol(const String& protocol) {
  // Firefox and IE remove everything after the first ':'.
  int separator_position = protocol.find(':');
  String new_protocol = protocol.Substring(0, separator_position);
  StringUTF8Adaptor new_protocol_utf8(new_protocol);

  // If KURL is given an invalid scheme, it returns failure without modifying
  // the URL at all. This is in contrast to most other setters which modify
  // the URL and set "m_isValid."
  url::RawCanonOutputT<char> canon_protocol;
  url::Component protocol_component;
  if (!url::CanonicalizeScheme(new_protocol_utf8.Data(),
                               url::Component(0, new_protocol_utf8.length()),
                               &canon_protocol, &protocol_component) ||
      !protocol_component.is_nonempty())
    return false;

  url::Replacements<char> replacements;
  replacements.SetScheme(CharactersOrEmpty(new_protocol_utf8),
                         url::Component(0, new_protocol_utf8.length()));
  ReplaceComponents(replacements);

  // isValid could be false but we still return true here. This is because
  // WebCore or JS scripts can build up a URL by setting individual
  // components, and a JS exception is based on the return value of this
  // function. We want to throw the exception and stop the script only when
  // its trying to set a bad protocol, and not when it maybe just hasn't
  // finished building up its final scheme.
  return true;
}

void KURL::SetHost(const String& host) {
  StringUTF8Adaptor host_utf8(host);
  url::Replacements<char> replacements;
  replacements.SetHost(CharactersOrEmpty(host_utf8),
                       url::Component(0, host_utf8.length()));
  ReplaceComponents(replacements);
}

static String ParsePortFromStringPosition(const String& value,
                                          unsigned port_start) {
  // "008080junk" needs to be treated as port "8080" and "000" as "0".
  size_t length = value.length();
  unsigned port_end = port_start;
  while (IsASCIIDigit(value[port_end]) && port_end < length)
    ++port_end;
  while (value[port_start] == '0' && port_start < port_end - 1)
    ++port_start;

  // Required for backwards compat.
  // https://www.w3.org/Bugs/Public/show_bug.cgi?id=23463
  if (port_start == port_end)
    return "0";

  return value.Substring(port_start, port_end - port_start);
}

void KURL::SetHostAndPort(const String& host_and_port) {
  size_t separator = host_and_port.find(':');
  if (!separator)
    return;

  if (separator == kNotFound) {
    url::Replacements<char> replacements;
    StringUTF8Adaptor host_utf8(host_and_port);
    replacements.SetHost(CharactersOrEmpty(host_utf8),
                         url::Component(0, host_utf8.length()));
    ReplaceComponents(replacements);
    return;
  }

  String host = host_and_port.Substring(0, separator);
  String port = ParsePortFromStringPosition(host_and_port, separator + 1);

  StringUTF8Adaptor host_utf8(host);
  StringUTF8Adaptor port_utf8(port);

  url::Replacements<char> replacements;
  replacements.SetHost(CharactersOrEmpty(host_utf8),
                       url::Component(0, host_utf8.length()));
  replacements.SetPort(CharactersOrEmpty(port_utf8),
                       url::Component(0, port_utf8.length()));
  ReplaceComponents(replacements);
}

void KURL::RemovePort() {
  if (!HasPort())
    return;
  url::Replacements<char> replacements;
  replacements.ClearPort();
  ReplaceComponents(replacements);
}

void KURL::SetPort(const String& port) {
  String parsed_port = ParsePortFromStringPosition(port, 0);
  SetPort(parsed_port.ToUInt());
}

void KURL::SetPort(unsigned short port) {
  if (IsDefaultPortForProtocol(port, Protocol())) {
    RemovePort();
    return;
  }

  String port_string = String::Number(port);
  DCHECK(port_string.Is8Bit());

  url::Replacements<char> replacements;
  replacements.SetPort(reinterpret_cast<const char*>(port_string.Characters8()),
                       url::Component(0, port_string.length()));
  ReplaceComponents(replacements);
}

void KURL::SetUser(const String& user) {
  // This function is commonly called to clear the username, which we
  // normally don't have, so we optimize this case.
  if (user.IsEmpty() && !parsed_.username.is_valid())
    return;

  // The canonicalizer will clear any usernames that are empty, so we
  // don't have to explicitly call ClearUsername() here.
  StringUTF8Adaptor user_utf8(user);
  url::Replacements<char> replacements;
  replacements.SetUsername(CharactersOrEmpty(user_utf8),
                           url::Component(0, user_utf8.length()));
  ReplaceComponents(replacements);
}

void KURL::SetPass(const String& pass) {
  // This function is commonly called to clear the password, which we
  // normally don't have, so we optimize this case.
  if (pass.IsEmpty() && !parsed_.password.is_valid())
    return;

  // The canonicalizer will clear any passwords that are empty, so we
  // don't have to explicitly call ClearUsername() here.
  StringUTF8Adaptor pass_utf8(pass);
  url::Replacements<char> replacements;
  replacements.SetPassword(CharactersOrEmpty(pass_utf8),
                           url::Component(0, pass_utf8.length()));
  ReplaceComponents(replacements);
}

void KURL::SetFragmentIdentifier(const String& fragment) {
  // This function is commonly called to clear the ref, which we
  // normally don't have, so we optimize this case.
  if (fragment.IsNull() && !parsed_.ref.is_valid())
    return;

  StringUTF8Adaptor fragment_utf8(fragment);

  url::Replacements<char> replacements;
  if (fragment.IsNull())
    replacements.ClearRef();
  else
    replacements.SetRef(CharactersOrEmpty(fragment_utf8),
                        url::Component(0, fragment_utf8.length()));
  ReplaceComponents(replacements);
}

void KURL::RemoveFragmentIdentifier() {
  url::Replacements<char> replacements;
  replacements.ClearRef();
  ReplaceComponents(replacements);
}

void KURL::SetQuery(const String& query) {
  StringUTF8Adaptor query_utf8(query);
  url::Replacements<char> replacements;
  if (query.IsNull()) {
    // KURL.cpp sets to null to clear any query.
    replacements.ClearQuery();
  } else if (query.length() > 0 && query[0] == '?') {
    // WebCore expects the query string to begin with a question mark, but
    // GoogleURL doesn't. So we trim off the question mark when setting.
    replacements.SetQuery(CharactersOrEmpty(query_utf8),
                          url::Component(1, query_utf8.length() - 1));
  } else {
    // When set with the empty string or something that doesn't begin with
    // a question mark, KURL.cpp will add a question mark for you. The only
    // way this isn't compatible is if you call this function with an empty
    // string. KURL.cpp will leave a '?' with nothing following it in the
    // URL, whereas we'll clear it.
    // FIXME We should eliminate this difference.
    replacements.SetQuery(CharactersOrEmpty(query_utf8),
                          url::Component(0, query_utf8.length()));
  }
  ReplaceComponents(replacements);
}

void KURL::SetPath(const String& path) {
  // Empty paths will be canonicalized to "/", so we don't have to worry
  // about calling ClearPath().
  StringUTF8Adaptor path_utf8(path);
  url::Replacements<char> replacements;
  replacements.SetPath(CharactersOrEmpty(path_utf8),
                       url::Component(0, path_utf8.length()));
  ReplaceComponents(replacements);
}

String DecodeURLEscapeSequences(const String& string) {
  return DecodeURLEscapeSequences(string, UTF8Encoding());
}

String DecodeURLEscapeSequences(const String& string,
                                const WTF::TextEncoding& encoding) {
  StringUTF8Adaptor string_utf8(string);
  url::RawCanonOutputT<base::char16> unescaped;
  url::DecodeURLEscapeSequences(string_utf8.Data(), string_utf8.length(),
                                &unescaped);
  return StringImpl::Create8BitIfPossible(
      reinterpret_cast<UChar*>(unescaped.data()), unescaped.length());
}

String EncodeWithURLEscapeSequences(const String& not_encoded_string) {
  CString utf8 = UTF8Encoding().Encode(not_encoded_string,
                                       WTF::kURLEncodedEntitiesForUnencodables);

  url::RawCanonOutputT<char> buffer;
  int input_length = utf8.length();
  if (buffer.capacity() < input_length * 3)
    buffer.Resize(input_length * 3);

  url::EncodeURIComponent(utf8.data(), input_length, &buffer);
  String escaped(buffer.data(), buffer.length());
  // Unescape '/'; it's safe and much prettier.
  escaped.Replace("%2F", "/");
  return escaped;
}

bool KURL::IsHierarchical() const {
  if (string_.IsNull() || !parsed_.scheme.is_nonempty())
    return false;
  return string_.Is8Bit()
             ? url::IsStandard(AsURLChar8Subtle(string_), parsed_.scheme)
             : url::IsStandard(string_.Characters16(), parsed_.scheme);
}

bool EqualIgnoringFragmentIdentifier(const KURL& a, const KURL& b) {
  // Compute the length of each URL without its ref. Note that the reference
  // begin (if it exists) points to the character *after* the '#', so we need
  // to subtract one.
  int a_length = a.string_.length();
  if (a.parsed_.ref.len >= 0)
    a_length = a.parsed_.ref.begin - 1;

  int b_length = b.string_.length();
  if (b.parsed_.ref.len >= 0)
    b_length = b.parsed_.ref.begin - 1;

  if (a_length != b_length)
    return false;

  const String& a_string = a.string_;
  const String& b_string = b.string_;
  // FIXME: Abstraction this into a function in WTFString.h.
  for (int i = 0; i < a_length; ++i) {
    if (a_string[i] != b_string[i])
      return false;
  }
  return true;
}

unsigned KURL::HostStart() const {
  return parsed_.CountCharactersBefore(url::Parsed::HOST, false);
}

unsigned KURL::HostEnd() const {
  return parsed_.CountCharactersBefore(url::Parsed::PORT, true);
}

unsigned KURL::PathStart() const {
  return parsed_.CountCharactersBefore(url::Parsed::PATH, false);
}

unsigned KURL::PathEnd() const {
  return parsed_.CountCharactersBefore(url::Parsed::QUERY, true);
}

unsigned KURL::PathAfterLastSlash() const {
  if (string_.IsNull())
    return 0;
  if (!is_valid_ || !parsed_.path.is_valid())
    return parsed_.CountCharactersBefore(url::Parsed::PATH, false);
  url::Component filename;
  if (string_.Is8Bit())
    url::ExtractFileName(AsURLChar8Subtle(string_), parsed_.path, &filename);
  else
    url::ExtractFileName(string_.Characters16(), parsed_.path, &filename);
  return filename.begin;
}

bool ProtocolIs(const String& url, const char* protocol) {
#if DCHECK_IS_ON()
  AssertProtocolIsGood(protocol);
#endif
  if (url.IsNull())
    return false;
  if (url.Is8Bit())
    return url::FindAndCompareScheme(AsURLChar8Subtle(url), url.length(),
                                     protocol, 0);
  return url::FindAndCompareScheme(url.Characters16(), url.length(), protocol,
                                   0);
}

void KURL::Init(const KURL& base,
                const String& relative,
                const WTF::TextEncoding* query_encoding) {
  // As a performance optimization, we do not use the charset converter
  // if encoding is UTF-8 or other Unicode encodings. Note that this is
  // per HTML5 2.5.3 (resolving URL). The URL canonicalizer will be more
  // efficient with no charset converter object because it can do UTF-8
  // internally with no extra copies.

  StringUTF8Adaptor base_utf8(base.GetString());

  // We feel free to make the charset converter object every time since it's
  // just a wrapper around a reference.
  KURLCharsetConverter charset_converter_object(query_encoding);
  KURLCharsetConverter* charset_converter =
      (!query_encoding || IsUnicodeEncoding(query_encoding))
          ? 0
          : &charset_converter_object;

  // Clamp to int max to avoid overflow.
  url::RawCanonOutputT<char> output;
  if (!relative.IsNull() && relative.Is8Bit()) {
    StringUTF8Adaptor relative_utf8(relative);
    is_valid_ = url::ResolveRelative(base_utf8.Data(), base_utf8.length(),
                                     base.parsed_, relative_utf8.Data(),
                                     clampTo<int>(relative_utf8.length()),
                                     charset_converter, &output, &parsed_);
  } else {
    is_valid_ = url::ResolveRelative(base_utf8.Data(), base_utf8.length(),
                                     base.parsed_, relative.Characters16(),
                                     clampTo<int>(relative.length()),
                                     charset_converter, &output, &parsed_);
  }

  // AtomicString::fromUTF8 will re-hash the raw output and check the
  // AtomicStringTable (addWithTranslator) for the string. This can be very
  // expensive for large URLs. However, since many URLs are generated from
  // existing AtomicStrings (which already have their hashes computed), this
  // fast path is used if the input string is already canonicalized.
  //
  // Because this optimization does not apply to non-AtomicStrings, explicitly
  // check that the input is Atomic before moving forward with it. If we mark
  // non-Atomic input as Atomic here, we will render the (const) input string
  // thread unsafe.
  if (!relative.IsNull() && relative.Impl()->IsAtomic() &&
      StringView(output.data(), static_cast<unsigned>(output.length())) ==
          relative) {
    string_ = relative;
  } else {
    string_ = AtomicString::FromUTF8(output.data(), output.length());
  }

  InitProtocolMetadata();
  InitInnerURL();
  DCHECK(!::blink::ProtocolIsJavaScript(string_) || ProtocolIsJavaScript());
}

void KURL::InitInnerURL() {
  if (!is_valid_) {
    inner_url_.reset();
    return;
  }
  if (url::Parsed* inner_parsed = parsed_.inner_parsed())
    inner_url_ = WTF::WrapUnique(new KURL(
        kParsedURLString, string_.Substring(inner_parsed->scheme.begin,
                                            inner_parsed->Length() -
                                                inner_parsed->scheme.begin)));
  else
    inner_url_.reset();
}

void KURL::InitProtocolMetadata() {
  if (!is_valid_) {
    protocol_is_in_http_family_ = false;
    protocol_ = ComponentString(parsed_.scheme);
    return;
  }

  DCHECK(!string_.IsNull());
  StringView protocol = ComponentStringView(parsed_.scheme);
  protocol_is_in_http_family_ = true;
  if (protocol == WTF::g_https_atom) {
    protocol_ = WTF::g_https_atom;
  } else if (protocol == WTF::g_http_atom) {
    protocol_ = WTF::g_http_atom;
  } else {
    protocol_ = protocol.ToAtomicString();
    protocol_is_in_http_family_ =
        protocol_ == "http-so" || protocol_ == "https-so";
  }
  DCHECK_EQ(protocol_, protocol_.DeprecatedLower());
}

bool KURL::ProtocolIs(const StringView protocol) const {
#if DCHECK_IS_ON()
  AssertProtocolIsGood(protocol);
#endif

  // JavaScript URLs are "valid" and should be executed even if KURL decides
  // they are invalid.  The free function protocolIsJavaScript() should be used
  // instead.
  // FIXME: Chromium code needs to be fixed for this assert to be enabled.
  // ASSERT(strcmp(protocol, "javascript"));
  return protocol_ == protocol;
}

StringView KURL::StringViewForInvalidComponent() const {
  return string_.IsNull() ? StringView() : StringView(StringImpl::empty_);
}

StringView KURL::ComponentStringView(const url::Component& component) const {
  if (!is_valid_ || component.len <= 0)
    return StringViewForInvalidComponent();
  // begin and len are in terms of bytes which do not match
  // if string() is UTF-16 and input contains non-ASCII characters.
  // However, the only part in urlString that can contain non-ASCII
  // characters is 'ref' at the end of the string. In that case,
  // begin will always match the actual value and len (in terms of
  // byte) will be longer than what's needed by 'mid'. However, mid
  // truncates len to avoid go past the end of a string so that we can
  // get away without doing anything here.

  int max_length = GetString().length() - component.begin;
  return StringView(GetString(), component.begin,
                    component.len > max_length ? max_length : component.len);
}

String KURL::ComponentString(const url::Component& component) const {
  return ComponentStringView(component).ToString();
}

template <typename CHAR>
void KURL::ReplaceComponents(const url::Replacements<CHAR>& replacements) {
  url::RawCanonOutputT<char> output;
  url::Parsed new_parsed;

  StringUTF8Adaptor utf8(string_);
  is_valid_ = url::ReplaceComponents(utf8.Data(), utf8.length(), parsed_,
                                     replacements, 0, &output, &new_parsed);

  parsed_ = new_parsed;
  string_ = AtomicString::FromUTF8(output.data(), output.length());
  InitProtocolMetadata();
}

bool KURL::IsSafeToSendToAnotherThread() const {
  return string_.IsSafeToSendToAnotherThread() &&
         (!inner_url_ || inner_url_->IsSafeToSendToAnotherThread());
}

}  // namespace blink
