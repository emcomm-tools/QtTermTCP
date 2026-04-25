#ifndef BBSDIRECTORY_H
#define BBSDIRECTORY_H

#include <QObject>
#include <QList>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkReply>

struct BBSBandEntry {
    QString band;           // "40m", "80m", etc.
    quint64 frequency;      // Hz
    QString mode;           // "VARA HF"
    QList<int> bandwidth;   // [500, 2300]
    QString hours;          // "00-12" UTC
};

struct BBSStation {
    QString callsign;
    QString sysop;
    QString network;
    QString location;
    QString grid;
    QString power;
    QString antenna;
    QString notes;
    QList<BBSBandEntry> bands;
};

class BBSDirectory : public QObject
{
    Q_OBJECT

public:
    explicit BBSDirectory(QObject *parent = nullptr);

    void loadFromFile(const QString &path);
    void fetchFromUrl(const QString &url);
    void saveToFile(const QString &path);

    const QList<BBSStation> &stations() const { return m_stations; }

    // Add or update a station (matched by callsign)
    void addOrUpdateStation(const BBSStation &station);
    void removeStation(const QString &callsign);
    bool hasStation(const QString &callsign) const;

    // Filtered view
    // bandwidthFilter: 500/2300/2750 (0 = no filter)
    // hourFilter: current UTC hour 0-23 (-1 = no filter)
    // bandFilter: e.g. "40m" (empty = no filter)
    QList<BBSStation> filtered(const QString &bandFilter,
                               int bandwidthFilter,
                               int hourFilter) const;

    QString localFilePath() const;

    // Convert frequency (Hz) to amateur band name
    static QString freqToBand(quint64 freq);

    // Maidenhead grid to lat/lon (returns false if grid invalid)
    static bool gridToLatLon(const QString &grid, double &lat, double &lon);

    // Haversine distance in km between two lat/lon points
    static double haversineKm(double lat1, double lon1, double lat2, double lon2);

    // Read user grid from ~/.config/emcomm-tools/user.json
    static QString userGrid();

signals:
    void loaded();
    void fetchError(const QString &error);

private slots:
    void onNetworkReply(QNetworkReply *reply);

private:
    void parseJson(const QByteArray &data);
    bool hourMatch(const QString &hours, int utcHour) const;

    QList<BBSStation> m_stations;
    QNetworkAccessManager *m_nam;
};

// Global instance — shared between QtTermTCP and TabDialog
extern BBSDirectory *g_bbsDirectory;

#endif // BBSDIRECTORY_H
