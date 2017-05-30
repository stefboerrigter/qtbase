/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QSCREENVNC_P_H
#define QSCREENVNC_P_H

#include <QtCore/qvarlengtharray.h>
#include <QtCore/qsharedmemory.h>
#include <QtNetwork>

#include "qvncscreen.h"

QT_BEGIN_NAMESPACE

class QVNCServer;
class QVNCCursor;
class QVNCSocket;

#define MAP_TILE_SIZE 16
#define MAP_WIDTH 1280 / MAP_TILE_SIZE
#define MAP_HEIGHT 1024 / MAP_TILE_SIZE

class QVNCDirtyMap
{
public:
    QVNCDirtyMap(QVNCScreen *screen);
    virtual ~QVNCDirtyMap();

    void reset();
    bool dirty(int x, int y) const;
    virtual void setDirty(int x, int y, bool force = false) = 0;
    void setClean(int x, int y);

    int bytesPerPixel;

    int numDirty;
    int mapWidth;
    int mapHeight;

protected:
    uchar *map;
    QVNCScreen *screen;
    uchar *buffer;
    int bufferWidth;
    int bufferHeight;
    int bufferStride;
    int numTiles;
};

template <class T>
class QVNCDirtyMapOptimized : public QVNCDirtyMap
{
public:
    QVNCDirtyMapOptimized(QVNCScreen *screen) : QVNCDirtyMap(screen) {}
    ~QVNCDirtyMapOptimized() {}

    void setDirty(int x, int y, bool force = false);
};

class QRfbRect
{
public:
    QRfbRect() {}
    QRfbRect(quint16 _x, quint16 _y, quint16 _w, quint16 _h) {
        x = _x; y = _y; w = _w; h = _h;
    }

    void read(QVNCSocket *s);
    void write(QVNCSocket *s) const;

    quint16 x;
    quint16 y;
    quint16 w;
    quint16 h;
};

class QRfbPixelFormat
{
public:
    static int size() { return 16; }

    void read(QVNCSocket *s);
    void write(QVNCSocket *s);

    int bitsPerPixel;
    int depth;
    bool bigEndian;
    bool trueColor;
    int redBits;
    int greenBits;
    int blueBits;
    int redShift;
    int greenShift;
    int blueShift;
};

class QRfbServerInit
{
public:
    QRfbServerInit() { name = 0; }
    ~QRfbServerInit() { delete[] name; }

    int size() const { return QRfbPixelFormat::size() + 8 + strlen(name); }
    void setName(const char *n);

    void read(QVNCSocket *s);
    void write(QVNCSocket *s);

    quint16 width;
    quint16 height;
    QRfbPixelFormat format;
    char *name;
};

class QRfbSetEncodings
{
public:
    bool read(QVNCSocket *s);

    quint16 count;
};

class QRfbFrameBufferUpdateRequest
{
public:
    bool read(QVNCSocket *s);

    char incremental;
    QRfbRect rect;
};

class QRfbKeyEvent
{
public:
    bool read(QVNCSocket *s);

    char down;
    int  keycode;
    int  unicode;
};

class QRfbPointerEvent
{
public:
    bool read(QVNCSocket *s);

    Qt::MouseButtons buttons;
    enum { WheelNone,
           WheelUp,
           WheelDown,
           WheelLeft,
           WheelRight
       } wheelDirection;
    quint16 x;
    quint16 y;
};

class QRfbClientCutText
{
public:
    bool read(QVNCSocket *s);

    quint32 length;
};

class QRfbEncoder
{
public:
    QRfbEncoder(QVNCServer *s) : server(s) {}
    virtual ~QRfbEncoder() {}

    virtual void write() = 0;

protected:
    QVNCServer *server;
};

class QRfbRawEncoder : public QRfbEncoder
{
public:
    QRfbRawEncoder(QVNCServer *s) : QRfbEncoder(s) {}

    void write();

private:
    QByteArray buffer;
};

template <class SRC> class QRfbHextileEncoder;

template <class SRC>
class QRfbSingleColorHextile
{
public:
    QRfbSingleColorHextile(QRfbHextileEncoder<SRC> *e) : encoder(e) {}
    bool read(const uchar *data, int width, int height, int stride);
    void write(QVNCSocket *socket) const;

private:
    QRfbHextileEncoder<SRC> *encoder;
};

