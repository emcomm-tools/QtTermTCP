#include "BBSCache.h"
#include <QDir>
#include <QStandardPaths>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>

// Global pointer — set in QtTermTCP constructor
BBSCache *g_bbsCache = nullptr;

BBSCache::BBSCache(QObject *parent)
    : QObject(parent)
{
    // Compile regex patterns once
    m_rxBanner1 = QRegularExpression("Connected to BBS");
    m_rxBanner2 = QRegularExpression("\\*\\*\\* Connected to (\\S+)");
    m_rxBPQVersion = QRegularExpression("\\[BPQ-([^\\]]+)\\]");
    m_rxPrompt = QRegularExpression("^de ([A-Z0-9/\\-]+)>\\s*$");
    m_rxListLine = QRegularExpression(
        "^\\s*(\\d+)\\s+"           // msg_id
        "(\\d{1,2}-\\w{3})\\s+"     // date (e.g. 18-Apr)
        "([A-Z][$FNKP])\\s+"       // type (e.g. BN, B$, BF, BK, PN)
        "(\\d+)\\s+"               // size
        "(\\S+)\\s+"               // category (e.g. WX, NEWS, ALL)
        "@(\\S+)\\s+"              // distribution (e.g. ON, USA, WW)
        "(\\S+)\\s+"               // from callsign
        "(.+)$"                    // title (rest of line)
    );
    m_rxMsgEnd = QRegularExpression("^\\[End of Message #(\\d+) from (\\S+)\\]");

    initDb();
}

BBSCache::~BBSCache()
{
    if (m_db.isOpen())
        m_db.close();
}

// ============================================================================
// Database
// ============================================================================

void BBSCache::initDb()
{
    // Create cache directory
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                       + "/QtTermTCP/bbs-cache";
    QDir().mkpath(cacheDir);

    QString dbPath = cacheDir + "/bbscache.db";

    m_db = QSqlDatabase::addDatabase("QSQLITE", "bbscache");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "BBSCache: Failed to open database:" << m_db.lastError().text();
        return;
    }

    QSqlQuery q(m_db);

    q.exec("CREATE TABLE IF NOT EXISTS nodes ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "callsign TEXT UNIQUE NOT NULL,"
           "last_connected INTEGER"
           ")");

    q.exec("CREATE TABLE IF NOT EXISTS sessions ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "node_id INTEGER REFERENCES nodes(id),"
           "started INTEGER NOT NULL,"
           "ended INTEGER,"
           "raw_text TEXT"
           ")");

    q.exec("CREATE TABLE IF NOT EXISTS bulletins ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "node_id INTEGER REFERENCES nodes(id),"
           "msg_id INTEGER NOT NULL,"
           "date TEXT,"
           "type TEXT,"
           "size INTEGER,"
           "category TEXT,"
           "dist TEXT,"
           "from_call TEXT,"
           "title TEXT,"
           "body TEXT,"
           "cached_at INTEGER,"
           "UNIQUE(node_id, msg_id)"
           ")");
}

int BBSCache::getOrCreateNode(const QString &callsign)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM nodes WHERE callsign = ?");
    q.addBindValue(callsign);
    q.exec();

    if (q.next())
        return q.value(0).toInt();

    q.prepare("INSERT INTO nodes (callsign, last_connected) VALUES (?, ?)");
    q.addBindValue(callsign);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();

    return q.lastInsertId().toInt();
}

void BBSCache::storeBulletin(int nodeId, const QVariantMap &b)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO bulletins "
              "(node_id, msg_id, date, type, size, category, dist, from_call, title, cached_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(nodeId);
    q.addBindValue(b["msg_id"]);
    q.addBindValue(b["date"]);
    q.addBindValue(b["type"]);
    q.addBindValue(b["size"]);
    q.addBindValue(b["category"]);
    q.addBindValue(b["dist"]);
    q.addBindValue(b["from_call"]);
    q.addBindValue(b["title"]);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
}

void BBSCache::storeMessageBody(int nodeId, int msgId, const QString &body)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE bulletins SET body = ? WHERE node_id = ? AND msg_id = ?");
    q.addBindValue(body);
    q.addBindValue(nodeId);
    q.addBindValue(msgId);
    q.exec();
}

// ============================================================================
// Hook points — called from existing code
// ============================================================================

