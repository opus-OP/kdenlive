/*
    SPDX-FileCopyrightText: 2023 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "bin/model/subtitlestylelibrary.h"
#include "definitions.h"
#include "ui_managesubtitles_ui.h"

class SubtitleModel;
class TimelineController;

namespace KNSWidgets {
class Action;
}

class SideBarDropFilter : public QObject
{
    Q_OBJECT
public:
    explicit SideBarDropFilter(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
Q_SIGNALS:
    void drop(QObject *obj, QDropEvent *event);
};

/**
 * @class ManageSubtitles
 * @brief A dialog for managing project subtitles.
 * @author Jean-Baptiste Mardelle
 */
class ManageSubtitles : public QDialog, public Ui::ManageSubtitles_UI
{
    Q_OBJECT

public:
    explicit ManageSubtitles(std::shared_ptr<SubtitleModel> model, TimelineController *controller, int currentIx, QWidget *parent = nullptr);
    ~ManageSubtitles() override;

private:
    std::shared_ptr<SubtitleModel> m_model;
    TimelineController *m_controller;
    void parseFileList();
    void parseEventList();
    void parseStyleList(SubtitleStyleScope scope);
    void parseInfoList();
    void parseSideBar();

    /** @brief Scope of the currently selected style entry in the sidebar */
    SubtitleStyleScope currentStyleScope() const;
    /** @brief Name of the currently selected style (empty if none) */
    QString currentStyleName() const;
    /** @brief Names available in the given style scope */
    QStringList namesForScope(SubtitleStyleScope scope) const;
    /** @brief Style with the given name in the given scope */
    SubtitleStyle styleForScope(const QString &name, SubtitleStyleScope scope) const;
    /** @brief Store a style in the given scope */
    void applyStyle(const QString &name, const SubtitleStyle &style, SubtitleStyleScope scope);
    /** @brief Delete a style from the given scope, optionally asking for confirmation */
    bool deleteStyleInScope(const QString &name, SubtitleStyleScope scope, bool confirm);
    /** @brief Enable/disable the style buttons according to the current selection */
    void updateStyleButtons();
    /** @brief Select the sidebar entry matching the given role (-1 project, -2 shared) */
    void selectSideBarScope(int role);
    /** @brief Select the style with the given name in the style list */
    void selectStyleByName(const QString &name);

    /** @brief Add a style in the given scope */
    void addStyle(SubtitleStyleScope scope);
    /** @brief Delete the selected style from the given scope */
    void deleteStyle(SubtitleStyleScope scope);
    /** @brief Duplicate the selected style in the given scope */
    void duplicateStyle(SubtitleStyleScope scope);
    /** @brief Edit the selected style in the given scope */
    void editStyle(SubtitleStyleScope scope);
    /** @brief Copy the selected style to the given scope */
    void copyStyleToScope(SubtitleStyleScope scope);
    /** @brief Show the context menu of the style list */
    void showStyleMenu(const QPoint &pos);
    /** @brief Import a style set file into the shared library */
    void importStyleSet();
    /** @brief Export the current style set to a file */
    void exportStyleSet();

    int m_activeSubFile{-1};
    /** @brief KNewStuff action used to download shared style sets from the store */
    KNSWidgets::Action *m_downloadStyleAction{nullptr};

private Q_SLOTS:
    void updateSubtitle(QTreeWidgetItem *item, int column);
    void addSubtitleFile(const QString name = QString());
    void duplicateFile();
    void deleteFile();
    void addLayer();
    void deleteLayer();
    void deleteEvent();
    void duplicateLayer();
    void importSubtitleFile();
};
