# VNC platform plugin for Qt 5

This fork contains a VNC platform plugin which lets you run any Qt app in headless mode and access it with a VNC client.

## Build

The default `./configure` will build the VNC plugin. Use `--no-vnc` to disable building the VNC plugin.
Your Qt application must be built with this Qt library.

## Use

    export QT_QPA_PLATFORM=vnc
    export QT_DEBUG_PLUGINS=1
    
That's pretty much it. Run your Qt application. You should see messages appearing on the console, like

  QVNCServer created on port 5900
  
Connect your vncviewer and enjoy.

## Gotchas

The server listens on all interfaces and has no authentication.
