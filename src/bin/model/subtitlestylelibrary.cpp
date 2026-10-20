/*
    SPDX-FileCopyrightText: 2026 Kdenlive contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "subtitlestylelibrary.h"

#include "kdenlive_debug.h"
#include "klocalizedstring.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <utility>

namespace {
const QString s_userSetName = QStringLiteral("user");

/** @brief Returns true if @p params (the part after "StyleName|") contains a known animation kind */
bool isKnownAnimationKind(const QString &params)
{
    const QStringList tokens = params.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const int separator = token.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }
        if (token.left(separator).trimmed().compare(QLatin1String("kind"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString kind = token.mid(separator + 1).trimmed().toLower();
        return kind == QLatin1String("fade") || kind == QLatin1String("move") || kind == QLatin1String("scale") || kind == QLatin1String("rotate") ||
               kind == QLatin1String("typewriter") || kind == QLatin1String("erase");
    }
    return false;
}
} // namespace

SubtitleStyleLibrary::SubtitleStyleLibrary(QObject *parent)
    : SubtitleStyleLibrary(defaultBuiltinDirectories(), defaultUserDirectory(), parent)
{
}

SubtitleStyleLibrary *SubtitleStyleLibrary::instance()
{
    static SubtitleStyleLibrary *library = new SubtitleStyleLibrary(QCoreApplication::instance());
    return library;
}

SubtitleStyleLibrary::SubtitleStyleLibrary(const QStringList &builtInDirectories, const QString &userDirectory, QObject *parent)
    : QObject(parent)
    , m_builtinDirectories(builtInDirectories)
    , m_userDirectory(userDirectory)
{
    reload();
}

SubtitleStyleLibrary::~SubtitleStyleLibrary() = default;

void SubtitleStyleLibrary::reload()
{
    m_sets.clear();
    const QString userPath = QDir(m_userDirectory).absolutePath();
    for (const QString &directory : std::as_const(m_builtinDirectories)) {
        // The writable user directory can also be reported as a built-in location, don't load it twice
        if (QDir(directory).absolutePath() == userPath) {
            continue;
        }
        loadDirectory(directory, true);
    }
    loadDirectory(m_userDirectory, false);
    rebuildCaches();
}

void SubtitleStyleLibrary::loadDirectory(const QString &path, bool builtIn)
{
    QDir dir(path);
    if (!dir.exists()) {
        return;
    }
    const QStringList files = dir.entryList({QStringLiteral("*.ass")}, QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        StyleSet set;
        set.path = dir.absoluteFilePath(fileName);
        set.name = QFileInfo(fileName).completeBaseName();
        set.builtIn = builtIn;
        loadSet(set);
        if (!set.styles.empty()) {
            m_sets.append(set);
        }
    }
}

