// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "InfoBoxes/Content/Base.hpp"

struct InfoBoxData;
struct InfoBoxPanel;

class InfoBoxContentGlideCone : public InfoBoxContent {
public:
  void Update(InfoBoxData &data) noexcept override;
  const InfoBoxPanel *GetDialogContent() noexcept override;
};

/** Euclidean ground distance along the glide-cone relay path. */
class InfoBoxContentGlideConeDist : public InfoBoxContent {
public:
  void Update(InfoBoxData &data) noexcept override;
  const InfoBoxPanel *GetDialogContent() noexcept override;
};
