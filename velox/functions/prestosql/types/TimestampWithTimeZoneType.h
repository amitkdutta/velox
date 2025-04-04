/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "velox/type/SimpleFunctionApi.h"
#include "velox/type/Type.h"

namespace facebook::velox {

using TimeZoneKey = int16_t;

constexpr int32_t kMillisShift = 12;
constexpr int32_t kTimezoneMask = (1 << kMillisShift) - 1;
// The maximum and minimum millis UTC we can represent in a
// TimestampWithTimeZone value given the bits we have to store it.
// We have 64 bits minus the bits for the time zone minus 1 for the sign bit.
constexpr int64_t kMaxMillisUtc = (1L << (64 - (int64_t)kMillisShift - 1)) - 1L;
constexpr int64_t kMinMillisUtc = (kMaxMillisUtc + 1) * -1;

inline int64_t unpackMillisUtc(int64_t dateTimeWithTimeZone) {
  return dateTimeWithTimeZone >> kMillisShift;
}

inline TimeZoneKey unpackZoneKeyId(int64_t dateTimeWithTimeZone) {
  return dateTimeWithTimeZone & kTimezoneMask;
}

inline int64_t pack(int64_t millisUtc, TimeZoneKey timeZoneKey) {
  VELOX_USER_CHECK(
      millisUtc <= kMaxMillisUtc && millisUtc >= kMinMillisUtc,
      "TimestampWithTimeZone overflow: {} ms",
      millisUtc);
  return (millisUtc << kMillisShift) | (timeZoneKey & kTimezoneMask);
}

inline int64_t pack(const Timestamp& timestamp, TimeZoneKey timeZoneKey) {
  return pack(timestamp.toMillis(), timeZoneKey);
}

inline Timestamp unpackTimestampUtc(int64_t dateTimeWithTimeZone) {
  return Timestamp::fromMillis(unpackMillisUtc(dateTimeWithTimeZone));
}

/// Represents timestamp with time zone as a number of milliseconds since epoch
/// and time zone ID.
class TimestampWithTimeZoneType : public BigintType {
  TimestampWithTimeZoneType() : BigintType(true) {}

 public:
  static const std::shared_ptr<const TimestampWithTimeZoneType>& get() {
    static const std::shared_ptr<const TimestampWithTimeZoneType> instance =
        std::shared_ptr<TimestampWithTimeZoneType>(
            new TimestampWithTimeZoneType());

    return instance;
  }

  bool equivalent(const Type& other) const override {
    // Pointer comparison works since this type is a singleton.
    return this == &other;
  }

  int32_t compare(const int64_t& left, const int64_t& right) const override {
    const int64_t leftUnpacked = unpackMillisUtc(left);
    const int64_t rightUnpacked = unpackMillisUtc(right);

    return leftUnpacked < rightUnpacked ? -1
        : leftUnpacked == rightUnpacked ? 0
                                        : 1;
  }

  uint64_t hash(const int64_t& value) const override {
    return folly::hasher<int64_t>()(unpackMillisUtc(value));
  }

  const char* name() const override {
    return "TIMESTAMP WITH TIME ZONE";
  }

  const std::vector<TypeParameter>& parameters() const override {
    static const std::vector<TypeParameter> kEmpty = {};
    return kEmpty;
  }

  std::string toString() const override {
    return name();
  }

  folly::dynamic serialize() const override {
    folly::dynamic obj = folly::dynamic::object;
    obj["name"] = "Type";
    obj["type"] = name();
    return obj;
  }
};

inline bool isTimestampWithTimeZoneType(const TypePtr& type) {
  // Pointer comparison works since this type is a singleton.
  return TimestampWithTimeZoneType::get() == type;
}

inline std::shared_ptr<const TimestampWithTimeZoneType>
TIMESTAMP_WITH_TIME_ZONE() {
  return TimestampWithTimeZoneType::get();
}

// Type used for function registration.
struct TimestampWithTimezoneT {
  using type = int64_t;
  static constexpr const char* typeName = "timestamp with time zone";
};

using TimestampWithTimezone = CustomType<TimestampWithTimezoneT, true>;

} // namespace facebook::velox
