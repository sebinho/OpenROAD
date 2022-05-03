/////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2019, The Regents of the University of California
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include "rsz/Resizer.hh"
#include "BufferedNet.hh"

#include <algorithm>
// Use spdlog fmt::format until c++20 that supports std::format.
#include <spdlog/fmt/fmt.h>

#include "rsz/Resizer.hh"

#include "utl/Logger.h"

#include "sta/Units.hh"
#include "sta/Liberty.hh"

namespace rsz {

using std::min;

using sta::INF;

using utl::Logger;
using utl::RSZ;

BufferedNet::BufferedNet(BufferedNetType type,
                         Point location,
                         float cap,
                         Pin *load_pin,
                         PathRef required_path,
                         Delay required_delay,
                         LibertyCell *buffer,
                         BufferedNetPtr ref,
                         BufferedNetPtr ref2) :
  type_(type),
  location_(location),
  load_pin_(load_pin),
  ref_(ref),
  ref2_(ref2),
  cap_(cap),
  required_path_(required_path),
  required_delay_(required_delay),
  buffer_cell_(buffer)
{
}

BufferedNet::BufferedNet(BufferedNetType type,
                         Point location,
                         float cap,
                         Pin *load_pin,
                         BufferedNetPtr ref,
                         BufferedNetPtr ref2) :
  type_(type),
  location_(location),
  load_pin_(load_pin),
  ref_(ref),
  ref2_(ref2),
  cap_(cap),
  buffer_cell_(nullptr)
{
}

// load
BufferedNet::BufferedNet(BufferedNetType type,
                         Point location,
                         Pin *load_pin) :
  type_(type),
  location_(location),
  load_pin_(load_pin),
  ref_(nullptr),
  ref2_(nullptr),
  cap_(0.0),
  buffer_cell_(nullptr)
{
}

// junc
BufferedNet::BufferedNet(BufferedNetType type,
                         Point location,
                         BufferedNetPtr ref,
                         BufferedNetPtr ref2) :
  type_(type),
  location_(location),
  load_pin_(nullptr),
  ref_(ref),
  ref2_(ref2),
  cap_(0.0),
  buffer_cell_(nullptr)
{
}
  
// wire
BufferedNet::BufferedNet(BufferedNetType type,
                         Point location,
                         BufferedNetPtr ref)  :
  type_(type),
  location_(location),
  load_pin_(nullptr),
  ref_(ref),
  ref2_(nullptr),
  cap_(0.0),
  buffer_cell_(nullptr)
{
}

BufferedNet::~BufferedNet()
{
}

void
BufferedNet::reportTree(Resizer *resizer)
{
  reportTree(0, resizer);
}

void
BufferedNet::reportTree(int level,
                        Resizer *resizer)
{
  resizer->logger()->report("{:{}s}{}", "", level, to_string(resizer));
  switch (type_) {
  case BufferedNetType::load:
    break;
  case BufferedNetType::buffer:
  case BufferedNetType::wire:
    ref_->reportTree(level + 1, resizer);
    break;
  case BufferedNetType::junction:
    ref_->reportTree(level + 1, resizer);
    ref2_->reportTree(level + 1, resizer);
    break;
  }
}

string
BufferedNet::to_string(Resizer *resizer)
{
  Network *sdc_network = resizer->sdcNetwork();
  Units *units = resizer->units();
  Unit *dist_unit = units->distanceUnit();
  const char *x = dist_unit->asString(resizer->dbuToMeters(location_.x()), 2);
  const char *y = dist_unit->asString(resizer->dbuToMeters(location_.y()), 2);
  const char *cap = units->capacitanceUnit()->asString(cap_);
  
  switch (type_) {
  case BufferedNetType::load:
    // {:{}s} format indents level spaces.
    return fmt::format("load {} ({}, {}) cap {} req {}",
                       sdc_network->pathName(load_pin_),
                       x, y, cap,
                       delayAsString(required(resizer), resizer));
  case BufferedNetType::wire:
    return fmt::format("wire ({}, {}) cap {} req {}",
                       x, y, cap,
                       delayAsString(required(resizer), resizer));
  case BufferedNetType::buffer:
    return fmt::format("buffer ({}, {}) {} cap {} req {}",
                       x, y,
                       buffer_cell_->name(),
                       cap,
                       delayAsString(required(resizer), resizer));
  case BufferedNetType::junction:
    return fmt::format("junction ({}, {}) cap {} req {}",
                       x, y, cap,
                       delayAsString(required(resizer), resizer));
  }
  // suppress gcc warning
  return "";
}

void
BufferedNet::setCapacitance(float cap)
{
  cap_ = cap;
}

void
BufferedNet::setRequiredPath(const PathRef &path_ref)
{
  required_path_ = path_ref;
}

Required
BufferedNet::required(StaState *sta)
{
  if (required_path_.isNull())
    return INF;
  else
    return required_path_.required(sta) - required_delay_;
}

int
BufferedNet::bufferCount() const
{
  switch (type_) {
  case BufferedNetType::buffer:
    return ref_->bufferCount() + 1;
  case BufferedNetType::wire:
    return ref_->bufferCount();
  case BufferedNetType::junction:
    return ref_->bufferCount() + ref2_->bufferCount();
  case BufferedNetType::load:
    return 0;
  }
  return 0;
}

}
