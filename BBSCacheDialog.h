#ifndef BBSCACHEDIALOG_H
#define BBSCACHEDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>

class BBSCache;

class BBSCacheDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BBSCacheDialog(BBSCache *cache, QWidget *parent = nullptr);

private slots:
    void nodeChanged(int index);
    void bulletinSelected();
    void clearNode();

private:
    void loadNodes();
    void loadBulletins(const QString &node);

    BBSCache *m_cache;
    QComboBox *m_nodeCombo;
    QTableWidget *m_table;
    QTextEdit *m_messageView;
    QPushButton *m_clearBtn;
    QLabel *m_countLabel;
};

#endif // BBSCACHEDIALOG_H
