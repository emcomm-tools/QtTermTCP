#include "BBSCacheDialog.h"
#include "BBSCache.h"
#include "QtTermTCP.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QMessageBox>

BBSCacheDialog::BBSCacheDialog(BBSCache *cache, Ui_ListenSession *sess, QWidget *parent)
    : QDialog(parent), m_cache(cache), m_sess(sess)
{
    setWindowTitle("BBS Cache Browser");
    setMinimumSize(800, 600);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Top bar — node selector
    QHBoxLayout *topBar = new QHBoxLayout();

    QLabel *nodeLabel = new QLabel("Node:");
    m_nodeCombo = new QComboBox();
    m_nodeCombo->setMinimumWidth(200);
    m_countLabel = new QLabel("");
    m_clearBtn = new QPushButton("Clear Cache");

    topBar->addWidget(nodeLabel);
    topBar->addWidget(m_nodeCombo);
    topBar->addWidget(m_countLabel);
    topBar->addStretch();
    topBar->addWidget(m_clearBtn);

    mainLayout->addLayout(topBar);

    // Splitter — bulletin list on top, message view on bottom
    QSplitter *splitter = new QSplitter(Qt::Vertical);

    // Bulletin table
    m_table = new QTableWidget();
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels(
        {"ID", "Date", "Type", "Size", "Category", "Dist", "From", "Title"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);

    // Set column widths
    m_table->setColumnWidth(0, 60);   // ID
    m_table->setColumnWidth(1, 70);   // Date
    m_table->setColumnWidth(2, 40);   // Type
    m_table->setColumnWidth(3, 60);   // Size
    m_table->setColumnWidth(4, 70);   // Category
    m_table->setColumnWidth(5, 50);   // Dist
    m_table->setColumnWidth(6, 80);   // From

    splitter->addWidget(m_table);

    // Message view
    m_messageView = new QTextEdit();
    m_messageView->setReadOnly(true);
    m_messageView->setFont(QFont("Courier", 10));

    splitter->addWidget(m_messageView);

    // Set splitter proportions (60% table, 40% message)
    splitter->setStretchFactor(0, 6);
    splitter->setStretchFactor(1, 4);

    mainLayout->addWidget(splitter);

    // Connections
    connect(m_nodeCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(nodeChanged(int)));
    connect(m_table, SIGNAL(itemSelectionChanged()),
            this, SLOT(bulletinSelected()));
    connect(m_clearBtn, SIGNAL(clicked()),
            this, SLOT(clearNode()));

    // Load data
    loadNodes();
}

void BBSCacheDialog::loadNodes()
{
    m_nodeCombo->clear();

    if (!m_cache)
        return;

    QStringList nodes = m_cache->getKnownNodes();
    for (const QString &node : nodes)
        m_nodeCombo->addItem(node);

    if (!nodes.isEmpty())
        loadBulletins(nodes.first());
}

void BBSCacheDialog::nodeChanged(int index)
{
    if (index < 0)
        return;

    QString node = m_nodeCombo->currentText();
    loadBulletins(node);
    m_messageView->clear();
}

void BBSCacheDialog::loadBulletins(const QString &node)
{
    m_table->setRowCount(0);

    if (!m_cache)
        return;

    QList<QVariantMap> bulletins = m_cache->getCachedBulletins(node);

    m_table->setRowCount(bulletins.size());
    m_countLabel->setText(QString("%1 bulletins cached").arg(bulletins.size()));

    for (int row = 0; row < bulletins.size(); ++row) {
        const QVariantMap &b = bulletins[row];

        m_table->setItem(row, 0, new QTableWidgetItem(b["msg_id"].toString()));
        m_table->setItem(row, 1, new QTableWidgetItem(b["date"].toString()));
        m_table->setItem(row, 2, new QTableWidgetItem(b["type"].toString()));
        m_table->setItem(row, 3, new QTableWidgetItem(b["size"].toString()));
        m_table->setItem(row, 4, new QTableWidgetItem(b["category"].toString()));
        m_table->setItem(row, 5, new QTableWidgetItem(b["dist"].toString()));
        m_table->setItem(row, 6, new QTableWidgetItem(b["from_call"].toString()));
        m_table->setItem(row, 7, new QTableWidgetItem(b["title"].toString()));

        // Highlight rows that have cached message body
        if (b["has_body"].toBool()) {
            for (int col = 0; col < 8; col++) {
                QTableWidgetItem *item = m_table->item(row, col);
                if (item)
                    item->setForeground(QColor(0, 180, 0));  // Green = has body
            }
        }
    }
}

void BBSCacheDialog::bulletinSelected()
{
    QList<QTableWidgetItem*> items = m_table->selectedItems();
    if (items.isEmpty())
        return;

    int row = items.first()->row();
    QTableWidgetItem *idItem = m_table->item(row, 0);
    if (!idItem)
        return;

    int msgId = idItem->text().toInt();
    QString node = m_nodeCombo->currentText();

    QString body = m_cache->getCachedMessage(node, msgId);
    if (body.isEmpty()) {
        m_messageView->setPlainText("(Message body not cached — connect and use R "
                                    + QString::number(msgId) + " to download)");
        // Fill the input line with the R command if session is active
        if (m_sess && m_sess->inputWindow) {
            m_sess->inputWindow->setText(QString("R %1").arg(msgId));
            m_sess->inputWindow->setFocus();
        }
    } else {
        m_messageView->setPlainText(body);
    }
}

void BBSCacheDialog::clearNode()
{
    QString node = m_nodeCombo->currentText();
    if (node.isEmpty())
        return;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Clear Cache",
        QString("Clear all cached data for %1?").arg(node),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_cache->clearCache(node);
        loadBulletins(node);
        m_messageView->clear();
    }
}