void BBSCache::onDataReceived(Ui_ListenSession *sess, const char *data, int len)
{
    if (!m_db.isOpen())
        return;

    // Get or create session state
    SessionState &st = m_sessions[sess];

    // Append to line buffer
    st.lineBuffer.append(QString::fromUtf8(data, len));

    // Process complete lines
    int pos;
    while ((pos = st.lineBuffer.indexOf('\r')) != -1) {
        QString line = st.lineBuffer.left(pos).trimmed();
        st.lineBuffer.remove(0, pos + 1);

        // Skip empty lines
        if (line.isEmpty())
            continue;

        // Also strip \n if present at start after \r
        if (!st.lineBuffer.isEmpty() && st.lineBuffer[0] == '\n')
            st.lineBuffer.remove(0, 1);

        processLine(sess, line);
    }

    // Also check for \n without \r
    while ((pos = st.lineBuffer.indexOf('\n')) != -1) {
        QString line = st.lineBuffer.left(pos).trimmed();
        st.lineBuffer.remove(0, pos + 1);

        if (line.isEmpty())
            continue;

        processLine(sess, line);
    }
}

void BBSCache::onCommandSent(Ui_ListenSession *sess, const QString &cmd)
{
    if (!m_db.isOpen())
        return;

    SessionState &st = m_sessions[sess];
    QString trimmed = cmd.trimmed().toUpper();

    if (st.state == AtPrompt) {
        if (trimmed.startsWith("LL") || trimmed.startsWith("L ") ||
            trimmed == "L" || trimmed.startsWith("LM") ||
            trimmed.startsWith("LB") || trimmed.startsWith("LT") ||
            trimmed.startsWith("LP")) {
            // List command — switch to parsing list
            st.state = ParsingList;
        }
        else if (trimmed.startsWith("R ")) {
            // Read command — extract message ID
            bool ok;
            int msgId = trimmed.mid(2).trimmed().toInt(&ok);
            if (ok) {
                st.state = ReadingMessage;
                st.currentMsgId = msgId;
                st.messageAccum.clear();
            }
        }
    }
}

// ============================================================================
// Line processing — state machine
// ============================================================================

void BBSCache::processLine(Ui_ListenSession *sess, const QString &line)
{
    SessionState &st = m_sessions[sess];

    switch (st.state) {
    case Idle:
    case Connected:
    {
        // Look for BBS banner
        QString nodeCall;
        if (detectBanner(line, nodeCall)) {
            st.state = Connected;
            st.nodeCall = nodeCall;
            st.nodeDbId = getOrCreateNode(nodeCall);

            // Update last_connected
            QSqlQuery q(m_db);
            q.prepare("UPDATE nodes SET last_connected = ? WHERE id = ?");
            q.addBindValue(QDateTime::currentSecsSinceEpoch());
            q.addBindValue(st.nodeDbId);
            q.exec();
        }

        // Look for prompt after connection
        if (st.state == Connected && detectPrompt(line)) {
            st.state = AtPrompt;
            emit bbsDetected(sess, st.nodeCall);
        }
        break;
    }

    case AtPrompt:
        // Waiting for user command — handled in onCommandSent
        break;

    case ParsingList:
    {
        // Check for prompt (end of list)
        if (detectPrompt(line)) {
            st.state = AtPrompt;
            break;
        }

        // Try to parse as bulletin list line
        QVariantMap bulletin;
        if (parseListLine(line, bulletin) && st.nodeDbId > 0) {
            storeBulletin(st.nodeDbId, bulletin);
        }
        break;
    }

    case ReadingMessage:
    {
        // Check for end of message
        int endMsgId;
        if (detectMessageEnd(line, endMsgId)) {
            // Store the accumulated message body
            if (st.nodeDbId > 0 && st.currentMsgId > 0) {
                storeMessageBody(st.nodeDbId, st.currentMsgId, st.messageAccum);
            }
            st.messageAccum.clear();
            st.currentMsgId = 0;
            st.state = AtPrompt;

            // Consume the trailing prompt if on same line
            break;
        }

        // Check for prompt (message ended without [End of Message] marker)
        if (detectPrompt(line)) {
            if (st.nodeDbId > 0 && st.currentMsgId > 0) {
                storeMessageBody(st.nodeDbId, st.currentMsgId, st.messageAccum);
            }
            st.messageAccum.clear();
            st.currentMsgId = 0;
            st.state = AtPrompt;
            break;
        }

        // Accumulate message body
        st.messageAccum.append(line + "\n");
        break;
    }
    }
}

