# VNC platform plugin for Qt 5

This fork contains a VNC platform plugin which lets you run any Qt app in
headless mode and access it with a VNC client. It has builtin websocket
support for pure HTML5 VNC clients like [noVNC](http://novnc.com).

This enables viewing Qt apps in a web browser with a single click.

## Build

The default `./configure` will build the VNC plugin. Use `--no-vnc` to disable
building the VNC plugin. Your Qt application must be built with this Qt
library.

## Usage

You can control the platform using environment variables or CLI options.
In the shell,

    export QT_QPA_PLATFORM=vnc
    export QT_DEBUG_PLUGINS=1

Alternately, invoke the application with

    /path/to/qtapp -platform vnc

For MacOS app bundles, this may be modified to

    /path/to/qtapp.app/Contents/MacOS/qtapp -platform vnc

That's pretty much it. Run the Qt application. You should see messages
appearing on the console, like

    QVNCServer created on "127.0.0.1" port 5900 mode raw
  
Connect your vncviewer. If your app does not run maximized, it occupies a small
window aligned to the top left.

To create a 640x480 display, server listening on port 5901 on all interfaces,

    export QT_QPA_PLATFORM=vnc:size=640x480:display=1:addr=0.0.0.0

or

    /path/to/qtapp -platform vnc:size=640x480:display=1:addr=0.0.0.0

unset `QT_QPA_PLATFORM` to get back to the default backend of Cocoa/X.

## Viewing Qt apps in a browser

To use a browser-based HTML5 VNC viewer, use the `ws=<url>` option.

    /path/to/app -platform vnc:ws=http%3A//pigshell.github.io/noVNC/qtvnc.html

You should see a line like

    QVNCServer created on "127.0.0.1" port 5900 mode websocket

Navigate to http://localhost:5900 on the browser and you should see the
application.

The `ws` option puts the server in websocket mode. Any regular HTTP request to
http://localhost:5900 will be redirected to the viewer URL specified in the
`ws` parameter, with a hash fragment containing the host IP and port. In this
case, it will redirect to
`http://pigshell.github.io/noVNC/qtvnc.html#host=127.0.0.1&port=5900`

This viewer is a fork of [noVNC](http://novnc.com), modified to take
connection parameters in the URL fragment, which is not sent to the server.

Note that the colon in the viewer URL needs to be percent escaped, since the
colon is used by Qt as an option delimiter.

Websocket requests from origins other than the one specified by the viewer
URL will be rejected.

## Options

  * `size=<width>x<height>` Width and height of the frame buffer.
    * Default: `size=800x600`.
  * `display=<num>` VNC display number. Server listens to port 5900 + `<num>`
    * Default: `display=0`
  * `addr=<IP>` IPv4 address on which the server listens.
    * Example: `addr=0.0.0.0` listens on all interfaces
    * Default: `addr=127.0.0.1`
  * `ws=<URL>` Enable websocket mode and redirect to HTML5 viewer `<URL>`. In
    the absence of this option, the platform functions as a regular VNC server.
    * Example: `ws=http%3A//pigshell.github.io/noVNC/qtvnc.html`
  * `maximize=<bool>` Maximize first window.
    * Default: `maximize=true`

## Gotchas

  * Does not support authentication.
  * Does not build on Windows.
  * If running examples on MacOS, you need to do a `make install` first.
    Otherwise you need to copy the $QTBASE/plugins/platforms directory into
    QtAppName.app/Contents/MacOS/, where $QTBASE is the directory containing
    this (built) Qt repo.
