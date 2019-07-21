#include "widget.h"
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QTreeWidget>
#include <QListWidget>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStandardPaths>
#include <QPushButton>
#include <QMessageBox>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
{
    for (const QString &dirPath : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        qDebug() << "Loading applications from" << dirPath;
        QDir applicationsDir(dirPath);

        for (const QFileInfo &file : applicationsDir.entryInfoList(QStringList("*.desktop"))) {
            loadDesktopFile(file);
        }
    }
    // Check that we shit with multiple .desktop files, but some nodisplay files
    for (const QString &appId : m_supportedMimetypes.uniqueKeys()) {
        if (!m_desktopFileNames.contains(appId)) {
            qWarning() << appId << "does not have an associated desktop file!";
            continue;
        }
    }

    // Preload up front, so it doesn't get sluggish when selecting applications supporting a lot
    for (const QString &mimetypeName : m_supportedMimetypes.values()) {
        if (m_mimeTypeIcons.contains(mimetypeName)) {
            continue;
        }
        const QMimeType mimetype = m_mimeDb.mimeTypeForName(mimetypeName);
        m_mimeTypeIcons[mimetypeName] =  QIcon::fromTheme(mimetype.iconName());
    }

    QHBoxLayout *mainLayout = new QHBoxLayout;
    setLayout(mainLayout);
    m_applicationList = new QTreeWidget;
    mainLayout->addWidget(m_applicationList);

    QGridLayout *rightLayout = new QGridLayout;
    mainLayout->addLayout(rightLayout);

    m_setDefaultButton = new QPushButton(tr("Set as default application for these file types"));
    m_setDefaultButton->setEnabled(false);

    m_mimetypeList = new QListWidget;
    m_mimetypeList->setSelectionMode(QAbstractItemView::MultiSelection);

    rightLayout->addWidget(m_mimetypeList);
    rightLayout->addWidget(m_setDefaultButton);

    QStringList types = m_applications.uniqueKeys();
    std::sort(types.begin(), types.end());
    for (const QString &type : types)  {
        QTreeWidgetItem *typeItem = new QTreeWidgetItem(QStringList(type));
        QStringList applications = m_applications[type].toList();
        std::sort(applications.begin(), applications.end(), [=](const QString &a, const QString &b) {
            return m_applicationNames[a] < m_applicationNames[b];
        });
        for (const QString &application : applications) {
            QTreeWidgetItem *appItem = new QTreeWidgetItem(QStringList(m_applicationNames[application]));
            appItem->setData(0, Qt::UserRole, application);
            appItem->setIcon(0, QIcon::fromTheme(m_applicationIcons[application]));
            typeItem->addChild(appItem);
        }
        m_applicationList->addTopLevelItem(typeItem);
    }
    m_applicationList->setHeaderHidden(true);

    connect(m_applicationList, &QTreeWidget::itemSelectionChanged, this, &Widget::onMimetypeSelected);
    connect(m_setDefaultButton, &QPushButton::clicked, this, &Widget::onSetDefaultClicked);
}

Widget::~Widget()
{

}

void Widget::onMimetypeSelected()
{
    m_setDefaultButton->setEnabled(false);
    m_mimetypeList->clear();

    QList<QTreeWidgetItem*> selectedItems = m_applicationList->selectedItems();
    if (selectedItems.count() != 1) {
        return;
    }

    const QTreeWidgetItem *item = selectedItems.first();
    if (!item->parent()) {
        return;
    }

    const QString mimetypeGroup = item->parent()->text(0);
    const QString application = item->data(0, Qt::UserRole).toString();

    for (const QString &supportedMime : m_supportedMimetypes.values(application)) {
        if (!supportedMime.startsWith(mimetypeGroup)) {
            continue;
        }
        const QMimeType mimetype = m_mimeDb.mimeTypeForName(supportedMime);
        QString name = mimetype.filterString().trimmed();
        if (name.isEmpty()) {
            name = mimetype.comment().trimmed();
        }
        if (name.isEmpty()) {
            name = mimetype.name().trimmed();
        }
        QListWidgetItem *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, mimetype.name());
        item->setIcon(m_mimeTypeIcons[supportedMime]);
        m_mimetypeList->addItem(item);
        item->setSelected(true);
    }

    m_setDefaultButton->setEnabled(m_mimetypeList->count() > 0);
}

void Widget::onSetDefaultClicked()
{
    QList<QTreeWidgetItem*> selectedItems = m_applicationList->selectedItems();
    if (selectedItems.count() != 1) {
        return;
    }

    const QTreeWidgetItem *item = selectedItems.first();
    if (!item->parent()) {
        return;
    }

    const QString application = item->data(0, Qt::UserRole).toString();
    if (application.isEmpty()) {
        return;
    }

    QSet<QString> unselected;
    QSet<QString> selected;
    for (int i=0; i<m_mimetypeList->count(); i++) {
        QListWidgetItem *item = m_mimetypeList->item(i);
        const QString name = item->data(Qt::UserRole).toString();
        if (item->isSelected()) {
            selected.insert(name);
        } else {
            unselected.insert(name);
        }
    }

    setDefault(application, selected, unselected);
}

