#include "wizUserInfoWidget.h"

#include <QMenu>
#include <QFileDialog>

#include "wizdef.h"
#include "share/wizsettings.h"
#include "share/wizDatabaseManager.h"
#include "share/wizUserAvatar.h"
#include "../wizmainwindow.h"
#include "share/wizApiEntry.h"
#include "sync/wizkmxmlrpc.h"
#include "wizWebSettingsDialog.h"
#include "sync/wizAvatarUploader.h"
#include "sync/wizCloudPool.h"



CWizUserInfoWidget::CWizUserInfoWidget(CWizExplorerApp& app, QWidget *parent)
    : QToolButton(parent)
    , m_app(app)
    , m_db(app.databaseManager().db())
    , m_userSettings(NULL)
{
    setPopupMode(QToolButton::MenuButtonPopup);

    resetAvatar(false);
    resetUserInfo();

    connect(&m_db, SIGNAL(userInfoChanged()), SLOT(on_userInfo_changed()));

    // load builtin arraw
    QString strIconPath = ::WizGetSkinResourcePath(app.userSettings().skin()) + "arrow.png";
    m_iconArraw.addFile(strIconPath);

    // setup menu
    m_menuMain = new QMenu(this);

    QAction* actionAccountInfo = new QAction(tr("View account info"), m_menuMain);
    connect(actionAccountInfo, SIGNAL(triggered()), SLOT(on_action_accountInfo_triggered()));
    actionAccountInfo->setVisible(false);

    QAction* actionAccountSetup = new QAction(tr("Account settings"), m_menuMain);
    connect(actionAccountSetup, SIGNAL(triggered()), SLOT(on_action_accountSetup_triggered()));

    QAction* actionChangeAvatar = new QAction(tr("Change avatar"), m_menuMain);
    connect(actionChangeAvatar, SIGNAL(triggered()), SLOT(on_action_changeAvatar_triggered()));

    m_menuMain->addAction(actionAccountInfo);
    m_menuMain->addAction(actionAccountSetup);
    m_menuMain->addSeparator();
    m_menuMain->addAction(actionChangeAvatar);
    setMenu(m_menuMain);

    // avatar
    MainWindow* mainWindow = qobject_cast<MainWindow *>(m_app.mainWindow());
    m_avatarDownloader = mainWindow->avatarHost();
    connect(m_avatarDownloader, SIGNAL(downloaded(const QString&)),
            SLOT(on_userAvatar_downloaded(const QString&)));
}

QSize CWizUserInfoWidget::sizeHint() const
{
    // FIXME: builtin avatar size (36, 36), margin = 4 * 2, arraw width = 10
    return QSize(36 + fontMetrics().width(text()) + 18, 36);
}

void CWizUserInfoWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    int nAvatarWidth = 36;
    int nArrawWidth = 10;
    int nMargin = 4;

    QStyleOptionToolButton opt;
    initStyleOption(&opt);

    QPainter p(this);
    p.setClipRect(opt.rect);

    // draw user avatar
    QRect rectIcon = opt.rect;
    rectIcon.setLeft(rectIcon.left());
    rectIcon.setRight(rectIcon.left() + nAvatarWidth);
    if (!opt.icon.isNull()) {
        opt.icon.paint(&p, rectIcon);
    }

    // draw vip indicator
    QRect rectVip = rectIcon;
    rectVip.setLeft(rectVip.right() + nMargin);
    rectVip.setRight(rectVip.left() + fontMetrics().width(opt.text));
    rectVip.setBottom(rectVip.top() + rectVip.height()/2);
    if (!m_iconVipIndicator.isNull()) {
        m_iconVipIndicator.paint(&p, rectVip, Qt::AlignLeft|Qt::AlignTop);
    }

    // draw display name
    QRect rectText = rectVip;
    rectText.setBottom(rectText.bottom() + rectVip.height());
    rectText.setTop(rectText.bottom() - rectVip.height());
    if (!opt.text.isEmpty()) {
        if (opt.state & QStyle::State_MouseOver) {
            QFont font = p.font();
            font.setUnderline(true);
            p.setFont(font);
        }

        p.setPen("#787878"); // FIXME
        p.drawText(rectText, Qt::AlignLeft|Qt::AlignVCenter, opt.text);
    }

    // draw arraw
    QRect rectArrow = rectText;
    rectArrow.setLeft(rectArrow.right() + nMargin);
    rectArrow.setRight(rectArrow.left() + nArrawWidth);
    if (!m_iconArraw.isNull()) {
        m_iconArraw.paint(&p, rectArrow, Qt::AlignVCenter, QIcon::Normal);
    }
}

