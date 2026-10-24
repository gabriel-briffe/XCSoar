// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

package org.xcsoar;

import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;

/**
 * A USB port the native code has claimed: serial or the OAT HID probe.
 */
interface ClaimedUsbPort extends AndroidPort {
  boolean isOpen();

  void open(UsbManager manager, UsbDevice device, int iface);

  void onDisconnect();
}
