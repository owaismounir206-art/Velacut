// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <vector>

namespace vedit::ui {

class AudioLibraryModel;
class EditorController;

// Every command of the editor with its name, icon, shortcut and the selections it applies to (docs/ARCHITECTURE.md
// §9). The contextual toolbar and the right-click menu show the ones of the current selection (SPEC 0bis rule 3);
// the universal search (Ctrl+K, rule 14) finds them together with the items of the libraries (filters, transitions,
// text styles, animations, music, media) and applies or opens what is chosen.
class ActionRegistry : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ActionRegistry)
    QML_UNCREATABLE("Provided by Editor.actions")

    // [{id, text, icon, shortcut, enabled}] for the selection, in toolbar order.
    Q_PROPERTY(QVariantList toolbar READ toolbar NOTIFY changed FINAL)

public:
    // What is selected: an action shows for the selections it lists.
    enum Context
    {
        Nothing = 1 << 0,
        Video = 1 << 1,
        Image = 1 << 2,
        Audio = 1 << 3,
        Text = 1 << 4,
        TransitionSelected = 1 << 5,
        AdjustmentLayer = 1 << 6,
        AnyClip = Video | Image | Audio | Text | AdjustmentLayer,
    };

    explicit ActionRegistry(EditorController &editor);

    QVariantList toolbar() const;
    Q_INVOKABLE bool trigger(const QString &id);
    Q_INVOKABLE bool isEnabled(const QString &id) const;

    // Results of the universal search, best first: [{kind ("action", "filter", "transition", "text", "animation",
    // "music", "media"), id, text, detail, icon, enabled}]. An empty text lists the actions of the selection.
    Q_INVOKABLE QVariantList search(const QString &text, int limit = 40) const;
    // Applies or opens a result of search().
    Q_INVOKABLE bool activate(const QString &kind, const QString &id);

    // The local music library (owned by the application), searched too.
    void setMusicLibrary(AudioLibraryModel *library);

signals:
    void changed();

private:
    struct Action
    {
        QString id;
        QString text;
        QString icon;
        QString shortcut;
        int contexts = 0;  // where it shows in the toolbar and the menu; 0 = only in the search
        std::function<bool()> enabled;
        std::function<bool()> run;
    };
    void add(Action action);
    int currentContext() const;
    const Action *find(const QString &id) const;

    EditorController &m_editor;
    std::vector<Action> m_actions;
    QPointer<AudioLibraryModel> m_music;
};

} // namespace vedit::ui