void CWizUserInfoWidget::mousePressEvent(QMouseEvent* event)
{
    // show menu at proper position
    if (hitButton(event->pos())) {
        QPoint pos(event->pos().x(), sizeHint().height());
        menu()->popup(mapToGlobal(pos), defaultAction());
    }
}

bool CWizUserInfoWidget::hitButton(const QPoint& pos) const
{
    // FIXME
    QRect rectArrow(36 + 8, 36 - fontMetrics().height(), sizeHint().width() - 36 - 4, fontMetrics().height());
    return rectArrow.contains(pos) ? true : false;
}

void CWizUserInfoWidget::resetAvatar(bool bForce)
{
    QString strAvatarPath = m_db.GetAvatarPath() + m_db.GetUserId() + ".png";

    QFileInfo avatar(strAvatarPath);
    if (!avatar.exists()) {
        strAvatarPath = ::WizGetSkinResourcePath(m_app.userSettings().skin()) + "avatar.png";
    }

    bool bNeedUpdate;
    if (avatar.exists()) {
        bNeedUpdate = avatar.created() > QDateTime::currentDateTime().addSecs(60*60*24) ? true : false;
    } else {
        bNeedUpdate = true;
    }

    if (bNeedUpdate || bForce) {
        QTimer::singleShot(3000, this, SLOT(downloadAvatar()));
    }

    setIcon(QIcon(strAvatarPath));
}

void CWizUserInfoWidget::resetUserInfo()
{
    WIZUSERINFO info;
    if (!m_db.GetUserInfo(info))
        return;

    if (info.strDisplayName.isEmpty()) {
        setText(::WizGetEmailPrefix(m_db.GetUserId()));
    } else {
        QString strName = fontMetrics().elidedText(info.strDisplayName, Qt::ElideRight, 150);
        setText(strName);
    }

    QString iconName;
    if (info.strUserType == "vip") {
        iconName = "vip1.png";
    } else if (info.strUserType == "vip2") {
        iconName = "vip2.png";
    } else if (info.strUserType == "vip3") {
        iconName = "vip3.png";
    } else {
        iconName = "vip0.png";
    }

    QString strIconPath = ::WizGetSkinResourcePath(m_app.userSettings().skin()) + iconName;
    m_iconVipIndicator.addFile(strIconPath);
}

void CWizUserInfoWidget::downloadAvatar()
{
    m_avatarDownloader->download(m_db.GetUserId());
}

void CWizUserInfoWidget::on_userAvatar_downloaded(const QString& strGUID)
{
    if (strGUID != m_db.GetUserId())
        return;

    resetAvatar(false);
    update();
}

void CWizUserInfoWidget::on_action_accountInfo_triggered()
{

}

void CWizUserInfoWidget::on_action_accountSetup_triggered()
{
    if (!m_userSettings) {
        m_userSettings = new CWizWebSettingsDialog(QSize(720, 400), window()); // use toplevel window as parent
        m_userSettings->setWindowTitle(tr("Account settings"));
        connect(m_userSettings, SIGNAL(accepted()), m_userSettings, SLOT(deleteLater()));
        connect(m_userSettings, SIGNAL(showProgress()), CWizCloudPool::instance(), SLOT(getToken()));
        connect(CWizCloudPool::instance(), SIGNAL(tokenAcquired(const QString&)),
                SLOT(on_action_accountSetup_requested(const QString&)));
    }

    m_userSettings->open();
}

void CWizUserInfoWidget::on_action_accountSetup_requested(const QString& strToken)
{
    if (!m_userSettings)
        return;

    if (strToken.isEmpty()) {
        m_userSettings->showError();
        return;
    }

    QString strUrl = CWizApiEntry::getAccountInfoUrl(strToken);
    m_userSettings->load(QUrl::fromEncoded(strUrl.toUtf8()));
}

void CWizUserInfoWidget::on_action_changeAvatar_triggered()
{
    QFileDialog dialog;
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter("*.png *.jpg *.jpeg");

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QStringList listFiles = dialog.selectedFiles();
    if (listFiles.size() != 1) {
        return;
    }

    CWizAvatarUploader* uploader = new CWizAvatarUploader(m_app, this);
    connect(uploader, SIGNAL(uploaded(bool)), SLOT(on_action_changeAvatar_uploaded(bool)));
    uploader->upload(listFiles[0]);
}

void CWizUserInfoWidget::on_action_changeAvatar_uploaded(bool ok)
{
    sender()->deleteLater();

    if (ok) {
        downloadAvatar();
    }
}

void CWizUserInfoWidget::on_userInfo_changed()
{
    resetAvatar(true);
    resetUserInfo();
}
