#include "websocket.h"
#include <QDebug>
#include <QtCore/QRegularExpression>
#include <QCryptographicHash>
#include <stdint.h>

#define htonll(x) ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))

WebSocket::WebSocket(QTcpSocket *sock)
:   WebSocket::WebSocket(sock, 50)
{
}

WebSocket::WebSocket(QTcpSocket *sock, int buftime)
:   socket(sock), state(Request), buftime(buftime)
{
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
        QTimer *timer = new QTimer(this);
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
    while (socket->bytesAvailable()) {
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
                if (processHeader()) {
                    state = Response;
                    /* Last header reached, fall through */
                } else {
                    break;
                }

            case Response:
                if (sendHandshake()) {
                    state = FrameStart;
                    emit setupComplete();
                } else {
                    abort("Websocket handshake failed");
                    return;
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
                if (!decodeFrame()) {
                    return;
                }
                break;
            
            default:
                break;
        }
    }
}

bool WebSocket::processHeader()
{
    QByteArray line;

    line = socket->readLine();
    int colon = line.indexOf(':');
    if (colon == -1) {
        /* Last line */
        return true;
    } else {
        QString header = line.left(colon).trimmed().toLower();
        QString value = line.mid(colon + 1).trimmed();
        headers[header] = value;
    }
    return false;
}

bool WebSocket::decodeFrame()
{

    qint64 framelen = 6; /* Minimum frame size: 2 bytes header, 4 mask */
    qint64 datalen = 0; /* Length of payload data */
    const char *framebuf = frame.constData();
    const char *mask = 0;
    const char *mdata = 0;
    unsigned char opcode = framebuf[0] & 0xf;
    //bool fin = (framebuf[0] >> 7) & 0x1;
    bool masked = (framebuf[1] >> 7) & 0x1;
    unsigned char reserved = (framebuf[0] >> 4) & 0x7;

    if (reserved) {
        abort("Reserved bit set in frame");
        return false;
    }
    if (!masked) {
        abort("Frame not masked");
        return false;
    }
    int payload_len = (unsigned char)(framebuf[1] & (~0x80));
    if (payload_len < 126) {
        framelen += payload_len;
        datalen = payload_len;
        mask = framebuf + 2;
    } else if (payload_len == 126) {
        framelen += 2;
        datalen = ntohs(*(unsigned short *)(framebuf + 2));
        framelen += datalen;
        mask = framebuf + 4;
    } else if (payload_len == 127) {
        if (frame.size() < 10) {
            wantbytes = 10 - frame.size();
            return true;
        }
        framelen += 8;
        datalen = ntohll(*(quint64 *)(framebuf + 2));
        framelen += datalen;
        mask = framebuf + 10;
    }
    //qDebug() << "framesize" << frame.size() << "framelen" << framelen << "datalen" << datalen << inbuf;
    if (frame.size() < framelen) {
        wantbytes = framelen - frame.size();
        return true;
    }

    /* We have a full frame at last */
    switch (opcode) {
        case OpPing:
            sendPong();
            state = FrameStart;
            break;

        case OpClose:
            sendClose();
            state = Unconnected;
            break;

        case OpPong: /* Pong, ignore */
            state = FrameStart;
            qDebug() << "Pong, ignoring" << opcode;
            break;

        case OpContinuation:
        case OpTextFrame:
        case OpBinaryFrame:
            mdata = mask + 4; /* masked data */
            for (qint64 i = 0; i < datalen; i++) {
                inbuf.append(mdata[i] ^ mask[i % 4]);
            }
            state = FrameStart;
            emit readyRead();
            break;

        default: /* We don't know these opcodes */
            state = FrameStart;
            qDebug() << "Unknown opcode, ignoring" << opcode;
            break;
    }
    return true;
}

void WebSocket::sendPong()
{
    QByteArray msg;
    unsigned char b0 = 0x80 | OpPong;
    msg.append(b0);
    (void)socket->write(msg.constData(), msg.size());
}

void WebSocket::sendClose()
{
    abort("Should really do an orderly close"); // XXX
}

qint64 WebSocket::sendFrame(QByteArray buf)
{
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

bool WebSocket::sendHandshake()
{
    if (headers["sec-websocket-version"] != "13") {
        sendError(404, "Websocket version not supported");
        return false;
    }
    if (!headers.contains("sec-websocket-key")) {
        sendError(404, "Websocket key header not found");
        return false;
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
    return true;
}

void WebSocket::sendResponse(int code, QString response)
{
    QString proto = "HTTP/1.1";

    QString line = QString("%1 %2 %3\r\n").arg(proto).arg(code).arg(response);
    socket->write(line.toUtf8().data());
    sendHeader("Server", "QtVNC/0.1");
}

void WebSocket::sendHeader(QString header, QString value)
{
    QString line = QString("%1: %2\r\n").arg(header).arg(value);
    socket->write(line.toUtf8().data());
}

void WebSocket::endHeaders()
{
    QString line = "\r\n";
    socket->write(line.toUtf8().data());
}

void WebSocket::sendError(int code, QString message)
{
    sendResponse(code, message);
    sendHeader("Connection", "close");
    endHeaders();
    socket->flush();
}

void WebSocket::abort(QString message)
{
    qDebug() << __PRETTY_FUNCTION__ << message;
    socket->abort();
    state = Unconnected;
    emit disconnected();
}

void WebSocket::discardClient()
{
    qDebug() << "discarding client";
    state = Unconnected;
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
