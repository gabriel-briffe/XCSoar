// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GlideConeMode.hpp"
#include "Widget/RowFormWidget.hpp"
#include "Interface.hpp"
#include "Computer/Settings.hpp"
#include "Profile/Profile.hpp"
#include "Profile/Keys.hpp"
#include "Language/Language.hpp"
#include "UIGlobals.hpp"

static void
SetGlideConeMode(GlideConeSettings::Mode mode) noexcept
{
  CommonInterface::SetComputerSettings().glide_cone.mode = mode;
  Profile::Set(ProfileKeys::GlideConeMode, int(mode));
}

class GlideConeModeWidget final : public RowFormWidget {
public:
  GlideConeModeWidget() noexcept
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    RowFormWidget::Prepare(parent, rc);

    AddButton(_("Off"), [](){
      SetGlideConeMode(GlideConeSettings::Mode::OFF);
    });
    AddButton(_("Single"), [](){
      SetGlideConeMode(GlideConeSettings::Mode::SINGLE);
    });
    AddButton(_("Combined"), [](){
      SetGlideConeMode(GlideConeSettings::Mode::COMBINED);
    });
  }
};

std::unique_ptr<Widget>
LoadGlideConeModePanel([[maybe_unused]] unsigned id)
{
  return std::make_unique<GlideConeModeWidget>();
}