template <class SRC>
class QRfbDualColorHextile
{
public:
    QRfbDualColorHextile(QRfbHextileEncoder<SRC> *e) : encoder(e) {}
    bool read(const uchar *data, int width, int height, int stride);
    void write(QVNCSocket *socket) const;

private:
    struct Rect {
        quint8 xy;
        quint8 wh;
    } Q_PACKED rects[8 * 16];

    quint8 numRects;
    QRfbHextileEncoder<SRC> *encoder;

private:
    inline int lastx() const { return rectx(numRects); }
    inline int lasty() const { return recty(numRects); }
    inline int rectx(int r) const { return rects[r].xy >> 4; }
    inline int recty(int r) const { return rects[r].xy & 0x0f; }
    inline int width(int r) const { return (rects[r].wh >> 4) + 1; }
    inline int height(int r) const { return (rects[r].wh & 0x0f) + 1; }

    inline void setX(int r, int x) {
        rects[r].xy = (x << 4) | (rects[r].xy & 0x0f);
    }
    inline void setY(int r, int y) {
        rects[r].xy = (rects[r].xy & 0xf0) | y;
    }
    inline void setWidth(int r, int width) {
        rects[r].wh = ((width - 1) << 4) | (rects[r].wh & 0x0f);
    }
    inline void setHeight(int r, int height) {
        rects[r].wh = (rects[r].wh & 0xf0) | (height - 1);
    }

    inline void setWidth(int width) { setWidth(numRects, width); }
    inline void setHeight(int height) { setHeight(numRects, height); }
    inline void setX(int x) { setX(numRects, x); }
    inline void setY(int y) { setY(numRects, y); }
    void next();
};

template <class SRC>
class QRfbMultiColorHextile
{
public:
    QRfbMultiColorHextile(QRfbHextileEncoder<SRC> *e) : encoder(e) {}
    bool read(const uchar *data, int width, int height, int stride);
    void write(QVNCSocket *socket) const;

private:
    inline quint8* rect(int r) {
        return rects.data() + r * (bpp + 2);
    }
    inline const quint8* rect(int r) const {
        return rects.constData() + r * (bpp + 2);
    }
    inline void setX(int r, int x) {
        quint8 *ptr = rect(r) + bpp;
        *ptr = (x << 4) | (*ptr & 0x0f);
    }
    inline void setY(int r, int y) {
        quint8 *ptr = rect(r) + bpp;
        *ptr = (*ptr & 0xf0) | y;
    }
    void setColor(SRC color);
    inline int rectx(int r) const {
        const quint8 *ptr = rect(r) + bpp;
        return *ptr >> 4;
    }
    inline int recty(int r) const {
        const quint8 *ptr = rect(r) + bpp;
        return *ptr & 0x0f;
    }
    inline void setWidth(int r, int width) {
        quint8 *ptr = rect(r) + bpp + 1;
        *ptr = ((width - 1) << 4) | (*ptr & 0x0f);
    }
    inline void setHeight(int r, int height) {
        quint8 *ptr = rect(r) + bpp + 1;
        *ptr = (*ptr & 0xf0) | (height - 1);
    }

    bool beginRect();
    void endRect();

    static const int maxRectsSize = 16 * 16;
    QVarLengthArray<quint8, maxRectsSize> rects;

    quint8 bpp;
    quint8 numRects;
    QRfbHextileEncoder<SRC> *encoder;
};

template <class SRC>
class QRfbHextileEncoder : public QRfbEncoder
{
public:
    QRfbHextileEncoder(QVNCServer *s);
    void write();

private:
    enum SubEncoding {
        Raw = 1,
        BackgroundSpecified = 2,
        ForegroundSpecified = 4,
        AnySubrects = 8,
        SubrectsColoured = 16
    };

    QByteArray buffer;
    QRfbSingleColorHextile<SRC> singleColorHextile;
    QRfbDualColorHextile<SRC> dualColorHextile;
    QRfbMultiColorHextile<SRC> multiColorHextile;

    SRC bg;
    SRC fg;
    bool newBg;
    bool newFg;

