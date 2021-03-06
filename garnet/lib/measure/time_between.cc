// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/time_between.h"

namespace tracing {
namespace measure {
namespace {

bool IsOfSupportedType(const trace::Record::Event& event) {
  return event.type() == trace::EventType::kInstant ||
         event.type() == trace::EventType::kAsyncBegin ||
         event.type() == trace::EventType::kDurationBegin ||
         event.type() == trace::EventType::kFlowBegin ||
         event.type() == trace::EventType::kAsyncEnd ||
         event.type() == trace::EventType::kDurationEnd ||
         event.type() == trace::EventType::kFlowEnd ||
         event.type() == trace::EventType::kDurationComplete;
}

bool EventMatchesSpecWithAnchor(const trace::Record::Event& event, EventSpec spec, Anchor anchor) {
  if (!EventMatchesSpec(event, spec)) {
    return false;
  }

  if (event.type() == trace::EventType::kInstant ||
      event.type() == trace::EventType::kDurationComplete) {
    return true;
  } else if (event.type() == trace::EventType::kAsyncBegin ||
             event.type() == trace::EventType::kDurationBegin ||
             event.type() == trace::EventType::kFlowBegin) {
    return anchor == Anchor::Begin;
  } else if (event.type() == trace::EventType::kAsyncEnd ||
             event.type() == trace::EventType::kDurationEnd ||
             event.type() == trace::EventType::kFlowEnd) {
    return anchor == Anchor::End;
  } else {
    return false;
  }
}

}  // namespace

MeasureTimeBetween::MeasureTimeBetween(std::vector<TimeBetweenSpec> specs)
    : specs_(std::move(specs)) {}

// TODO(fmeawad): We currently do not handle TimeBetween 2 DurationComplete
// events that are nested.
bool MeasureTimeBetween::Process(const trace::Record::Event& event) {
  if (!IsOfSupportedType(event)) {
    return true;
  }

  for (const TimeBetweenSpec& spec : specs_) {
    uint64_t key = spec.common.id;

    if (EventMatchesSpecWithAnchor(event, spec.second_event, spec.second_anchor) &&
        pending_time_between_.count(key)) {
      if (spec.second_anchor == Anchor::End &&
          event.type() == trace::EventType::kDurationComplete) {
        AddResult(spec.common.id, pending_time_between_[key],
                  event.data.GetDurationComplete().end_time);
      } else {
        AddResult(spec.common.id, pending_time_between_[key], event.timestamp);
      }

      pending_time_between_.erase(key);
    }

    if (EventMatchesSpecWithAnchor(event, spec.first_event, spec.first_anchor)) {
      if (spec.first_anchor == Anchor::End && event.type() == trace::EventType::kDurationComplete) {
        pending_time_between_[key] = event.data.GetDurationComplete().end_time;
      } else {
        pending_time_between_[key] = event.timestamp;
      }
    }
  }
  return true;
}

void MeasureTimeBetween::AddResult(uint64_t spec_id, trace_ticks_t from, trace_ticks_t to) {
  results_[spec_id].push_back(to - from);
}

}  // namespace measure
}  // namespace tracing
