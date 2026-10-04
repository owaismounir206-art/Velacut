// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace vedit::ui {

// Brand kits (SPEC §5.13ter): logos, colours, fonts, intro and outro, a watermark and favourite music, saved once and
// used in any project with a click; several kits (a personal channel, work). Each kit is a folder in
// <XDG data>/vedit/brandkits/<id>/ with kit.json and copies of its files (docs/FILE_FORMAT.md, appendix B), so a
// kit keeps working when the originals are moved. The kit in use is remembered; its colours come first in every
// colour picker (ClipInspector::swatches).
class BrandKitModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.brandKits")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY currentChanged FINAL)
    // The kit in use (empty when there is none yet).
    Q_PROPERTY(QString name READ name NOTIFY kitChanged FINAL)
    Q_PROPERTY(QVariantList colors READ colors NOTIFY kitChanged FINAL)
    Q_PROPERTY(QStringList fonts READ fonts NOTIFY kitChanged FINAL)
    Q_PROPERTY(QVariantList logos READ logos NOTIFY kitChanged FINAL)  // file URLs
    Q_PROPERTY(QUrl intro READ intro NOTIFY kitChanged FINAL)
    Q_PROPERTY(QUrl outro READ outro NOTIFY kitChanged FINAL)
    Q_PROPERTY(QVariantList music READ music NOTIFY kitChanged FINAL)  // [{name, url}]

public:
    enum Role
    {
        KitIdRole = Qt::UserRole + 1,
        NameRole,
    };

    explicit BrandKitModel(QObject *parent = nullptr);

    // The application's instance (set by AppController): the colour pickers read the kit in use from it.
    static BrandKitModel *instance();
    static void setInstance(BrandKitModel *model);
    static QString folder();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int current() const { return m_current; }
    void setCurrent(int index);
    QString name() const;
    QVariantList colors() const;
    QStringList fonts() const;
    QVariantList logos() const;
    QUrl intro() const;
    QUrl outro() const;
    QVariantList music() const;

    // A new kit, which becomes the one in use. Returns its index.
    Q_INVOKABLE int createKit(const QString &name);
    Q_INVOKABLE void renameKit(const QString &name);
    // Removes the kit in use; restoreKit() with the returned id brings it back (the snackbar's "Undo").
    Q_INVOKABLE QString removeKit();
    Q_INVOKABLE bool restoreKit(const QString &kitId);

    Q_INVOKABLE void addColor(const QColor &color);
    Q_INVOKABLE void removeColor(int index);
    Q_INVOKABLE void addFont(const QString &family);
    Q_INVOKABLE void removeFont(int index);
    // Files are copied into the kit. "" = done, else what went wrong.
    Q_INVOKABLE QString addLogo(const QUrl &file);
    Q_INVOKABLE void removeLogo(int index);
    Q_INVOKABLE QString setIntro(const QUrl &file);
    Q_INVOKABLE QString setOutro(const QUrl &file);
    Q_INVOKABLE void clearIntro();
    Q_INVOKABLE void clearOutro();
    Q_INVOKABLE QString addMusic(const QUrl &file);
    Q_INVOKABLE void removeMusic(int index);

signals:
    void countChanged();
    void currentChanged();
    void kitChanged();

private:
    struct Kit
    {
        QString id;
        QJsonObject json;
    };
    void load();
    bool save(const Kit &kit) const;
    Kit *currentKit();
    const Kit *currentKit() const;
    QString kitFolder(const QString &id) const;
    // Copies `file` into the current kit's `subfolder` and returns the path relative to the kit ("" on failure).
    QString copyIn(const QUrl &file, const QString &subfolder, QString *error);
    void removeFile(const QString &relative);
    void changed();
    QUrl fileUrl(const QString &relative) const;

    std::vector<Kit> m_kits;
    int m_current = -1;
};

} // namespace vedit::ui
