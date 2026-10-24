// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

package org.xcsoar;

import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;

import android.annotation.TargetApi;

import java.util.Arrays;

/**
 * Interrupt-IN reader for the OAT probe (USB 2E8A:0A01).  This is not
 * a serial port: the four-byte HID report is passed through as-is.
 */
@TargetApi(Build.VERSION_CODES.HONEYCOMB_MR1)
public final class OatHidPort implements ClaimedUsbPort {
  private final UsbSerialHelper.UsbDeviceInterface parent;
  private UsbDeviceConnection connection;
  private UsbInterface claimed;
  private PortListener portListener;
  private InputListener inputListener;
  private volatile int state = STATE_LIMBO;
  private volatile boolean running;
  private Thread thread;
  private final SafeDestruct safeDestruct = new SafeDestruct();

  public OatHidPort(UsbSerialHelper.UsbDeviceInterface parent) {
    this.parent = parent;
  }

  @Override
  public synchronized boolean isOpen() {
    return connection != null;
  }

  private static UsbEndpoint interruptIn(UsbInterface iface) {
    for (int i = 0; i < iface.getEndpointCount(); ++i) {
      UsbEndpoint endpoint = iface.getEndpoint(i);
      if (endpoint.getDirection() == UsbConstants.USB_DIR_IN &&
          endpoint.getType() == UsbConstants.USB_ENDPOINT_XFER_INT)
        return endpoint;
    }
    return null;
  }

  @Override
  public synchronized void open(UsbManager manager, UsbDevice device,
                                int iface) {
    connection = manager.openDevice(device);
    if (connection == null) {
      setState(STATE_FAILED);
      return;
    }

    UsbInterface usbInterface = device.getInterface(iface);
    UsbEndpoint endpoint = interruptIn(usbInterface);
    if (endpoint == null || !connection.claimInterface(usbInterface, true)) {
      connection.close();
      connection = null;
      setState(STATE_FAILED);
      return;
    }

    claimed = usbInterface;
    running = true;
    final UsbDeviceConnection openConnection = connection;
    thread = new Thread(() -> readLoop(openConnection, endpoint), "OatHid");
    thread.start();
    setState(STATE_READY);
  }

  private void readLoop(UsbDeviceConnection openConnection,
                        UsbEndpoint endpoint) {
    final byte[] buffer =
      new byte[Math.max(endpoint.getMaxPacketSize(), 4)];

    while (running) {
      /* a timeout is the idle gap between 10 s reports, not a fault */
      final int n = openConnection.bulkTransfer(endpoint, buffer,
                                                buffer.length, 1000);
      if (!running || n <= 0)
        continue;

      deliver(Arrays.copyOf(buffer, n));
    }
  }

  private void deliver(byte[] data) {
    InputListener listener = inputListener;
    if (listener != null && safeDestruct.increment()) {
      try {
        listener.dataReceived(data, data.length);
      } finally {
        safeDestruct.decrement();
      }
    }
  }

  @Override
  public synchronized void close() {
    parent.portClosed(this);

    running = false;
    setState(STATE_FAILED);
    safeDestruct.beginShutdown();

    Thread reader = thread;
    thread = null;
    if (reader != null) {
      reader.interrupt();
      try {
        reader.join(1500);
      } catch (InterruptedException ignored) {
        Thread.currentThread().interrupt();
      }
    }

    if (connection != null) {
      if (claimed != null)
        connection.releaseInterface(claimed);
      connection.close();
      connection = null;
      claimed = null;
    }

    safeDestruct.finishShutdown();
  }

  @Override
  public void onDisconnect() {
    error("Disconnected");
  }

  @Override
  public void setListener(PortListener listener) {
    portListener = listener;
  }

  @Override
  public void setInputListener(InputListener listener) {
    inputListener = listener;
  }

  @Override
  public int getState() {
    return state;
  }

  @Override
  public boolean drain() {
    return false;
  }

  @Override
  public int getBaudRate() {
    return 0;
  }

  @Override
  public boolean setBaudRate(int baud) {
    return true;
  }

  @Override
  public int write(byte[] data, int length) {
    return -1;
  }

  private void stateChanged() {
    PortListener listener = portListener;
    if (listener != null && safeDestruct.increment()) {
      try {
        listener.portStateChanged();
      } finally {
        safeDestruct.decrement();
      }
    }
  }

  private void setState(int newState) {
    if (newState == state)
      return;

    state = newState;
    stateChanged();
  }

  private void error(String msg) {
    state = STATE_FAILED;

    PortListener listener = portListener;
    if (listener != null && safeDestruct.increment()) {
      try {
        listener.portError(msg);
      } finally {
        safeDestruct.decrement();
      }
    }
  }
}
