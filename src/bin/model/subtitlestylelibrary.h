/*
    SPDX-FileCopyrightText: 2026 Kdenlive contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "definitions.h"

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

#include <map>

/** @brief Storage scope of a subtitle style */
enum class SubtitleStyleScope {
    SubtitleFile, /**< style stored in the current subtitle file */
    Project,      /**< style stored in the project (globalSubtitleStyles property) */
    Shared,       /**< style stored in the shared (cross-project) user library */
};

/**
 * @class SubtitleStyleLibrary
 * @brief Manages shared (cross-project) subtitle styles.
 *
 * The library is composed of style sets, each set being a single ASS-compatible
 * file (<tt>.ass</tt>). Sets are loaded from two locations, in cascade order
 * (lowest to highest precedence):
 *  - built-in, read-only sets shipped with Kdenlive (found through
 *    QStandardPaths::AppLocalDataLocation, e.g. <tt>&lt;prefix&gt;/share/kdenlive/subtitle-styles/</tt>)
 *  - user sets stored in QStandardPaths::writableLocation(AppLocalDataLocation) + <tt>/subtitle-styles/</tt>
 *    (the same place KNewStuff downloads to, on every platform)
 *
 * Only the [V4+ Styles] section is used to build styles, but the whole file is
 * round-tripped: unknown keys of the [Kdenlive Extradata] section are preserved when
 * a set is rewritten. Subtitle animations are stored as <tt>Animation:
 * &lt;StyleName&gt;|&lt;params&gt;</tt> lines and are regenerated on save.
 */
class SubtitleStyleLibrary : public QObject
{
    Q_OBJECT

public:
    /** @brief Scope of a style inside the library */
    enum class Scope {
        BuiltIn, /**< read-only set installed with Kdenlive */
        Shared,  /**< editable set stored in the user directory */
    };

    explicit SubtitleStyleLibrary(QObject *parent = nullptr);
    /** @brief The application wide library, created on first use and owned by the application object.
     *  All subtitle models share it so that a change is visible in every sequence. */
    static SubtitleStyleLibrary *instance();
    /** @brief Constructor allowing to override the storage locations (used by tests) */
    SubtitleStyleLibrary(const QStringList &builtInDirectories, const QString &userDirectory, QObject *parent = nullptr);
    ~SubtitleStyleLibrary() override;

    /** @brief Reload all style sets from disk */
    void reload();

    /** @brief Names of all available style sets, built-in sets first */
    QStringList styleSetNames() const;
    /** @brief Names of the styles contained in the set @p setName */
    QStringList styleNames(const QString &setName) const;
    /** @brief Name of the set providing the style @p styleName (empty if unknown) */
    QString styleSetFor(const QString &styleName) const;

    /** @brief All shared styles, user sets overriding built-in ones */
    const std::map<QString, SubtitleStyle> &sharedStyles() const;
    /** @brief Styles provided by built-in (read-only) sets only */
    const std::map<QString, SubtitleStyle> &builtinStyles() const;
    /** @brief Styles provided by the user sets only */
    const std::map<QString, SubtitleStyle> &userStyles() const;

    /** @brief Returns true if a shared style with this name exists */
    bool hasStyle(const QString &styleName) const;
    /** @brief Returns true if the style is only provided by a built-in (read-only) set */
    bool isBuiltinStyle(const QString &styleName) const;
    /** @brief Resolves a shared style by name (user sets overriding built-in ones) */
    SubtitleStyle style(const QString &styleName) const;
    /** @brief Resolves a shared style by name, @p found tells whether it was found */
    SubtitleStyle style(const QString &styleName, bool *found) const;

    /**
     * @brief Add or update a style in the user store.
     *
     * The style is written to the user set already providing it, or to the
     * default user set otherwise. Built-in styles are never modified: a style
     * overriding a built-in one is created in the user store.
     */
    bool saveStyle(const QString &styleName, const SubtitleStyle &style, QString *errorString = nullptr);
    /** @brief Remove a style from the user store */
    bool deleteStyle(const QString &styleName, QString *errorString = nullptr);

    /** @brief Import a style set file into the user store */
    bool importSet(const QString &filePath, QString *errorString = nullptr);
    /** @brief Export a set to an ASS file */
    bool exportSet(const QString &setName, const QString &filePath, QString *errorString = nullptr) const;
    /** @brief Export the given styles to an ASS file as a new set.
     *  The styles are passed by value so that styles which do not live in the shared library
     *  (project or subtitle file styles) can be exported too. */
    bool exportStyles(const QString &filePath, const std::map<QString, SubtitleStyle> &styles, QString *errorString = nullptr) const;

    /** @brief Directory holding the user style sets */
    const QString &userDirectory() const;
    /** @brief Create the user directory on demand, returns false on failure */
    bool ensureUserDirectory() const;

    /** @brief Default writable directory for user style sets */
    static QString defaultUserDirectory();
    /** @brief Default read-only directories for built-in style sets */
    static QStringList defaultBuiltinDirectories();

Q_SIGNALS:
    /** @brief Emitted whenever the library content changed on disk */
    void libraryChanged();

private:
    /** @brief A single ASS style set */
    struct StyleSet
    {
        QString name;
        QString path;
        bool builtIn{false};
        std::map<QString, SubtitleStyle> styles;
        /** Preserved [Kdenlive Extradata] keys, order and duplicates kept */
        QList<QPair<QString, QString>> extraData;
    };

    void loadDirectory(const QString &path, bool builtIn);
    void loadSet(StyleSet &set) const;
    bool writeSet(const StyleSet &set, const QString &path, QString *errorString = nullptr) const;
    void rebuildCaches();
    StyleSet *findSet(const QString &name);
    const StyleSet *findSet(const QString &name) const;
    StyleSet &ensureUserSet();

    QStringList m_builtinDirectories;
    QString m_userDirectory;
    QList<StyleSet> m_sets;
    std::map<QString, SubtitleStyle> m_builtinStyles;
    std::map<QString, SubtitleStyle> m_userStyles;
    std::map<QString, SubtitleStyle> m_sharedStyles;
};
