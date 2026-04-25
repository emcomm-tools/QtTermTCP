#ifndef BBSCACHEDIALOG_H
#define BBSCACHEDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>

class BBSCache;
struct Ui_ListenSession;

class BBSCacheDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BBSCacheDialog(BBSCache *cache, Ui_ListenSession *sess = nullptr, QWidget *parent = nullptr);

private slots:
    void nodeChanged(int index);
    void bulletinSelected();
    void clearNode();

private:
    void loadNodes();
    void loadBulletins(const QString &node);

    BBSCache *m_cache;
    Ui_ListenSession *m_sess;
    QComboBox *m_nodeCombo;
    QTableWidget *m_table;
    QTextEdit *m_messageView;
    QPushButton *m_clearBtn;
    QLabel *m_countLabel;
};

#endif // BBSCACHEDIALOG_H