void Widget::loadDesktopFile(const QFileInfo &fileInfo)
{
    // Ugliest implementation of .desktop file reading ever

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open" << fileInfo.fileName();
        return;
    }

    QStringList mimetypes;
    QString appName;
    QString appId = fileInfo.fileName();
    QString iconName;

    bool inCorrectGroup = false;
    bool noDisplay = false;

    while (!file.atEnd()) {
        QString line = file.readLine().simplified();

        if (line.startsWith('[')) {
            inCorrectGroup = (line == "[Desktop Entry]");
            continue;
        }

        if (!inCorrectGroup) {
            continue;
        }

        if (line.startsWith("MimeType")) {
            line.remove(0, line.indexOf('=') + 1);
            mimetypes = line.split(';', QString::SkipEmptyParts);
            continue;
        }

        if (line.startsWith("Name") && !line.contains('[')) {
            line.remove(0, line.indexOf('=') + 1);
            appName = line;
            continue;
        }

        if (line.startsWith("Icon")) {
            line.remove(0, line.indexOf('=') + 1);
            iconName = line;
            continue;
        }

        if (line.startsWith("Exec")) {
            line.remove(0, line.indexOf('=') + 1);
            if (line.isEmpty()) {
                continue;
            }
            QStringList parts = line.split(' ');
            if (parts.first() == "env" && parts.count() > 2) {
                line = parts[2];
            } else {
                line = parts.first();
            }

            appId = line;
            continue;
        }

        if (line.startsWith("NoDisplay=") && line.contains("true", Qt::CaseInsensitive)) {
            noDisplay = true;
        }
    }


    if (mimetypes.isEmpty()) {
        return;
    }

    if (appName.isEmpty()) {
        qWarning() << "Missing name" << fileInfo.fileName() << appId << mimetypes;
        appName = appId;
    }


    // If an application has a .desktop file without NoDisplay use that, otherwise use one of the ones with NoDisplay anyways
    if (!noDisplay || !m_desktopFileNames.contains(appId)) {
        m_desktopFileNames[appId] = fileInfo.fileName();
    }

    // Dumb assumption; if it has an icon it probably has the proper name
    if ((!noDisplay || !m_applicationIcons.contains(appId)) && !iconName.isEmpty()) {
        m_applicationIcons[appId] = iconName;
        m_applicationNames[appId] = appName;
    }

    for (const QString &readMimeName : mimetypes) {
        // Resolve aliases etc
        const QMimeType mimetype = m_mimeDb.mimeTypeForName(readMimeName.trimmed());
        if (!mimetype.isValid()) {
            continue;
        }

        const QString name = mimetype.name();
        if (m_supportedMimetypes.contains(appId, name)) {
            continue;
        }

        const QStringList parts = name.split('/');
        if (parts.count() != 2) {
            continue;
        }

        const QString type = parts[0].trimmed();

        m_applications[type].insert(appId);
        m_supportedMimetypes.insert(appId, name);
    }
}

void Widget::setDefault(const QString &appName, const QSet<QString> &mimetypes, const QSet<QString> &unselectedMimetypes)
{
    QString desktopFile = m_desktopFileNames.value(appName);
    if (desktopFile.isEmpty()) {
        qWarning() << "invalid" << appName;
        return;
    }

    const QString filePath = QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).absoluteFilePath("mimeapps.list");
    QFile file(filePath);

    // Read in existing mimeapps.list, skipping the lines for the mimetypes we're updating
    QList<QByteArray> existingContent;
    QList<QByteArray> existingAssociations;
    if (file.open(QIODevice::ReadOnly)) {
        bool inCorrectGroup = false;
        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();

            if (line.isEmpty()) {
                continue;
            }

            if (line.startsWith('[')) {
                inCorrectGroup = (line == "[Default Applications]");
                if (!inCorrectGroup) {
                    existingContent.append(line);
                }
                continue;
            }

            if (!inCorrectGroup) {
                existingContent.append(line);
                continue;
            }

            if (!line.contains('=')) {
                existingAssociations.append(line);
                continue;
            }

            const QString mimetype = m_mimeDb.mimeTypeForName(line.split('=').first().trimmed()).name();
            if (!mimetypes.contains(mimetype) && !unselectedMimetypes.contains(mimetype)) {
                existingAssociations.append(line);
            }
        }

        file.close();
    } else {
        qDebug() << "Unable to open file for reading";
    }

    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Failed to store settings"), file.errorString());
        return;
    }

    for (const QByteArray &line : existingContent) {
        file.write(line + '\n');
    }
    file.write("\n[Default Applications]\n");
    for (const QByteArray &line : existingAssociations) {
        file.write(line + '\n');
    }

    for (const QString &mimetype : mimetypes) {
        file.write(QString(mimetype + '=' + m_desktopFileNames[appName] + '\n').toUtf8());
    }

    return;
}