// ============================================================================
// Pattern detection
// ============================================================================

bool BBSCache::detectBanner(const QString &line, QString &nodeCall)
{
    // Pattern 1: "Connected to BBS" — node call from earlier "Connected to NODE" line
    auto m1 = m_rxBanner1.match(line);
    if (m1.hasMatch()) {
        // nodeCall will be set when we see the [BPQ-] line or prompt
        return true;
    }

    // Pattern 2: "*** Connected to NODE-CALL"
    auto m2 = m_rxBanner2.match(line);
    if (m2.hasMatch()) {
        nodeCall = m2.captured(1);
        return true;
    }

    // Pattern 3: "NODENAME:CALL}" — LinBPQ node banner
    QRegularExpression rxNode("^(\\w+):(\\S+)\\}");
    auto m3 = rxNode.match(line);
    if (m3.hasMatch()) {
        nodeCall = m3.captured(2);  // e.g. VA2OPS-7
        return true;
    }

    return false;
}

bool BBSCache::detectPrompt(const QString &line)
{
    return m_rxPrompt.match(line).hasMatch();
}

bool BBSCache::parseListLine(const QString &line, QVariantMap &bulletin)
{
    auto match = m_rxListLine.match(line);
    if (!match.hasMatch())
        return false;

    bulletin["msg_id"] = match.captured(1).toInt();
    bulletin["date"] = match.captured(2);
    bulletin["type"] = match.captured(3);
    bulletin["size"] = match.captured(4).toInt();
    bulletin["category"] = match.captured(5);
    bulletin["dist"] = match.captured(6);
    bulletin["from_call"] = match.captured(7);
    bulletin["title"] = match.captured(8).trimmed();

    return true;
}

bool BBSCache::detectMessageEnd(const QString &line, int &msgId)
{
    auto match = m_rxMsgEnd.match(line);
    if (!match.hasMatch())
        return false;

    msgId = match.captured(1).toInt();
    return true;
}

// ============================================================================
// UI access — used by BBSCacheDialog
// ============================================================================

QList<QVariantMap> BBSCache::getCachedBulletins(const QString &node)
{
    QList<QVariantMap> results;

    QSqlQuery q(m_db);
    q.prepare("SELECT b.msg_id, b.date, b.type, b.size, b.category, b.dist, "
              "b.from_call, b.title, (b.body IS NOT NULL) as has_body "
              "FROM bulletins b "
              "JOIN nodes n ON b.node_id = n.id "
              "WHERE n.callsign = ? "
              "ORDER BY b.msg_id DESC");
    q.addBindValue(node);
    q.exec();

    while (q.next()) {
        QVariantMap m;
        m["msg_id"] = q.value(0);
        m["date"] = q.value(1);
        m["type"] = q.value(2);
        m["size"] = q.value(3);
        m["category"] = q.value(4);
        m["dist"] = q.value(5);
        m["from_call"] = q.value(6);
        m["title"] = q.value(7);
        m["has_body"] = q.value(8);
        results.append(m);
    }

    return results;
}

QString BBSCache::getCachedMessage(const QString &node, int msgId)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT b.body FROM bulletins b "
              "JOIN nodes n ON b.node_id = n.id "
              "WHERE n.callsign = ? AND b.msg_id = ?");
    q.addBindValue(node);
    q.addBindValue(msgId);
    q.exec();

    if (q.next())
        return q.value(0).toString();

    return QString();
}

QStringList BBSCache::getKnownNodes()
{
    QStringList nodes;
    QSqlQuery q(m_db);
    q.exec("SELECT callsign FROM nodes ORDER BY last_connected DESC");
    while (q.next())
        nodes.append(q.value(0).toString());
    return nodes;
}

bool BBSCache::hasCache(const QString &node)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM bulletins b "
              "JOIN nodes n ON b.node_id = n.id "
              "WHERE n.callsign = ?");
    q.addBindValue(node);
    q.exec();
    return q.next() && q.value(0).toInt() > 0;
}

void BBSCache::clearCache(const QString &node)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM bulletins WHERE node_id = "
              "(SELECT id FROM nodes WHERE callsign = ?)");
    q.addBindValue(node);
    q.exec();
}

void BBSCache::clearAll()
{
    QSqlQuery q(m_db);
    q.exec("DELETE FROM bulletins");
    q.exec("DELETE FROM sessions");
    q.exec("DELETE FROM nodes");
}
