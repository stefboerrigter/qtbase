# VNC platform plugin for Qt 5

This fork contains a VNC platform plugin which lets you run any Qt app in
headless mode and access it with a VNC client.

## Build

The default `./configure` will build the VNC plugin. Use `--no-vnc` to disable
building the VNC plugin. Your Qt application must be built with this Qt
library.

## Usage

You can control the platform using environment variables. In the shell,

    export QT_QPA_PLATFORM=vnc
    export QT_DEBUG_PLUGINS=1
    
That's pretty much it. Run the Qt application. You should see messages
appearing on the console, like

    QVNCServer created on "127.0.0.1" port 5900
  
Connect your vncviewer. If your app does not run maximized, it occupies a small
window aligned to the top left.

To create a 640x480 display, server listening on port 5901 on all interfaces,

    export QT_QPA_PLATFORM=vnc:display=640x480:display=1:addr=0.0.0.0

unset `QT_QPA_PLATFORM` to get back to the default backend of Cocoa/X.

## Gotchas

  * Does not support authentication.
  * Does not build on Windows.
  * If running examples on MacOS, you need to do a `make install` first.
    Otherwise you need to copy the $QTBASE/plugins/platforms directory into
    QtAppName.app/Contents/MacOS/, where $QTBASE is the directory containing
    this (built) Qt repo.