    friend class QRfbSingleColorHextile<SRC>;
    friend class QRfbDualColorHextile<SRC>;
    friend class QRfbMultiColorHextile<SRC>;
};

class QVNCServer : public QObject
{
    Q_OBJECT
public:
    QVNCServer(QVNCScreen *screen, const QStringList &args);
    ~QVNCServer();

    void setDirty();
    void setDirtyCursor() { dirtyCursor = true; setDirty(); }
    inline bool isConnected() const { return state == Connected; }
    inline void setRefreshRate(int rate) { refreshRate = rate; }

    enum ClientMsg { SetPixelFormat = 0,
                     FixColourMapEntries = 1,
                     SetEncodings = 2,
                     FramebufferUpdateRequest = 3,
                     KeyEvent = 4,
                     PointerEvent = 5,
                     ClientCutText = 6 };

    enum ServerMsg { FramebufferUpdate = 0,
                     SetColourMapEntries = 1 };

    void convertPixels(char *dst, const char *src, int count) const;

    inline int clientBytesPerPixel() const {
        return pixelFormat.bitsPerPixel / 8;
    }

    inline QVNCScreen* screen() const { return qvnc_screen; }
    inline QVNCDirtyMap* dirtyMap() const { return qvnc_screen->dirtyMap(); }
    inline QVNCSocket* clientSocket() const { return client; }
    QImage *screenImage() const;
    inline bool doPixelConversion() const { return needConversion; }
    void setCursor(QVNCCursor * c) { cursor = c; }
private:
    void setPixelFormat();
    void setEncodings();
    void frameBufferUpdateRequest();
    void pointerEvent();
    void keyEvent();
    void clientCutText();
    bool pixelConversionNeeded() const;

private slots:
    void acceptConnection();
    void readClient();
    void checkUpdate();
    void discardClient();

private:
    void init();
    enum ClientState { Unconnected, Protocol, Init, Connected };
    QStringList mArgs;
    QTimer *timer;
    QTcpServer *serverSocket;
    QVNCSocket *client;
    ClientState state;
    quint8 msgType;
    bool handleMsg;
    QRfbPixelFormat pixelFormat;
    Qt::KeyboardModifiers keymod;
    Qt::MouseButtons buttons;
    int encodingsPending;
    int cutTextPending;
    uint supportCopyRect : 1;
    uint supportRRE : 1;
    uint supportCoRRE : 1;
    uint supportHextile : 1;
    uint supportZRLE : 1;
    uint supportCursor : 1;
    uint supportDesktopSize : 1;
    bool wantUpdate;
    bool sameEndian;
    bool needConversion;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    bool swapBytes;
#endif
    bool dirtyCursor;
    int refreshRate;
    QVNCScreen *qvnc_screen;

    QRfbEncoder *encoder;
    QVNCCursor *cursor;
};

class QVNCScreenPrivate : public QObject
{
public:
    QVNCScreenPrivate(QVNCScreen *parent, const QStringList &args);
    ~QVNCScreenPrivate();

    void setDirty(const QRect &rect, bool force = false);
    void configure();

    qreal dpiX;
    qreal dpiY;
    bool doOnScreenSurface;
    QVNCDirtyMap *dirty;
    int refreshRate;
    QVNCServer *vncServer;

    QVNCScreen *q_ptr;
};

class QVNCSocket : public QObject
{
Q_OBJECT
public:
    enum TxMode {
        RawSocket,
        WebSocket,
        SecureWebSocket
    };
    QVNCSocket(QTcpSocket *s, TxMode mode);
    ~QVNCSocket();

    /* Socket-like interfaces */
    qint64 write(const char *buf, qint64 maxSize);
    qint64 read(char *data, qint64 maxSize);
    qint64 bytesAvailable() const;
    QAbstractSocket::SocketState state();
    bool flush();

Q_SIGNALS:
    void readyRead();
    void disconnected();

private:
    QTcpSocket *socket;
    TxMode mode;
};

static inline int defaultWidth() { return 1024; }
static inline int defaultHeight() { return 768; }
static inline int defaultDisplay() { return 0; }
static inline QHostAddress *defaultAddr() { return new QHostAddress("127.0.0.1"); }

QT_END_NAMESPACE
#endif // QSCREENVNC_P_H
