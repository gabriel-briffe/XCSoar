// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "RainbowConfigPanel.hpp"

#ifdef HAVE_HTTP

#include "ConfigListPanel.hpp"
#include "Interface.hpp"
#include "Language/Language.hpp"
#include "Profile/Keys.hpp"
#include "Profile/Profile.hpp"
#include "Weather/Settings.hpp"
#include "util/StaticString.hxx"

/** The Rainbow.ai token. */
class RainbowConfigPanel final : public ConfigListPanel {
  StaticString<128> api_key;

protected:
  /* virtual methods from class ConfigListPanel */
  void LoadSettings() noexcept override;
  void Fill() noexcept override;

public:
  /* virtual methods from class Widget */
  bool Save(bool &changed) noexcept override;
};

void
RainbowConfigPanel::LoadSettings() noexcept
{
  api_key =
    CommonInterface::GetComputerSettings().weather.rainbow.api_key;
}

void
RainbowConfigPanel::Fill() noexcept
{
  AddGroup();

  AddTextItem(C_("Setting", "Rainbow API key"),
              _("API token from the Rainbow.ai developer portal."),
              api_key, true);
}

bool
RainbowConfigPanel::Save(bool &_changed) noexcept
{
  auto &settings = CommonInterface::SetComputerSettings().weather;

  _changed |= Profile::Update(ProfileKeys::RainbowApiKey,
                              settings.rainbow.api_key, api_key);

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
