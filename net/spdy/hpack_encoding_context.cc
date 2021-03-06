// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/hpack_encoding_context.h"

#include <cstddef>

#include "base/logging.h"
#include "base/macros.h"
#include "net/spdy/hpack_entry.h"

namespace net {

namespace {

// An entry in the static table. Must be a POD in order to avoid
// static initializers, i.e. no user-defined constructors or
// destructors.
struct StaticEntry {
  const char* const name;
  const size_t name_len;
  const char* const value;
  const size_t value_len;
};

// The "constructor" for a StaticEntry that computes the lengths at
// compile time.
#define STATIC_ENTRY(name, value) \
  { name, arraysize(name) - 1, value, arraysize(value) - 1 }

const StaticEntry kStaticTable[] = {
  STATIC_ENTRY(":authority"                  , ""),             // 0
  STATIC_ENTRY(":method"                     , "GET"),          // 1
  STATIC_ENTRY(":method"                     , "POST"),         // 2
  STATIC_ENTRY(":path"                       , "/"),            // 3
  STATIC_ENTRY(":path"                       , "/index.html"),  // 4
  STATIC_ENTRY(":scheme"                     , "http"),         // 5
  STATIC_ENTRY(":scheme"                     , "https"),        // 6
  STATIC_ENTRY(":status"                     , "200"),          // 7
  STATIC_ENTRY(":status"                     , "500"),          // 8
  STATIC_ENTRY(":status"                     , "404"),          // 9
  STATIC_ENTRY(":status"                     , "403"),          // 10
  STATIC_ENTRY(":status"                     , "400"),          // 11
  STATIC_ENTRY(":status"                     , "401"),          // 12
  STATIC_ENTRY("accept-charset"              , ""),             // 13
  STATIC_ENTRY("accept-encoding"             , ""),             // 14
  STATIC_ENTRY("accept-language"             , ""),             // 15
  STATIC_ENTRY("accept-ranges"               , ""),             // 16
  STATIC_ENTRY("accept"                      , ""),             // 17
  STATIC_ENTRY("access-control-allow-origin" , ""),             // 18
  STATIC_ENTRY("age"                         , ""),             // 19
  STATIC_ENTRY("allow"                       , ""),             // 20
  STATIC_ENTRY("authorization"               , ""),             // 21
  STATIC_ENTRY("cache-control"               , ""),             // 22
  STATIC_ENTRY("content-disposition"         , ""),             // 23
  STATIC_ENTRY("content-encoding"            , ""),             // 24
  STATIC_ENTRY("content-language"            , ""),             // 25
  STATIC_ENTRY("content-length"              , ""),             // 26
  STATIC_ENTRY("content-location"            , ""),             // 27
  STATIC_ENTRY("content-range"               , ""),             // 28
  STATIC_ENTRY("content-type"                , ""),             // 29
  STATIC_ENTRY("cookie"                      , ""),             // 30
  STATIC_ENTRY("date"                        , ""),             // 31
  STATIC_ENTRY("etag"                        , ""),             // 32
  STATIC_ENTRY("expect"                      , ""),             // 33
  STATIC_ENTRY("expires"                     , ""),             // 34
  STATIC_ENTRY("from"                        , ""),             // 35
  STATIC_ENTRY("if-match"                    , ""),             // 36
  STATIC_ENTRY("if-modified-since"           , ""),             // 37
  STATIC_ENTRY("if-none-match"               , ""),             // 38
  STATIC_ENTRY("if-range"                    , ""),             // 39
  STATIC_ENTRY("if-unmodified-since"         , ""),             // 40
  STATIC_ENTRY("last-modified"               , ""),             // 41
  STATIC_ENTRY("link"                        , ""),             // 42
  STATIC_ENTRY("location"                    , ""),             // 43
  STATIC_ENTRY("max-forwards"                , ""),             // 44
  STATIC_ENTRY("proxy-authenticate"          , ""),             // 45
  STATIC_ENTRY("proxy-authorization"         , ""),             // 46
  STATIC_ENTRY("range"                       , ""),             // 47
  STATIC_ENTRY("referer"                     , ""),             // 48
  STATIC_ENTRY("refresh"                     , ""),             // 49
  STATIC_ENTRY("retry-after"                 , ""),             // 50
  STATIC_ENTRY("server"                      , ""),             // 51
  STATIC_ENTRY("set-cookie"                  , ""),             // 52
  STATIC_ENTRY("strict-transport-security"   , ""),             // 53
  STATIC_ENTRY("transfer-encoding"           , ""),             // 54
  STATIC_ENTRY("user-agent"                  , ""),             // 55
  STATIC_ENTRY("vary"                        , ""),             // 56
  STATIC_ENTRY("via"                         , ""),             // 57
  STATIC_ENTRY("www-authenticate"            , ""),             // 58
};

#undef STATIC_ENTRY

const size_t kStaticEntryCount = arraysize(kStaticTable);

}  // namespace

const uint32 HpackEncodingContext::kUntouched = HpackEntry::kUntouched;

HpackEncodingContext::HpackEncodingContext() {}

HpackEncodingContext::~HpackEncodingContext() {}

uint32 HpackEncodingContext::GetEntryCount() const {
  return header_table_.GetEntryCount() + kStaticEntryCount;
}

base::StringPiece HpackEncodingContext::GetNameAt(uint32 index) const {
  CHECK_LT(index, GetEntryCount());
  if (index >= header_table_.GetEntryCount()) {
    const StaticEntry& entry =
        kStaticTable[index - header_table_.GetEntryCount()];
    return base::StringPiece(entry.name, entry.name_len);
  }
  return header_table_.GetEntry(index).name();
}

base::StringPiece HpackEncodingContext::GetValueAt(uint32 index) const {
  CHECK_LT(index, GetEntryCount());
  if (index >= header_table_.GetEntryCount()) {
    const StaticEntry& entry =
        kStaticTable[index - header_table_.GetEntryCount()];
    return base::StringPiece(entry.value, entry.value_len);
  }
  return header_table_.GetEntry(index).value();
}

bool HpackEncodingContext::IsReferencedAt(uint32 index) const {
  CHECK_LT(index, GetEntryCount());
  if (index >= header_table_.GetEntryCount())
    return false;
  return header_table_.GetEntry(index).IsReferenced();
}

uint32 HpackEncodingContext::GetTouchCountAt(uint32 index) const {
  CHECK_LT(index, GetEntryCount());
  if (index >= header_table_.GetEntryCount())
    return 0;
  return header_table_.GetEntry(index).TouchCount();
}

void HpackEncodingContext::SetReferencedAt(uint32 index, bool referenced) {
  header_table_.GetMutableEntry(index)->SetReferenced(referenced);
}

void HpackEncodingContext::AddTouchesAt(uint32 index, uint32 touch_count) {
  header_table_.GetMutableEntry(index)->AddTouches(touch_count);
}

void HpackEncodingContext::ClearTouchesAt(uint32 index) {
  header_table_.GetMutableEntry(index)->ClearTouches();
}

void HpackEncodingContext::SetMaxSize(uint32 max_size) {
  header_table_.SetMaxSize(max_size);
}

bool HpackEncodingContext::ProcessIndexedHeader(
    uint32 index,
    int32* new_index,
    std::vector<uint32>* removed_referenced_indices) {
  if (index >= GetEntryCount())
    return false;

  if (index < header_table_.GetEntryCount()) {
    *new_index = index;
    removed_referenced_indices->clear();
    HpackEntry* entry = header_table_.GetMutableEntry(index);
    entry->SetReferenced(!entry->IsReferenced());
  } else {
    // TODO(akalin): Make HpackEntry know about owned strings and
    // non-owned strings so that it can potentially avoid copies here.
    HpackEntry entry(GetNameAt(index), GetValueAt(index));

    header_table_.TryAddEntry(entry, new_index, removed_referenced_indices);
    if (*new_index >= 0) {
      header_table_.GetMutableEntry(*new_index)->SetReferenced(true);
    }
  }
  return true;
}

bool HpackEncodingContext::ProcessLiteralHeaderWithIncrementalIndexing(
    base::StringPiece name,
    base::StringPiece value,
    int32* index,
    std::vector<uint32>* removed_referenced_indices) {
  HpackEntry entry(name, value);
  header_table_.TryAddEntry(entry, index, removed_referenced_indices);
  if (*index >= 0) {
    header_table_.GetMutableEntry(*index)->SetReferenced(true);
  }
  return true;
}

}  // namespace net
