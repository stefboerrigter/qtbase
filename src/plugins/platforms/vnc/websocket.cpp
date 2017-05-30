/*
  Copyright (C) 2015 Coriolis Technologies Pvt Ltd, info@coriolis.co.in
  Author: Ganesh Varadarajan

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "websocket.h"
#include <QDebug>
#include <QtCore/QRegularExpression>
#include <QCryptographicHash>
#include <stdint.h>
#include <arpa/inet.h>

#define htonll(x) ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))

WebSocket::WebSocket(QObject *p, QTcpSocket *sock, QUrl vurl, int buftime)
:   QObject::QObject(p), socket(sock), state(Request), viewer(vurl), buftime(buftime), timer(0)
{
    QUrl ourl;

    ourl.setScheme(viewer.scheme());
    ourl.setHost(viewer.host());
    ourl.setPort(viewer.port());
    origin = ourl.toString();

    if (!viewer.hasFragment()) {
        QString frag = QString("host=%1&port=%2").arg(socket->localAddress().toString()).arg(sock->localPort());
        viewer.setFragment(frag);
    }
    connect(socket, SIGNAL(readyRead()), this, SLOT(readClient()));
    connect(socket, SIGNAL(disconnected()), this, SLOT(discardClient()));
    if (buftime == 0) {
        /* Frame and send buffered writes as fast as possible */
        connect(socket, SIGNAL(bytesWritten(qint64)), this,
            SLOT(flushOutbuf()));
    } else if (buftime > 0) {
        /*
         * Buffer writes up to buftime msec. This helps reduce the number
         * of frames. Sending a large number of very small frames impacts
         * browser performance.
         */
        timer = new QTimer(this);
        connect(timer, SIGNAL(timeout()), this, SLOT(flushOutbuf()));
        timer->start(buftime);
    } else if (buftime < 0) {
        /* Writes are buffered until a flush() */
    }
}

WebSocket::~WebSocket()
{
}

void WebSocket::readClient()
{
    while (state != Unconnected && socket->bytesAvailable()) {
        switch (state) {
            case Request:
                if (!socket->canReadLine()) {
                    return;
                }
                request = socket->readLine().trimmed();
                state = Header;
                /* Fall through */

            case Header:
                if (!socket->canReadLine()) {
                    return;
                }
                state = processHeader();
                if (state != Response) {
                    break;
                }
                /* Last header reached, fall through */

            case Response:
                state = sendHandshake();
                if (state == FrameStart) {
                    emit setupComplete();
                }
                break;

            case FrameStart:
                frame.clear();
                wantbytes = 6;
                state = Frame;
                /* Fall through */

            case Frame:
                if (socket->bytesAvailable() < wantbytes) {
                    return;
                }
                frame.append(socket->read(wantbytes));
                state = decodeFrame();
                break;
            
            default:
                break;
        }
    }
}

WebSocket::WState WebSocket::processHeader()
{
    QByteArray line;

    line = socket->readLine();
    int colon = line.indexOf(':');
    if (colon == -1) {
        /* Last line */
        return Response;
    } else {
        QString header = line.left(colon).trimmed().toLower();
        QString value = line.mid(colon + 1).trimmed();
        headers[header] = value;
    }
    return Header;
}

WebSocket::WState WebSocket::decodeFrame()
{

    qint64 framelen = 6; /* Minimum frame size: 2 bytes header, 4 mask */
    qint64 datalen = 0; /* Length of payload data */
    const char *framebuf = frame.constData();
    const char *mask = 0;
    unsigned char opcode = framebuf[0] & 0xf;
    //bool fin = (framebuf[0] >> 7) & 0x1;
    bool masked = (framebuf[1] >> 7) & 0x1;
    unsigned char reserved = (framebuf[0] >> 4) & 0x7;

    if (reserved) {
        abort("Reserved bit set in frame");
        return Unconnected;
    }
    if (!masked) {
        abort("Frame not masked");
        return Unconnected;
    }
    int payload_len = (unsigned char)(framebuf[1] & (~0x80));
    if (payload_len < 126) {
        framelen += payload_len;
        datalen = payload_len;
        mask = framebuf + 2;
    } else if (payload_len == 126) {
        framelen += 2;
        datalen = ntohs(*(quint16 *)(framebuf + 2));
        framelen += datalen;
        mask = framebuf + 4;
    } else if (payload_len == 127) {
        if (frame.size() < 10) {
            wantbytes = 10 - frame.size();
            return Frame;
        }
        framelen += 8;
        datalen = ntohll(*(quint64 *)(framebuf + 2));
        framelen += datalen;
        mask = framebuf + 10;
    }
    //qDebug() << "framesize" << frame.size() << "framelen" << framelen << "datalen" << datalen << inbuf;
    if (frame.size() < framelen) {
        wantbytes = framelen - frame.size();
        return Frame;
    }

    QByteArray framedata;
    if (opcode == OpPing || opcode == OpClose || opcode == OpContinuation ||
        opcode == OpTextFrame || opcode == OpBinaryFrame) {
        const char *mdata = mdata = mask + 4; /* masked data */
        for (qint64 i = 0; i < datalen; i++) {
            framedata.append(mdata[i] ^ mask[i % 4]);
        }
    }

    /* We have a full frame at last */
    switch (opcode) {
        case OpPing:
            sendPong(framedata);
            break;

        case OpClose:
            sendClose(framedata);
            return Unconnected;
            break;

        case OpPong: /* Pong, ignore */
            qDebug() << "Pong, ignoring" << opcode;
            break;

        case OpContinuation:
        case OpTextFrame:
        case OpBinaryFrame:
            inbuf.append(framedata);
            emit readyRead();
            break;

        default: /* We don't know these opcodes */
            qDebug() << "Unknown opcode, ignoring" << opcode;
            break;
    }
    return FrameStart;
}

