#ifndef BBSSTATIONEDITOR_H
#define BBSSTATIONEDITOR_H

#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QPushButton>
#include "BBSDirectory.h"

class BBSStationEditor : public QDialog
{
    Q_OBJECT

public:
    // Pass an existing station to edit, or a blank one to add new
    explicit BBSStationEditor(const BBSStation &station, QWidget *parent = nullptr);

    BBSStation result() const { return m_result; }

private slots:
    void addBandRow();
    void removeBandRow();
    void onAccept();

private:
    void populateBandRow(int row, const BBSBandEntry &be);
    BBSBandEntry readBandRow(int row) const;

    QLineEdit *m_callsign;
    QLineEdit *m_sysop;
    QLineEdit *m_network;
    QLineEdit *m_location;
    QLineEdit *m_grid;
    QLineEdit *m_power;
    QLineEdit *m_antenna;
    QPlainTextEdit *m_notes;
    QTableWidget *m_bandsTable;

    BBSStation m_result;
};

#endif // BBSSTATIONEDITOR_H
