#include "websocket.h"
#include <QDebug>
#include <QtCore/QRegularExpression>
#include <QCryptographicHash>
#include <stdint.h>

#define htonll(x) ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))

WebSocket::WebSocket(QTcpSocket *sock)
:   socket(sock), state(Request)
{
    connect(socket, SIGNAL(readyRead()), this, SLOT(readClient()));
    connect(socket, SIGNAL(disconnected()), this, SLOT(discardClient()));
}

WebSocket::~WebSocket()
{
}

void WebSocket::readClient()
{
    QByteArray line;
    switch (state) {
        case Request:
            if (socket->canReadLine()) {
                request = socket->readLine().trimmed();
            }
            state = Header;
            /* Fall through */

        case Header:
            while (socket->canReadLine()) {
                line = socket->readLine();
                int colon = line.indexOf(':');
                if (colon == -1) {
                    /* Last line */
                    state = Response;
                    readClient();
                    break;
                }
                QString header = line.left(colon).trimmed().toLower();
                QString value = line.mid(colon + 1).trimmed();
                headers[header] = value;
            }
            break;

        case Response:
            sendHandshake();
            state = FrameStart;
            break;

        case FrameStart:
            frame.clear();
            wantbytes = 6;
            state = Frame;
            /* Fall through */

        case Frame:
            if (socket->bytesAvailable() < wantbytes) {
                break;
            }
            frame.append(socket->read(wantbytes));
            decodeFrame();
            break;
        
        default:
            break;
    }
}

void WebSocket::decodeFrame()
{

    qint64 framelen = 6; /* Minimum frame size: 2 bytes header, 4 mask */
    qint64 datalen = 0; /* Length of payload data */
    const char *framebuf = frame.constData();
    const char *mask = 0;
    const char *mdata = 0;
    unsigned char opcode = framebuf[0] & 0xf;
    bool fin = (framebuf[0] >> 7) & 0x1;
    bool masked = (framebuf[1] >> 7) & 0x1;
    unsigned char reserved = (framebuf[0] >> 4) & 0x7;

    if (reserved) {
        abort("Reserved bit set in frame");
        return;
    }
    if (!masked) {
        abort("Frame not masked");
        return;
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
            readClient();
            return;
        }
        framelen += 8;
        datalen = ntohll(*(quint64 *)(framebuf + 2));
        framelen += datalen;
        mask = framebuf + 10;
    }
    //qDebug() << "framesize" << frame.size() << "framelen" << framelen << "datalen" << datalen;
    if (frame.size() < framelen) {
        wantbytes = framelen - frame.size();
        readClient();
        return;
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
                databuf.append(mdata[i] ^ mask[i % 4]);
            }
            state = FrameStart;
            emit readyRead();
            break;

        default: /* We don't know these opcodes */
            state = FrameStart;
            qDebug() << "Unknown opcode, ignoring" << opcode;
            break;
    }
}

void WebSocket::sendPong()
{
}

void WebSocket::sendClose()
{
    abort("Should really do an orderly close"); // XXX
}

qint64 WebSocket::write(const char *buf, qint64 datalen)
{
    QByteArray msg;
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
    msg.append(buf, datalen);
    return socket->write(msg.constData(), msg.size());
}

void WebSocket::sendHandshake()
{
    if (headers["sec-websocket-version"] != "13") {
        sendError(404, "Websocket version not supported");
        return;
    }
    if (!headers.contains("sec-websocket-key")) {
        sendError(404, "Websocket key header not found");
        return;
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
    socket->close();
    state = Unconnected; // XXX
    emit disconnected();
}

void WebSocket::abort(QString message)
{
    qDebug() << __PRETTY_FUNCTION__ << message;
    socket->abort();
    emit disconnected();
}

void WebSocket::discardClient()
{
    qDebug() << "discarding client";
    state = Unconnected;
    emit disconnected();
}

qint64 WebSocket::read(char *data, qint64 maxSize)
{
    qint64 copy = qMin<qint64>(databuf.size(), maxSize);
    memcpy(data, databuf.constData(), copy);
    databuf.remove(0, copy);
    return copy;
}

qint64 WebSocket::bytesAvailable() const
{
    return databuf.size();
}

bool WebSocket::flush()
{
    return socket->flush();
}
