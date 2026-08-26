// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RainbowConfigPanel.hpp"

#ifdef HAVE_HTTP

#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Weather/Settings.hpp"
#include "Widget/RowFormWidget.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "UIGlobals.hpp"

enum ControlIndex {
  RAINBOW_API_KEY,
};

class RainbowConfigPanel final : public RowFormWidget {
public:
  RainbowConfigPanel()
    :RowFormWidget(UIGlobals::GetDialogLook()) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool Save(bool &changed) noexcept override;
};

void
RainbowConfigPanel::Prepare(ContainerWindow &parent,
                            const PixelRect &rc) noexcept
{
  const auto &settings = CommonInterface::GetComputerSettings().weather;

  RowFormWidget::Prepare(parent, rc);

  AddPassword(C_("Setting", "Rainbow API key"),
              _("API token from the Rainbow.ai developer portal."),
              settings.rainbow.api_key);
}

bool
RainbowConfigPanel::Save(bool &_changed) noexcept
{
  bool changed = false;
  auto &settings = CommonInterface::SetComputerSettings().weather;

  changed |= SaveValue(RAINBOW_API_KEY, ProfileKeys::RainbowApiKey,
                       settings.rainbow.api_key);

  _changed |= changed;
  return true;
}

std::unique_ptr<Widget>
CreateRainbowConfigPanel()
{
  return std::make_unique<RainbowConfigPanel>();
}

#else

#include "Widget/Widget.hpp"

std::unique_ptr<Widget>
CreateRainbowConfigPanel()
{
  return nullptr;
}

#endif
