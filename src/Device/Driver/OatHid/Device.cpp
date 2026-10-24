// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Device/Driver/OatHid.hpp"
#include "Device/Driver.hpp"
#include "Device/Driver/OatHid/Parser.hpp"
#include "NMEA/Info.hpp"

#include <array>

class OatHidDevice final : public AbstractDevice {
  std::array<std::byte, 4> pending{};
  std::size_t pending_size = 0;

public:
  /* virtual methods from class Device */
  bool DataReceived(std::span<const std::byte> s,
                    NMEAInfo &info) noexcept override;
};

bool
OatHidDevice::DataReceived(std::span<const std::byte> s,
                           NMEAInfo &info) noexcept
{
  bool recognized = false;

  for (const std::byte byte : s) {
    pending[pending_size++] = byte;
    if (pending_size < pending.size())
      continue;

    pending_size = 0;
    const OatHid::Report report = OatHid::Parse(pending);
    if (!report.recognized)
      continue;

    /* a fault still means the probe is connected */
    info.alive.Update(info.clock);
    recognized = true;

    if (!report.ok)
      continue;

    info.temperature =
      Temperature::FromCelsius(report.hundredths / 100.);
    info.temperature_available.Update(info.clock);
  }

  return recognized;
}

static Device *
OatHidCreateOnPort(const DeviceConfig &, Port &)
{
  return new OatHidDevice();
}

const struct DeviceRegister oat_hid_driver = {
  "OatHid",
  "OAT HID",
  DeviceRegister::RAW_GPS_DATA | DeviceRegister::NO_TIMEOUT,
  OatHidCreateOnPort,
};
