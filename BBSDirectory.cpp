#include "BBSDirectory.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <cmath>

BBSDirectory *g_bbsDirectory = nullptr;

BBSDirectory::BBSDirectory(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_nam, &QNetworkAccessManager::finished,
            this, &BBSDirectory::onNetworkReply);
}

QString BBSDirectory::localFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                  + "/QtTermTCP/bbs-directory";
    QDir().mkpath(dir);
    return dir + "/bbs-directory.json";
}

void BBSDirectory::loadFromFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "BBSDirectory: cannot open" << path;
        return;
    }
    parseJson(f.readAll());
    emit loaded();
}

void BBSDirectory::fetchFromUrl(const QString &url)
{
    m_nam->get(QNetworkRequest(QUrl(url)));
}

void BBSDirectory::onNetworkReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit fetchError(reply->errorString());
        return;
    }
    QByteArray data = reply->readAll();
    parseJson(data);
    saveToFile(localFilePath());
    emit loaded();
}

void BBSDirectory::parseJson(const QByteArray &data)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (doc.isNull()) {
        qDebug() << "BBSDirectory: JSON parse error:" << err.errorString();
        return;
    }

    m_stations.clear();
    QJsonArray stationArr = doc.object().value("stations").toArray();
    for (const QJsonValue &sv : stationArr) {
        QJsonObject sObj = sv.toObject();
        BBSStation st;
        st.callsign = sObj.value("callsign").toString();
        st.sysop    = sObj.value("sysop").toString();
        st.network  = sObj.value("network").toString();
        st.location = sObj.value("location").toString();
        st.grid     = sObj.value("grid").toString();
        st.power    = sObj.value("power").toString();
        st.antenna  = sObj.value("antenna").toString();
        st.notes    = sObj.value("notes").toString();

        QJsonArray bandsArr = sObj.value("bands").toArray();
        for (const QJsonValue &bv : bandsArr) {
            QJsonObject bObj = bv.toObject();
            BBSBandEntry be;
            be.band      = bObj.value("band").toString();
            be.frequency = (quint64)bObj.value("frequency").toDouble();
            be.mode      = bObj.value("mode").toString();
            be.hours     = bObj.value("hours").toString();

            QJsonArray bwArr = bObj.value("bandwidth").toArray();
            for (const QJsonValue &bwv : bwArr)
                be.bandwidth.append(bwv.toInt());

            st.bands.append(be);
        }
        m_stations.append(st);
    }
}

void BBSDirectory::saveToFile(const QString &path)
{
    QJsonArray stArr;
    for (const BBSStation &st : m_stations) {
        QJsonObject sObj;
        sObj["callsign"] = st.callsign;
        sObj["sysop"]    = st.sysop;
        sObj["network"]  = st.network;
        sObj["location"] = st.location;
        sObj["grid"]     = st.grid;
        sObj["power"]    = st.power;
        sObj["antenna"]  = st.antenna;
        sObj["notes"]    = st.notes;

        QJsonArray bArr;
        for (const BBSBandEntry &be : st.bands) {
            QJsonObject bObj;
            bObj["band"]      = be.band;
            bObj["frequency"] = (double)be.frequency;
            bObj["mode"]      = be.mode;
            bObj["hours"]     = be.hours;
            QJsonArray bwArr;
            for (int bw : be.bandwidth) bwArr.append(bw);
            bObj["bandwidth"] = bwArr;
            bArr.append(bObj);
        }
        sObj["bands"] = bArr;
        stArr.append(sObj);
    }
    QJsonObject root;
    root["version"]  = "1.0";
    root["stations"] = stArr;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}

void BBSDirectory::addOrUpdateStation(const BBSStation &station)
{
    for (int i = 0; i < m_stations.size(); ++i) {
        if (m_stations[i].callsign.toUpper() == station.callsign.toUpper()) {
            m_stations[i] = station;
            saveToFile(localFilePath());
            return;
        }
    }
    m_stations.append(station);
    saveToFile(localFilePath());
}

void BBSDirectory::removeStation(const QString &callsign)
{
    for (int i = 0; i < m_stations.size(); ++i) {
        if (m_stations[i].callsign.toUpper() == callsign.toUpper()) {
            m_stations.removeAt(i);
            saveToFile(localFilePath());
            return;
        }
    }
}

