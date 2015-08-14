#ifndef _WEBSOCKET_H
#define _WEBSOCKET_H

#include <QtNetwork>

class WebSocket: public QObject
{
    Q_OBJECT

public:
    WebSocket(QTcpSocket *sock, int buftime);
    WebSocket(QTcpSocket *sock);
    ~WebSocket();

    /* Socket-like interfaces */
    qint64 write(const char *buf, qint64 maxSize);
    qint64 read(char *data, qint64 maxSize);
    qint64 bytesAvailable() const;
    bool flush();

Q_SIGNALS:
    void setupComplete();
    void readyRead();
    void disconnected();

private slots:
    void readClient();
    void discardClient();
    void flushOutbuf();

private:
    QTcpSocket *socket;
    enum ClientState {Unconnected, Request, Header, Response, FrameStart,
        Frame};
    ClientState state;
    QHash<QString, QString> headers;
    QString subprotocol;
    QByteArray request;
    QByteArray frame;
    QByteArray inbuf;
    QByteArray outbuf;
    qint64 wantbytes;
    int buftime;

    enum {
        OpContinuation = 0x0,
        OpTextFrame,
        OpBinaryFrame,
        OpClose = 0x8,
        OpPing,
        OpPong
    };

    void sendResponse(int code, QString response);
    void sendHeader(QString header, QString value);
    void sendError(int code, QString message);
    qint64 sendFrame(QByteArray buf);
    void endHeaders();
    bool processHeader();
    bool sendHandshake();
    bool decodeFrame();
    void encodeFrame();
    void abort(QString message);
    void sendPong();
    void sendClose();

};

#endif