void WebSocket::sendPong(const QByteArray& framedata)
{
    QByteArray msg;
    unsigned char b0 = 0x80 | OpPong;
    msg.append(b0);
    if (framedata.size() < 126) {
        msg.append(framedata);
    }
    (void)socket->write(msg.constData(), msg.size());
}

void WebSocket::sendClose(const QByteArray& framedata, quint16 code)
{
    QByteArray msg;
    unsigned char b0 = 0x80 | OpClose;
    msg.append(b0);
    if (framedata.size() > 2) {
        code = ntohs(*(quint16 *)framedata.constData());
    }
    code = htons(code);
    msg.append((quint8)2);
    msg.append((const char *)&code, 2);
    socket->write(msg.constData(), msg.size());
    socket->disconnectFromHost();
}

qint64 WebSocket::sendFrame(const QByteArray& buf)
{
    if (state == Unconnected) {
        qDebug() << "GDB:" << "sending frame while unconnected?";
    }
    QByteArray msg;
    qint64 datalen = buf.size();
    unsigned char b0 = 0x80 | OpBinaryFrame;

    msg.append(b0);
    if (datalen < 126) {
        msg.append((quint8)datalen);
    } else if (datalen <= 65536) {
        quint16 x = htons((quint16)datalen);
        msg.append((quint8)126);
        msg.append((const char *)&x, 2);
    } else {
        quint64 x = htonll((quint64)datalen);
        msg.append((quint8)127);
        msg.append((const char *)&x, 8);
    }
    msg.append(buf);
    return socket->write(msg.constData(), msg.size());
}

WebSocket::WState WebSocket::sendHandshake()
{
    if (headers["upgrade"].toLower() != "websocket") {
        /*
         * If we get a regular non-websocket HTTP request, redirect to the
         * viewer URL, which hopefully implements a websocket-based VNC player.
         */
        sendResponse(307, "Redirect to viewer");
        sendHeader("Connection", "close");
        sendHeader("Location", viewer.toString());
        endHeaders();
        socket->disconnectFromHost();
        return Unconnected;
    }

    if (!headers["connection"].contains("upgrade", Qt::CaseInsensitive) ||
        headers["sec-websocket-version"] != "13" ||
        !headers.contains("sec-websocket-key")) {
        sendError(400, "Bad request");
        return Unconnected;
    }

    if (headers["origin"] != "" && headers["origin"] != origin) {
        sendError(403, "Bad origin");
        return Unconnected;
    }
    if (headers.contains("sec-websocket-protocol")) {
        QStringList dtypes = headers["sec-websocket-protocol"].split(QRegExp(",\\s*"));
        subprotocol = "";
        if (dtypes.contains("binary", Qt::CaseInsensitive)) {
            subprotocol = "binary";
        }
    }

    QString key = headers["sec-websocket-key"];
    QString accept = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    accept = QCryptographicHash::hash(accept.toUtf8(),
        QCryptographicHash::Sha1).toBase64();
    sendResponse(101, "Switching Protocols");
    sendHeader("Connection", "Upgrade");
    sendHeader("Upgrade", "WebSocket");
    sendHeader("Sec-WebSocket-Accept", accept);
    if (subprotocol != "") {
        sendHeader("Sec-WebSocket-Protocol", subprotocol);
    }
    endHeaders();
    return FrameStart;
}

void WebSocket::sendResponse(int code, const QString& response)
{
    QString proto = "HTTP/1.1";

    QString line = QString("%1 %2 %3\r\n").arg(proto).arg(code).arg(response);
    socket->write(line.toUtf8().data());
    sendHeader("Server", "QtVNC/0.1");
}

void WebSocket::sendHeader(const QString& header, const QString& value)
{
    QString line = QString("%1: %2\r\n").arg(header).arg(value);
    socket->write(line.toUtf8().data());
}

void WebSocket::endHeaders()
{
    QString line = "\r\n";
    socket->write(line.toUtf8().data());
}

void WebSocket::sendError(int code, const QString& message)
{
    qDebug() << __PRETTY_FUNCTION__ << code << message;
    sendResponse(code, message);
    sendHeader("Connection", "close");
    endHeaders();
    socket->disconnectFromHost();
}

void WebSocket::abort(const QString& message)
{
    qDebug() << __PRETTY_FUNCTION__ << message;
    socket->abort();
    state = Unconnected;
}

void WebSocket::discardClient()
{
    state = Unconnected;
    if (timer) {
        timer->stop();
    }
    emit disconnected();
}

void WebSocket::flushOutbuf()
{
    if (outbuf.size()) {
        sendFrame(outbuf);
        outbuf.clear();
    }
}

qint64 WebSocket::write(const char *buf, qint64 size)
{
    outbuf.append(buf, size);

    if (buftime == 0 && socket->bytesToWrite() == 0) {
        flushOutbuf();
    } else if (buftime < 0) {
        flushOutbuf();
    }
    return size;
}

qint64 WebSocket::read(char *data, qint64 maxSize)
{
    qint64 copy = qMin<qint64>(inbuf.size(), maxSize);
    memcpy(data, inbuf.constData(), copy);
    inbuf.remove(0, copy);
    return copy;
}

qint64 WebSocket::bytesAvailable() const
{
    return inbuf.size();
}

bool WebSocket::flush()
{
    flushOutbuf();
    return socket->flush();
}

void WebSocket::close()
{
    QByteArray f;

    sendClose(f);
    state = Unconnected;
}