void SubtitleStyleLibrary::loadSet(StyleSet &set) const
{
    QFile file(set.path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    enum Section { None, ScriptInfo, ExtraData, Styles };
    Section section = None;
    QTextStream stream(&file);
    QString line;
    // "Animation:" / "AnimationOut:" lines are resolved once the whole file (including its styles)
    // has been read
    QList<QPair<QString, QString>> pendingAnimations;
    QList<QPair<QString, QString>> pendingAnimationsOut;
    while (stream.readLineInto(&line)) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            const QString sectionName = trimmed.mid(1, trimmed.size() - 2).trimmed().toLower();
            if (sectionName == QLatin1String("script info")) {
                section = ScriptInfo;
            } else if (sectionName == QLatin1String("kdenlive extradata")) {
                section = ExtraData;
            } else if (sectionName.contains(QLatin1String("styles"))) {
                section = Styles;
            } else {
                section = None;
            }
            continue;
        }
        const int colon = trimmed.indexOf(QLatin1Char(':'));
        switch (section) {
        case ScriptInfo:
            // Script Info keys are not used: the set name is derived from the file name
            break;
        case ExtraData:
            if (colon > 0) {
                const QString key = trimmed.left(colon).trimmed();
                const QString value = trimmed.mid(colon + 1).trimmed();
                if (key.compare(QLatin1String("Animation"), Qt::CaseInsensitive) == 0) {
                    const int separator = value.indexOf(QLatin1Char('|'));
                    if (separator > 0 && isKnownAnimationKind(value.mid(separator + 1))) {
                        // Defer: the style the animation belongs to is only known after the styles section
                        pendingAnimations.append({value.left(separator).trimmed(), value.mid(separator + 1)});
                        break;
                    }
                }
                if (key.compare(QLatin1String("AnimationOut"), Qt::CaseInsensitive) == 0) {
                    const int separator = value.indexOf(QLatin1Char('|'));
                    if (separator > 0 && isKnownAnimationKind(value.mid(separator + 1))) {
                        // Defer: the style the animation belongs to is only known after the styles section
                        pendingAnimationsOut.append({value.left(separator).trimmed(), value.mid(separator + 1)});
                        break;
                    }
                }
                // Preserve every other key (and duplicates such as unknown "Animation:" lines) for round-tripping
                set.extraData.append({key, value});
            }
            break;
        case Styles: {
            if (colon > 0 && trimmed.left(colon).trimmed().compare(QLatin1String("Style"), Qt::CaseInsensitive) == 0) {
                const QStringList parts = trimmed.section(QLatin1Char(':'), 1).split(QLatin1Char(','));
                // A valid V4+ style line has 23 fields (name + 22 properties)
                if (parts.size() >= 23) {
                    const QString styleName = parts.at(0).trimmed();
                    if (!styleName.isEmpty()) {
                        set.styles[styleName] = SubtitleStyle(trimmed);
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }
    for (const auto &pending : pendingAnimations) {
        auto styleIt = set.styles.find(pending.first);
        if (styleIt != set.styles.end()) {
            styleIt->second.setAnimation(SubtitleAnimation::fromString(pending.second));
        } else {
            // Unknown style: keep the line so that it is not lost
            set.extraData.append({QStringLiteral("Animation"), pending.first + QLatin1Char('|') + pending.second});
        }
    }
    for (const auto &pending : pendingAnimationsOut) {
        auto styleIt = set.styles.find(pending.first);
        if (styleIt != set.styles.end()) {
            styleIt->second.setAnimationOut(SubtitleAnimation::fromString(pending.second));
        } else {
            // Unknown style: keep the line so that it is not lost
            set.extraData.append({QStringLiteral("AnimationOut"), pending.first + QLatin1Char('|') + pending.second});
        }
    }
}

bool SubtitleStyleLibrary::writeSet(const StyleSet &set, const QString &path, QString *errorString) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorString) {
            *errorString = i18n("Cannot write to file %1.", path);
        }
        return false;
    }
    QTextStream out(&file);
    out << "[Script Info]\n";
    out << "; Kdenlive subtitle style library\n";
    out << "ScriptType: v4.00+\n";
    if (!set.name.isEmpty()) {
        out << "Title: " << set.name << '\n';
    }
    out << '\n';
    out << "[Kdenlive Extradata]\n";
    bool hasVersion = false;
    for (const auto &entry : set.extraData) {
        if (entry.first.compare(QLatin1String("LibraryVersion"), Qt::CaseInsensitive) == 0) {
            hasVersion = true;
        }
    }
    if (!hasVersion) {
        out << "LibraryVersion: 1\n";
    }
    for (const auto &entry : set.extraData) {
        out << entry.first << ": " << entry.second << '\n';
    }
    // Animations are stored separately from the style lines and regenerated on save
    for (const auto &entry : set.styles) {
        if (entry.second.animation().isEnabled()) {
            out << "Animation: " << entry.first << '|' << entry.second.animation().toString() << '\n';
        }
        if (entry.second.animationOut().isEnabled()) {
            out << "AnimationOut: " << entry.first << '|' << entry.second.animationOut().toString() << '\n';
        }
    }
    out << '\n';
    out << QStringLiteral("[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                          "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
                          "MarginR, MarginV, Encoding\n");
    for (const auto &entry : set.styles) {
        out << entry.second.toString(entry.first) << '\n';
    }
    out << '\n';
    out << "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    file.close();
    return true;
}

void SubtitleStyleLibrary::rebuildCaches()
{
    m_builtinStyles.clear();
    m_userStyles.clear();
    for (const StyleSet &set : std::as_const(m_sets)) {
        std::map<QString, SubtitleStyle> &target = set.builtIn ? m_builtinStyles : m_userStyles;
        for (const auto &entry : set.styles) {
            if (!set.builtIn && target.find(entry.first) != target.end()) {
                // The last user set defining a name wins here (see the loop order), silently shadowing
                // the earlier copy: warn so the duplicate can be cleaned up. This is the same set
                // saveStyle() edits, keeping editing and rendering consistent.
                qCWarning(KDENLIVE_LOG) << "Subtitle style" << entry.first << "is defined in more than one user set, using the one from" << set.name;
            }
            target[entry.first] = entry.second;
        }
    }
    m_sharedStyles = m_builtinStyles;
    for (const auto &entry : m_userStyles) {
        m_sharedStyles[entry.first] = entry.second;
    }
}

SubtitleStyleLibrary::StyleSet *SubtitleStyleLibrary::findSet(const QString &name)
{
    for (StyleSet &set : m_sets) {
        if (set.name == name) {
            return &set;
        }
    }
    return nullptr;
}

const SubtitleStyleLibrary::StyleSet *SubtitleStyleLibrary::findSet(const QString &name) const
{
    for (const StyleSet &set : m_sets) {
        if (set.name == name) {
            return &set;
        }
    }
    return nullptr;
}

SubtitleStyleLibrary::StyleSet &SubtitleStyleLibrary::ensureUserSet()
{
    for (StyleSet &set : m_sets) {
        if (!set.builtIn && set.name == s_userSetName) {
            return set;
        }
    }
    StyleSet set;
    set.name = s_userSetName;
    set.path = QDir(m_userDirectory).absoluteFilePath(s_userSetName + QStringLiteral(".ass"));
    set.builtIn = false;
    set.extraData.append({QStringLiteral("LibraryVersion"), QStringLiteral("1")});
    m_sets.append(set);
    return m_sets.last();
}

QStringList SubtitleStyleLibrary::styleSetNames() const
{
    QStringList names;
    for (const StyleSet &set : m_sets) {
        if (!names.contains(set.name)) {
            names.append(set.name);
        }
    }
    return names;
}

QStringList SubtitleStyleLibrary::styleNames(const QString &setName) const
{
    QStringList names;
    const StyleSet *set = findSet(setName);
    if (set) {
        for (const auto &entry : set->styles) {
            names.append(entry.first);
        }
    }
    return names;
}

QString SubtitleStyleLibrary::styleSetFor(const QString &styleName) const
{
    // When several user sets define the same name, rebuildCaches() (and therefore saveStyle()) keeps
    // the last one, so report that same set here.
    QString result;
    for (const StyleSet &set : m_sets) {
        if (!set.builtIn && set.styles.find(styleName) != set.styles.end()) {
            result = set.name;
        }
    }
    if (!result.isEmpty()) {
        return result;
    }
    for (const StyleSet &set : m_sets) {
        if (set.builtIn && set.styles.find(styleName) != set.styles.end()) {
            return set.name;
        }
    }
    return QString();
}

const std::map<QString, SubtitleStyle> &SubtitleStyleLibrary::sharedStyles() const
{
    return m_sharedStyles;
}

const std::map<QString, SubtitleStyle> &SubtitleStyleLibrary::builtinStyles() const
{
    return m_builtinStyles;
}

const std::map<QString, SubtitleStyle> &SubtitleStyleLibrary::userStyles() const
{
    return m_userStyles;
}

bool SubtitleStyleLibrary::hasStyle(const QString &styleName) const
{
    return m_sharedStyles.find(styleName) != m_sharedStyles.end();
}

bool SubtitleStyleLibrary::isBuiltinStyle(const QString &styleName) const
{
    return m_builtinStyles.find(styleName) != m_builtinStyles.end() && m_userStyles.find(styleName) == m_userStyles.end();
}

SubtitleStyle SubtitleStyleLibrary::style(const QString &styleName) const
{
    return style(styleName, nullptr);
}

SubtitleStyle SubtitleStyleLibrary::style(const QString &styleName, bool *found) const
{
    auto it = m_sharedStyles.find(styleName);
    if (it != m_sharedStyles.end()) {
        if (found) {
            *found = true;
        }
        return it->second;
    }
    if (found) {
        *found = false;
    }
    return SubtitleStyle();
}

bool SubtitleStyleLibrary::saveStyle(const QString &styleName, const SubtitleStyle &style, QString *errorString)
{
    if (styleName.isEmpty()) {
        if (errorString) {
            *errorString = i18n("Style name cannot be empty.");
        }
        return false;
    }
    // Update the user set that actually provides this style (the last one wins, see rebuildCaches()),
    // or create an override in the default user set. Updating the first matching set instead would
    // edit a shadowed copy that is never rendered.
    StyleSet *target = nullptr;
    for (StyleSet &set : m_sets) {
        if (!set.builtIn && set.styles.find(styleName) != set.styles.end()) {
            target = &set;
        }
    }
    if (!target) {
        if (!ensureUserDirectory()) {
            if (errorString) {
                *errorString = i18n("Cannot create the shared styles directory %1.", m_userDirectory);
            }
            return false;
        }
        target = &ensureUserSet();
    }
    target->styles[styleName] = style;
    if (!writeSet(*target, target->path, errorString)) {
        return false;
    }
    reload();
    Q_EMIT libraryChanged();
    return true;
}

bool SubtitleStyleLibrary::deleteStyle(const QString &styleName, QString *errorString)
{
    bool found = false;
    for (StyleSet &set : m_sets) {
        if (set.builtIn) {
            continue;
        }
        auto it = set.styles.find(styleName);
        if (it == set.styles.end()) {
            continue;
        }
        set.styles.erase(it);
        if (!writeSet(set, set.path, errorString)) {
            return false;
        }
        found = true;
    }
    if (!found) {
        if (errorString) {
            *errorString = i18n("The style %1 is built-in or does not exist.", styleName);
        }
        return false;
    }
    reload();
    Q_EMIT libraryChanged();
    return true;
}

bool SubtitleStyleLibrary::importSet(const QString &filePath, QString *errorString)
{
    StyleSet imported;
    imported.path = filePath;
    imported.name = QFileInfo(filePath).completeBaseName();
    imported.builtIn = false;
    loadSet(imported);
    if (imported.styles.empty()) {
        if (errorString) {
            *errorString = i18n("No subtitle style found in file %1.", filePath);
        }
        return false;
    }
    if (!ensureUserDirectory()) {
        if (errorString) {
            *errorString = i18n("Cannot create the shared styles directory %1.", m_userDirectory);
        }
        return false;
    }
    const QString source = QFileInfo(filePath).absoluteFilePath();
    const QString baseName = QFileInfo(filePath).completeBaseName();
    QString targetPath = QDir(m_userDirectory).absoluteFilePath(baseName + QStringLiteral(".ass"));
    int suffix = 1;
    while (QFileInfo(targetPath).absoluteFilePath() != source && QFile::exists(targetPath)) {
        targetPath = QDir(m_userDirectory).absoluteFilePath(baseName + QStringLiteral(" (%1).ass").arg(suffix++));
    }
    if (QFileInfo(targetPath).absoluteFilePath() != source) {
        if (!QFile::copy(filePath, targetPath)) {
            if (errorString) {
                *errorString = i18n("Cannot copy %1 to %2.", filePath, targetPath);
            }
            return false;
        }
    }
    reload();
    Q_EMIT libraryChanged();
    return true;
}

bool SubtitleStyleLibrary::exportSet(const QString &setName, const QString &filePath, QString *errorString) const
{
    const StyleSet *set = findSet(setName);
    if (!set) {
        if (errorString) {
            *errorString = i18n("Style set %1 not found.", setName);
        }
        return false;
    }
    return writeSet(*set, filePath, errorString);
}

bool SubtitleStyleLibrary::exportStyles(const QString &filePath, const std::map<QString, SubtitleStyle> &styles, QString *errorString) const
{
    if (styles.empty()) {
        if (errorString) {
            *errorString = i18n("No style to export.");
        }
        return false;
    }
    StyleSet set;
    set.name = QFileInfo(filePath).completeBaseName();
    set.path = filePath;
    set.extraData.append({QStringLiteral("LibraryVersion"), QStringLiteral("1")});
    set.styles = styles;
    return writeSet(set, filePath, errorString);
}

const QString &SubtitleStyleLibrary::userDirectory() const
{
    return m_userDirectory;
}

bool SubtitleStyleLibrary::ensureUserDirectory() const
{
    if (m_userDirectory.isEmpty()) {
        return false;
    }
    QDir dir(m_userDirectory);
    return dir.exists() || dir.mkpath(QStringLiteral("."));
}

QString SubtitleStyleLibrary::defaultUserDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/subtitle-styles");
}

QStringList SubtitleStyleLibrary::defaultBuiltinDirectories()
{
    QStringList directories =
        QStandardPaths::locateAll(QStandardPaths::AppLocalDataLocation, QStringLiteral("subtitle-styles/"), QStandardPaths::LocateDirectory);
    const QString userPath = QDir(defaultUserDirectory()).absolutePath();
    for (int i = directories.size() - 1; i >= 0; --i) {
        if (QDir(directories.at(i)).absolutePath() == userPath) {
            directories.removeAt(i);
        }
    }
    directories.removeDuplicates();
    return directories;
}
