#include "BBSStationEditor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QLabel>
#include <QFontMetrics>

BBSStationEditor::BBSStationEditor(const BBSStation &station, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(station.callsign.isEmpty() ? "Add BBS Station" : "Edit BBS Station");
    setMinimumWidth(620);

    QVBoxLayout *main = new QVBoxLayout(this);

    // ---- Station info ----
    QGroupBox *infoBox = new QGroupBox("Station Info");
    QFormLayout *form = new QFormLayout(infoBox);

    m_callsign = new QLineEdit(station.callsign);
    m_sysop    = new QLineEdit(station.sysop);
    m_network  = new QLineEdit(station.network);
    m_location = new QLineEdit(station.location);
    m_grid     = new QLineEdit(station.grid);
    m_power    = new QLineEdit(station.power);
    m_antenna  = new QLineEdit(station.antenna);
    m_notes    = new QPlainTextEdit(station.notes);
    m_notes->setMaximumHeight(QFontMetrics(m_notes->font()).lineSpacing() * 3 + 12);

    form->addRow("Callsign",  m_callsign);
    form->addRow("Sysop",     m_sysop);
    form->addRow("Network",   m_network);
    form->addRow("Location",  m_location);
    form->addRow("Grid",      m_grid);
    form->addRow("Power",     m_power);
    form->addRow("Antenna",   m_antenna);
    form->addRow("Notes",     m_notes);
    main->addWidget(infoBox);

    // ---- Bands table ----
    QGroupBox *bandsBox = new QGroupBox("Bands");
    QVBoxLayout *bandsLayout = new QVBoxLayout(bandsBox);

    m_bandsTable = new QTableWidget(0, 6);
    m_bandsTable->setHorizontalHeaderLabels({"Band", "Freq (Hz)", "Mode", "Bandwidth (comma sep.)", "Hours (UTC)", ""});
    m_bandsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_bandsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_bandsTable->horizontalHeader()->setMinimumSectionSize(80);
    m_bandsTable->verticalHeader()->setVisible(false);
    m_bandsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bandsTable->setMinimumHeight(120);
    bandsLayout->addWidget(m_bandsTable);

    QHBoxLayout *bandBtns = new QHBoxLayout();
    QPushButton *addRow    = new QPushButton("+ Add Band");
    QPushButton *removeRow = new QPushButton("- Remove");
    bandBtns->addWidget(addRow);
    bandBtns->addWidget(removeRow);
    bandBtns->addStretch();
    bandBtns->addWidget(new QLabel("Bandwidth examples: 500  or  500,2300"));
    bandsLayout->addLayout(bandBtns);
    main->addWidget(bandsBox);

    // Populate existing bands
    for (const BBSBandEntry &be : station.bands) {
        int row = m_bandsTable->rowCount();
        m_bandsTable->insertRow(row);
        populateBandRow(row, be);
    }

    // ---- OK / Cancel ----
    QDialogButtonBox *bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    main->addWidget(bbox);

    connect(addRow,    &QPushButton::clicked, this, &BBSStationEditor::addBandRow);
    connect(removeRow, &QPushButton::clicked, this, &BBSStationEditor::removeBandRow);
    connect(bbox, &QDialogButtonBox::accepted, this, &BBSStationEditor::onAccept);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void BBSStationEditor::populateBandRow(int row, const BBSBandEntry &be)
{
    m_bandsTable->setItem(row, 0, new QTableWidgetItem(be.band));
    m_bandsTable->setItem(row, 1, new QTableWidgetItem(QString::number(be.frequency)));
    m_bandsTable->setItem(row, 2, new QTableWidgetItem(be.mode.isEmpty() ? "VARA HF" : be.mode));

    QStringList bwParts;
    for (int bw : be.bandwidth) bwParts << QString::number(bw);
    m_bandsTable->setItem(row, 3, new QTableWidgetItem(bwParts.join(",")));
    m_bandsTable->setItem(row, 4, new QTableWidgetItem(be.hours.isEmpty() ? "00-00" : be.hours));
}

BBSBandEntry BBSStationEditor::readBandRow(int row) const
{
    BBSBandEntry be;
    be.band      = m_bandsTable->item(row, 0) ? m_bandsTable->item(row, 0)->text().trimmed() : "";
    be.frequency = m_bandsTable->item(row, 1) ? m_bandsTable->item(row, 1)->text().toULongLong() : 0;
    be.mode      = m_bandsTable->item(row, 2) ? m_bandsTable->item(row, 2)->text().trimmed() : "VARA HF";
    be.hours     = m_bandsTable->item(row, 4) ? m_bandsTable->item(row, 4)->text().trimmed() : "00-00";

    QString bwStr = m_bandsTable->item(row, 3) ? m_bandsTable->item(row, 3)->text() : "";
    for (const QString &s : bwStr.split(',', QString::SkipEmptyParts))
        be.bandwidth.append(s.trimmed().toInt());

    return be;
}

void BBSStationEditor::addBandRow()
{
    int row = m_bandsTable->rowCount();
    m_bandsTable->insertRow(row);
    m_bandsTable->setItem(row, 0, new QTableWidgetItem(""));
    m_bandsTable->setItem(row, 1, new QTableWidgetItem(""));
    m_bandsTable->setItem(row, 2, new QTableWidgetItem("VARA HF"));
    m_bandsTable->setItem(row, 3, new QTableWidgetItem(""));
    m_bandsTable->setItem(row, 4, new QTableWidgetItem("00-00"));
    m_bandsTable->selectRow(row);
}

void BBSStationEditor::removeBandRow()
{
    int row = m_bandsTable->currentRow();
    if (row >= 0)
        m_bandsTable->removeRow(row);
}

void BBSStationEditor::onAccept()
{
    QString call = m_callsign->text().trimmed().toUpper();
    if (call.isEmpty()) {
        QMessageBox::warning(this, "Missing Data", "Callsign is required.");
        return;
    }

    m_result.callsign = call;
    m_result.sysop    = m_sysop->text().trimmed();
    m_result.network  = m_network->text().trimmed();
    m_result.location = m_location->text().trimmed();
    m_result.grid     = m_grid->text().trimmed();
    m_result.power    = m_power->text().trimmed();
    m_result.antenna  = m_antenna->text().trimmed();
    m_result.notes    = m_notes->toPlainText().trimmed();
    m_result.bands.clear();

    for (int i = 0; i < m_bandsTable->rowCount(); ++i) {
        BBSBandEntry be = readBandRow(i);
        if (!be.band.isEmpty() && be.frequency > 0)
            m_result.bands.append(be);
    }

    accept();
}
