// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/min_max_size.h"

#include <algorithm>

namespace blink {

std::ostream& operator<<(std::ostream& stream, const MinMaxSize& value) {
  return stream << "(" << value.min_size << ", " << value.max_size << ")";
}

}  // namespace blink
