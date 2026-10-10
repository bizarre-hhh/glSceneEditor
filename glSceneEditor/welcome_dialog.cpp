#include "welcome_dialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <utility>

namespace
{
class ElidedLabel final : public QLabel
{
public:
    ElidedLabel(const QString& text, Qt::TextElideMode mode, QWidget* parent)
        : QLabel(text, parent), full_text_(text), mode_(mode)
    {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setToolTip(text);
    }
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        setText(fontMetrics().elidedText(full_text_, mode_, width()));
    }
private:
    QString full_text_;
    Qt::TextElideMode mode_;
};
} // namespace

WelcomeDialog::WelcomeDialog(const QString& recent_projects_file)
    : recent_projects_file_(recent_projects_file)
{
    setObjectName(QStringLiteral("welcomeDialog"));
    setFont(QFont(QStringLiteral("Microsoft YaHei"), 12));
    setWindowTitle(QStringLiteral("欢迎使用 glSceneEditor"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(1160, 900);
    setMinimumSize(1000, 860);
    setStyleSheet(QStringLiteral(
        "QDialog#welcomeDialog { background: #f3f6fb; }"
        "QFrame#welcomeCard { background: white; border: 1px solid #dbe3ef; border-radius: 16px; }"
        "QLabel#welcomeTitle { color: #182b49; font-size: 36px; font-weight: 600; }"
        "QLabel#welcomeSubtitle { color: #4b5f78; font-size: 18px; }"
        "QLabel#recentProjectsTitle { color: #23456b; font-size: 22px; font-weight: 600; }"
        "QLabel#recentProjectName { color: #23456b; font-size: 20px; font-weight: 600; }"
        "QLabel#recentProjectPath { color: #4b5f78; font-size: 18px; }"
        "QLabel#emptyRecentProjects { color: #5b6d87; font-size: 18px; padding: 16px 0; }"
        "QListWidget#recentProjectList { background: white; border: none; outline: none; color: #23456b; }"
        "QListWidget#recentProjectList::item { background: #f8fafd; border: 1px solid #e1e7f0; border-radius: 8px; padding: 8px 12px; }"
        "QListWidget#recentProjectList::item:hover { background: #eef4ff; border-color: #cdddf7; }"
        "QListWidget#recentProjectList::item:selected { background: #e8f0ff; color: #23456b; border-color: #b9cef5; }"
        "QPushButton { padding: 16px 32px; font-size: 20px; border-radius: 8px; }"
        "QPushButton#newProjectButton { color: white; background: #2563eb; border: 1px solid #2563eb; }"
        "QPushButton#newProjectButton:hover { background: #1d4ed8; }"
        "QPushButton#openProjectButton { color: #23456b; background: #eef4ff; border: 1px solid #cdddf7; }"
        "QPushButton#openProjectButton:hover { background: #dfeaff; }"
        "QPushButton#exitWelcomeButton { color: #4b5f78; border: none; background: transparent; font-size: 18px; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(44, 32, 44, 22);
    layout->addStretch();
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("welcomeCard"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(36, 28, 36, 24);
    card_layout->setSpacing(16);
    auto* title = new QLabel(QStringLiteral("欢迎使用 glSceneEditor"), card);
    title->setObjectName(QStringLiteral("welcomeTitle"));
    title->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("新建一个场景，或打开工程继续编辑。"), card);
    subtitle->setObjectName(QStringLiteral("welcomeSubtitle"));
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    card_layout->addWidget(subtitle);
    card_layout->addSpacing(12);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(18);
    auto* create = new QPushButton(QStringLiteral("新建工程"), card);
    create->setObjectName(QStringLiteral("newProjectButton"));
    create->setDefault(true);
    auto* open = new QPushButton(QStringLiteral("打开工程…"), card);
    open->setObjectName(QStringLiteral("openProjectButton"));
    buttons->addWidget(create);
    buttons->addWidget(open);
    card_layout->addLayout(buttons);
    card_layout->addSpacing(6);
    auto* recent_title = new QLabel(QStringLiteral("最近打开的工程"), card);
    recent_title->setObjectName(QStringLiteral("recentProjectsTitle"));
    card_layout->addWidget(recent_title);
    recent_list_ = new QListWidget(card);
    recent_list_->setObjectName(QStringLiteral("recentProjectList"));
    recent_list_->setIconSize(QSize(32, 32));
    recent_list_->setSpacing(4);
    recent_list_->setFrameShape(QFrame::NoFrame);
    recent_list_->setTextElideMode(Qt::ElideMiddle);
    recent_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    recent_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    recent_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    recent_list_->setCursor(Qt::PointingHandCursor);
    recent_list_->setAccessibleName(QStringLiteral("最近打开的工程，单击即可打开"));
    card_layout->addWidget(recent_list_);
    recent_empty_label_ = new QLabel(QStringLiteral("暂无最近工程，可以新建工程或选择打开工程。"), card);
    recent_empty_label_->setObjectName(QStringLiteral("emptyRecentProjects"));
    recent_empty_label_->setWordWrap(true);
    card_layout->addWidget(recent_empty_label_);
    const auto open_recent = [this](QListWidgetItem* item)
    {
        if (item)
            OpenProjectPath(item->data(Qt::UserRole).toString());
    };
    connect(recent_list_, &QListWidget::itemClicked, this, open_recent);
    connect(recent_list_, &QListWidget::itemActivated, this, open_recent);
    RefreshRecentProjects();
    auto* hint = new QLabel(QStringLiteral("工程文件会保存模型和场景位置，随时继续上次的工作。"), card);
    hint->setObjectName(QStringLiteral("welcomeSubtitle"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    card_layout->addWidget(hint);
    layout->addWidget(card);
    layout->addStretch();
    auto* exit = new QPushButton(QStringLiteral("退出"), this);
    exit->setObjectName(QStringLiteral("exitWelcomeButton"));
    layout->addWidget(exit, 0, Qt::AlignRight);
    connect(create, &QPushButton::clicked, this, [this]()
    {
        project_path_.clear();
        initial_project_ = ProjectData{};
        accept();
    });
    connect(open, &QPushButton::clicked, this, &WelcomeDialog::OpenProject);
    connect(exit, &QPushButton::clicked, this, &QDialog::reject);
}

void WelcomeDialog::OpenProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开工程"), ProjectFile::DefaultOpenDirectory(), ProjectFile::Filter());
    if (!path.isEmpty())
        OpenProjectPath(path);
}

void WelcomeDialog::OpenProjectPath(const QString& path)
{
    ProjectData project;
    QString error;
    if (!ProjectFile::Read(path, project, error))
    {
        QMessageBox::warning(this, QStringLiteral("无法打开工程"), error);
        return;
    }
    project_path_ = QFileInfo(path).absoluteFilePath();
    initial_project_ = std::move(project);
    accept();
}

void WelcomeDialog::RefreshRecentProjects()
{
    recent_list_->clear();
    const QStringList paths = RecentProjects::Read(recent_projects_file_);
    for (const QString& path : paths)
    {
        const QFileInfo info(path);
        const QString native_path = QDir::toNativeSeparators(path);
        const QString name = info.exists() ? info.fileName()
            : QStringLiteral("%1（文件不存在）").arg(info.fileName());
        auto* item = new QListWidgetItem(recent_list_);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::AccessibleTextRole, name + QStringLiteral("\n") + native_path);
        item->setToolTip(native_path + QStringLiteral("\n单击打开工程"));
        item->setSizeHint(QSize(0, 80));
        auto* row = new QWidget(recent_list_->viewport());
        // Let the list receive clicks anywhere within the icon or either label.
        row->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(18, 8, 18, 8);
        row_layout->setSpacing(16);
        auto* icon = new QLabel(row);
        icon->setPixmap(QIcon(QStringLiteral(":/icons/folder-open.svg")).pixmap(32, 32));
        icon->setFixedSize(34, 34);
        row_layout->addWidget(icon);
        auto* text_layout = new QVBoxLayout;
        text_layout->setSpacing(5);
        auto* name_label = new ElidedLabel(name, Qt::ElideRight, row);
        name_label->setObjectName(QStringLiteral("recentProjectName"));
        auto* path_label = new ElidedLabel(native_path, Qt::ElideMiddle, row);
        path_label->setObjectName(QStringLiteral("recentProjectPath"));
        text_layout->addWidget(name_label);
        text_layout->addWidget(path_label);
        row_layout->addLayout(text_layout, 1);
        recent_list_->setItemWidget(item, row);
    }
    recent_list_->setFixedHeight(paths.size() * 88);
    recent_list_->setVisible(!paths.isEmpty());
    recent_empty_label_->setVisible(paths.isEmpty());
}

void WelcomeDialog::showEvent(QShowEvent* event)
{
    RefreshRecentProjects();
    QDialog::showEvent(event);
}
