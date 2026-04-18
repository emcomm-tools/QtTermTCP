#ifndef BBSCACHE_H
#define BBSCACHE_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QVariantMap>
#include <QSqlDatabase>
#include <QRegularExpression>

// Forward declaration — Ui_ListenSession is defined in QtTermTCP.h
struct Ui_ListenSession;

// Global pointer — accessible from C functions in TermTCPCommon.cpp
class BBSCache;
extern BBSCache *g_bbsCache;

class BBSCache : public QObject
{
    Q_OBJECT

public:
    explicit BBSCache(QObject *parent = nullptr);
    ~BBSCache();

    // Hook points — called from existing code
    void onDataReceived(Ui_ListenSession *sess, const char *data, int len);
    void onCommandSent(Ui_ListenSession *sess, const QString &cmd);

    // UI access
    QList<QVariantMap> getCachedBulletins(const QString &node);
    QString getCachedMessage(const QString &node, int msgId);
    QStringList getKnownNodes();
    bool hasCache(const QString &node);

    // Session management
    void clearCache(const QString &node);
    void clearAll();

signals:
    void bbsDetected(Ui_ListenSession *sess, const QString &node);
    void sessionReady(const QString &node);

private:
    // State machine per session
    enum ParseState {
        Idle,
        Connected,
        AtPrompt,
        ParsingList,
        ReadingMessage
    };

    struct SessionState {
        ParseState state;
        QString nodeCall;
        QString lineBuffer;
        QString messageAccum;
        int currentMsgId;
        int nodeDbId;

        SessionState()
            : state(Idle), currentMsgId(0), nodeDbId(-1) {}
    };

    QMap<Ui_ListenSession*, SessionState> m_sessions;
    QSqlDatabase m_db;

    // Compiled regex patterns
    QRegularExpression m_rxBanner1;     // "Connected to BBS"
    QRegularExpression m_rxBanner2;     // "*** Connected to <NODE>"
    QRegularExpression m_rxPrompt;      // "de <CALL>>"
    QRegularExpression m_rxListLine;    // Bulletin list line
    QRegularExpression m_rxMsgEnd;      // "[End of Message #ID from CALL]"
    QRegularExpression m_rxBPQVersion;  // "[BPQ-...]"

    // Database operations
    void initDb();
    int getOrCreateNode(const QString &callsign);
    void storeBulletin(int nodeId, const QVariantMap &bulletin);
    void storeMessageBody(int nodeId, int msgId, const QString &body);

    // Parsing
    void processLine(Ui_ListenSession *sess, const QString &line);
    bool detectBanner(const QString &line, QString &nodeCall);
    bool detectPrompt(const QString &line);
    bool parseListLine(const QString &line, QVariantMap &bulletin);
    bool detectMessageEnd(const QString &line, int &msgId);
};

#endif // BBSCACHE_H