bool BBSDirectory::hasStation(const QString &callsign) const
{
    for (const BBSStation &st : m_stations)
        if (st.callsign.toUpper() == callsign.toUpper())
            return true;
    return false;
}

// hourFilter: current UTC hour (0-23), -1 = skip
// hours field: "00-12" or "12-00" (wraps midnight when start > end)
bool BBSDirectory::hourMatch(const QString &hours, int utcHour) const
{
    if (utcHour < 0) return true;
    QStringList parts = hours.split('-');
    if (parts.size() != 2) return true;
    int start = parts[0].toInt();
    int end   = parts[1].toInt();
    if (start == end) return true; // 00-00 = 24h
    if (start < end)
        return utcHour >= start && utcHour < end;
    else
        return utcHour >= start || utcHour < end;
}

QList<BBSStation> BBSDirectory::filtered(const QString &bandFilter,
                                          int bandwidthFilter,
                                          int hourFilter) const
{
    QList<BBSStation> result;
    for (const BBSStation &st : m_stations) {
        BBSStation filt = st;
        filt.bands.clear();
        for (const BBSBandEntry &be : st.bands) {
            if (!bandFilter.isEmpty() && be.band != bandFilter)
                continue;
            if (bandwidthFilter > 0 && !be.bandwidth.contains(bandwidthFilter))
                continue;
            if (!hourMatch(be.hours, hourFilter))
                continue;
            filt.bands.append(be);
        }
        if (!filt.bands.isEmpty())
            result.append(filt);
    }
    return result;
}

// Convert frequency (Hz) to standard amateur band name
QString BBSDirectory::freqToBand(quint64 freq)
{
    if (freq >= 1800000   && freq <= 2000000)   return "160m";
    if (freq >= 3500000   && freq <= 4000000)   return "80m";
    if (freq >= 5330500   && freq <= 5403500)   return "60m";
    if (freq >= 7000000   && freq <= 7300000)   return "40m";
    if (freq >= 10100000  && freq <= 10150000)  return "30m";
    if (freq >= 14000000  && freq <= 14350000)  return "20m";
    if (freq >= 18068000  && freq <= 18168000)  return "17m";
    if (freq >= 21000000  && freq <= 21450000)  return "15m";
    if (freq >= 24890000  && freq <= 24990000)  return "12m";
    if (freq >= 28000000  && freq <= 29700000)  return "10m";
    if (freq >= 50000000  && freq <= 54000000)  return "6m";
    if (freq >= 144000000 && freq <= 148000000) return "2m";
    return "";
}

bool BBSDirectory::gridToLatLon(const QString &grid, double &lat, double &lon)
{
    QString g = grid.trimmed().toUpper();
    if (g.length() < 4) return false;
    if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R') return false;
    if (!g[2].isDigit() || !g[3].isDigit()) return false;

    lon = (g[0].unicode() - 'A') * 20.0 - 180.0;
    lat = (g[1].unicode() - 'A') * 10.0 - 90.0;
    lon += g[2].digitValue() * 2.0;
    lat += g[3].digitValue() * 1.0;

    if (g.length() >= 6) {
        QChar sl = g[4], sr = g[5];
        if (sl >= 'A' && sl <= 'X' && sr >= 'A' && sr <= 'X') {
            lon += (sl.unicode() - 'A') * (2.0 / 24.0) + (1.0 / 24.0);
            lat += (sr.unicode() - 'A') * (1.0 / 24.0) + (0.5 / 24.0);
        } else {
            lon += 1.0; lat += 0.5;
        }
    } else {
        lon += 1.0; lat += 0.5;
    }
    return true;
}

double BBSDirectory::haversineKm(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371.0;
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dLat/2)*sin(dLat/2) +
               cos(lat1*M_PI/180.0)*cos(lat2*M_PI/180.0)*sin(dLon/2)*sin(dLon/2);
    return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

QString BBSDirectory::userGrid()
{
    QString path = QDir::homePath() + "/.config/emcomm-tools/user.json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    // Prefer gridSquare (GPS-updated), fall back to grid (configured)
    QString g = obj.value("gridSquare").toString().trimmed();
    if (g.isEmpty()) g = obj.value("grid").toString().trimmed();
    return g;
}
