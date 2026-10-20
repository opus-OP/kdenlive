/*
    SPDX-FileCopyrightText: 2023 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "managesubtitles.h"
#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "dialogs/importsubtitle.h"
#include "doc/kdenlivedoc.h"
#include "klocalizedstring.h"
#include "subtitlestyleedit.h"
#include "timeline2/view/timelinecontroller.h"
#include <KMessageBox>
#include <KNSCore/Entry>
#include <KNSWidgets/Action>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QListWidget>
#include <QMenu>
#include <QTableWidget>
#include <qinputdialog.h>
#include <qmimedata.h>

namespace {
/** @brief Convert a style sidebar role to a style scope (non-negative roles are subtitle files) */
SubtitleStyleScope scopeForRole(int role)
{
    if (role == -1) {
        return SubtitleStyleScope::Project;
    }
    if (role == -2) {
        return SubtitleStyleScope::Shared;
    }
    return SubtitleStyleScope::SubtitleFile;
}

/** @brief Return a name that is not part of @p existing, appending a numeric suffix if needed */
QString uniqueStyleName(const QString &baseName, const QStringList &existing)
{
    if (!existing.contains(baseName)) {
        return baseName;
    }
    int suffix = 1;
    while (existing.contains(baseName + QLatin1Char(' ') + QString::number(suffix))) {
        suffix++;
    }
    return baseName + QLatin1Char(' ') + QString::number(suffix);
}

/** @brief Human readable description of a style animation (empty when the animation is disabled)
 *
 * @p isOut selects the exit wording: a slide then goes to the given edge and the per character
 * animation is the erasing one.
 */
QString styleAnimationLabel(const SubtitleAnimation &animation, bool isOut)
{
    switch (animation.kind) {
    case SubtitleAnimationKind::Fade:
        return i18n("Fade");
    case SubtitleAnimationKind::Move:
        switch (animation.direction) {
        case 1:
            return isOut ? i18n("Slide to top") : i18n("Slide from top");
        case 2:
            return isOut ? i18n("Slide to left") : i18n("Slide from left");
        case 3:
            return isOut ? i18n("Slide to right") : i18n("Slide from right");
        case 0:
        default:
            return isOut ? i18n("Slide to bottom") : i18n("Slide from bottom");
        }
    case SubtitleAnimationKind::Scale:
        return animation.intensity >= 1.0 ? i18n("Zoom in") : i18n("Zoom out");
    case SubtitleAnimationKind::Rotate:
        return i18n("Rotate");
    case SubtitleAnimationKind::Typewriter:
        return isOut ? QString() : i18n("Typewriter");
    case SubtitleAnimationKind::Erase:
        return isOut ? i18n("Erase") : QString();
    case SubtitleAnimationKind::None:
    default:
        return QString();
    }
}
} // namespace

SideBarDropFilter::SideBarDropFilter(QObject *parent)
    : QObject(parent)
{
}

bool SideBarDropFilter::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::DragEnter) {
        QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent *>(event);
        dragEvent->acceptProposedAction();
    }
    if (event->type() == QEvent::Drop) {
        QDropEvent *dropEvent = static_cast<QDropEvent *>(event);
        Q_EMIT drop(obj, dropEvent);
        return true;
    }
    return QObject::eventFilter(obj, event);
}

ManageSubtitles::ManageSubtitles(std::shared_ptr<SubtitleModel> model, TimelineController *controller, int currentIx, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
    , m_controller(controller)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(i18nc("@title:window", "Manage Subtitles"));
    Qt::WindowFlags flags = Qt::Dialog;
    flags |= Qt::WindowMaximizeButtonHint;
    flags |= Qt::WindowCloseButtonHint;
    setWindowFlags(flags);
    messageWidget->hide();

    m_activeSubFile = currentIx;

    parseFileList();
    parseEventList();
    parseStyleList(SubtitleStyleScope::SubtitleFile);
    parseInfoList();
    // parseSideBar();

    SideBarDropFilter *dropFilter = new SideBarDropFilter(this);
    eventFileSideBar->viewport()->installEventFilter(dropFilter);
    styleFileSideBar->viewport()->installEventFilter(dropFilter);

    connect(dropFilter, &SideBarDropFilter::drop, this, [&](QObject *obj, QDropEvent *event) {
        if (obj->parent() == eventFileSideBar) {
            if (!eventList->currentItem()->text(0).contains(i18n("Layer"))) {
                return;
            }
            int ix = event->position().y() / eventFileSideBar->sizeHintForRow(0);
            if (ix > eventFileSideBar->count() - 1) {
                return;
            }
            int id = eventFileSideBar->item(ix)->data(Qt::UserRole).toInt();

            auto copyMoveLayer = [this, id](bool isMove) {
                int currentIx = -1;
                int newIx = -1;
                QStringList subKeys;
                const QMap<std::pair<int, QString>, QString> subs = m_model->getSubtitlesList();
                for (auto sub = subs.cbegin(), end = subs.cend(); sub != end; ++sub) {
                    subKeys.append(QString::number(sub.key().first) + " - " + sub.key().second);
                    if (sub.key().first == m_activeSubFile) {
                        currentIx = subKeys.size() - 1;
                    }
                    if (sub.key().first == id) {
                        newIx = subKeys.size() - 1;
                    }
                }

                bool ok;
                QString sub;
                if (isMove) {
                    sub = QInputDialog::getItem(this, i18n("Move Layer"), i18n("Select the subtitle file to move the layer to:"), subKeys, newIx, false, &ok);
                } else {
                    sub = QInputDialog::getItem(this, i18n("Copy Layer"), i18n("Select the subtitle file to copy the layer to:"), subKeys, newIx, false, &ok);
                }
                if (ok) {
                    // events
                    int layer = eventList->currentItem()->text(0).section(" ", 1, 1).toInt();
                    QList<std::pair<std::pair<int, GenTime>, SubtitleEvent>> events;
                    for (auto &event : m_model->getAllSubtitles()) {
                        if (event.first.first == layer) {
                            events.append(event);
                        }
                    }

                    if (isMove) {
                        m_model->requestDeleteLayer(layer);
                    }
                    m_controller->subtitlesMenuActivated(newIx);
                    m_model->requestCreateLayer();
                    int newLayer = m_model->getMaxLayer();
                    for (auto &event : events) {
                        // TODO: undo/redo
                        Fun undo = []() { return true; };
                        Fun redo = []() { return true; };
                        event.first.first = newLayer;
                        m_model->addSubtitle(event.first, event.second, undo, redo);
                    }
                    m_controller->subtitlesMenuActivated(currentIx);
                    parseEventList();
                }
            };

            QAction *actionMoveLayer = new QAction(i18n("Move To..."), this);
            actionMoveLayer->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
            connect(actionMoveLayer, &QAction::triggered, this, [function = copyMoveLayer]() { function(true); });
            QAction *actionCopyLayer = new QAction(i18n("Copy To..."), this);
            actionCopyLayer->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
            connect(actionCopyLayer, &QAction::triggered, this, [function = copyMoveLayer]() { function(false); });
            QMenu *layerMenu = new QMenu(this);
            layerMenu->addAction(actionCopyLayer);
            if (eventList->topLevelItemCount() > 1) {
                layerMenu->addAction(actionMoveLayer);
            }
            layerMenu->exec(eventFileSideBar->mapToGlobal(event->position()).toPoint());
        } else if (obj->parent() == styleFileSideBar) {
            if (styleFileSideBar->currentItem() == nullptr || styleList->currentItem() == nullptr) {
                return;
            }
            const int ix = event->position().y() / styleFileSideBar->sizeHintForRow(0);
            if (ix < 0 || ix > styleFileSideBar->count() - 1) {
                return;
            }
            const int fromRole = styleFileSideBar->currentItem()->data(Qt::UserRole).toInt();
            const int toRole = styleFileSideBar->item(ix)->data(Qt::UserRole).toInt();
            const SubtitleStyleScope fromScope = scopeForRole(fromRole);
            const SubtitleStyleScope toScope = scopeForRole(toRole);
            const bool sameContainer = (fromScope == toScope && fromRole == toRole);

            auto copyMoveStyle = [this, fromScope, toScope, fromRole, toRole](bool isMove) {
                const QString styleName = currentStyleName();
                if (styleName.isEmpty()) {
                    return;
                }
                if (isMove && fromScope == toScope && fromRole == toRole) {
                    return;
                }
                const SubtitleStyle style = styleForScope(styleName, fromScope);
                const QString newName = uniqueStyleName(styleName, namesForScope(toScope));
                // A style that cannot be removed from its source (built-in or default style) must not be
                // "moved": abort instead of silently turning the move into a copy.
                if (isMove && !deleteStyleInScope(styleName, fromScope, false)) {
                    return;
                }
                if (toScope == SubtitleStyleScope::SubtitleFile) {
                    // Activate the target subtitle file so the style is written to it
                    const QMap<std::pair<int, QString>, QString> subs = m_model->getSubtitlesList();
                    int comboIx = 0;
                    for (auto sub = subs.cbegin(); sub != subs.cend(); ++sub, ++comboIx) {
                        if (sub.key().first == toRole) {
                            m_controller->subtitlesMenuActivated(comboIx);
                            break;
                        }
                    }
                } else if (toScope == SubtitleStyleScope::Project) {
                    selectSideBarScope(-1);
                } else {
                    selectSideBarScope(-2);
                }
                applyStyle(newName, style, toScope);
                parseStyleList(toScope);
                selectStyleByName(newName);
            };

            QAction *actionMoveStyle = new QAction(i18n("Move To..."), this);
            actionMoveStyle->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
            connect(actionMoveStyle, &QAction::triggered, this, [function = copyMoveStyle]() { function(true); });
            QAction *actionCopyStyle = new QAction(i18n("Copy To..."), this);
            actionCopyStyle->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
            connect(actionCopyStyle, &QAction::triggered, this, [function = copyMoveStyle]() { function(false); });
            QMenu *styleMenu = new QMenu(this);
            styleMenu->addAction(actionCopyStyle);
            const QString draggedStyle = currentStyleName();
            const SubtitleStyleLibrary *library = m_model->styleLibrary();
            const bool movable = !(fromScope == SubtitleStyleScope::Shared && library && library->isBuiltinStyle(draggedStyle)) &&
                                 !(fromScope == SubtitleStyleScope::SubtitleFile && draggedStyle == QLatin1String("Default"));
            if (!sameContainer && movable) {
                styleMenu->addAction(actionMoveStyle);
            }
            styleMenu->exec(styleFileSideBar->mapToGlobal(event->position()).toPoint());
        }
    });

    connect(fileList, &QTreeWidget::itemChanged, this, &ManageSubtitles::updateSubtitle);
    connect(fileList, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current, QTreeWidgetItem *previous) {
        QSignalBlocker bk(eventFileSideBar);
        QSignalBlocker bk2(styleFileSideBar);
        QSignalBlocker bk3(infoFileSideBar);
        buttonDeleteFile->setEnabled(!(fileList->topLevelItemCount() == 1));
        int id = current->data(0, Qt::UserRole).toInt();
        if (id > -1) {
            int ix = fileList->indexOfTopLevelItem(current);
            m_controller->subtitlesMenuActivated(ix);

            m_activeSubFile = id;
            parseEventList();
            parseStyleList(SubtitleStyleScope::SubtitleFile);
            parseInfoList();
            eventFileSideBar->setCurrentRow(fileList->indexOfTopLevelItem(current));
            styleFileSideBar->setCurrentRow(fileList->indexOfTopLevelItem(current));
            infoFileSideBar->setCurrentRow(fileList->indexOfTopLevelItem(current));
        } else {
            QSignalBlocker bk4(fileList);
            fileList->setCurrentItem(previous);
        }
    });
    connect(eventList, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *current, int column) {
        if (current->parent() != nullptr) {
            return;
        }
        if (column != 2) {
            return;
        }

        int layer = current->text(0).section(" ", 1, 1).toInt();
        QMenu *menu = new QMenu(this);
        for (const QString &styleName : m_model->availableSubtitleStyleNames()) {
            QAction *styleAction = menu->addAction(styleName);
            styleAction->setToolTip(styleName);
        }
        QAction *action = menu->exec(QCursor::pos());
        if (action) {
            QString styleName = action->toolTip();
            m_model->setLayerDefaultStyle(layer, styleName);
            current->setText(2, styleName);
        }
    });
    connect(styleList, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *, QTreeWidgetItem *) { updateStyleButtons(); });
    connect(eventList, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        buttonDeleteLayer->setEnabled(current->parent() != nullptr || eventList->topLevelItemCount() > 1);
    });
    connect(eventFileSideBar, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *previous) {
        QSignalBlocker bk(fileList);
        QSignalBlocker bk2(styleFileSideBar);
        QSignalBlocker bk3(infoFileSideBar);
        buttonDeleteLayer->setEnabled(!(eventList->topLevelItemCount() == 1));
        int id = current->data(Qt::UserRole).toInt();
        if (id > -1) {
            int ix = eventFileSideBar->row(current);
            m_controller->subtitlesMenuActivated(ix);

            m_activeSubFile = id;
            parseEventList();
            parseStyleList(SubtitleStyleScope::SubtitleFile);
            parseInfoList();
            fileList->setCurrentItem(fileList->topLevelItem(eventFileSideBar->currentRow()));
            styleFileSideBar->setCurrentRow(eventFileSideBar->currentRow());
            infoFileSideBar->setCurrentRow(eventFileSideBar->currentRow());
        } else {
            QSignalBlocker bk4(eventFileSideBar);
            eventFileSideBar->setCurrentItem(previous);
        }
    });
    connect(styleFileSideBar, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *previous) {
        QSignalBlocker bk(fileList);
        QSignalBlocker bk2(eventFileSideBar);
        QSignalBlocker bk3(infoFileSideBar);
        const int role = current->data(Qt::UserRole).toInt();
        if (role == -1) {
            parseStyleList(SubtitleStyleScope::Project);
        } else if (role == -2) {
            parseStyleList(SubtitleStyleScope::Shared);
        } else if (role > -1) {
            m_activeSubFile = role;
            const int ix = styleFileSideBar->row(current);
            m_controller->subtitlesMenuActivated(ix);

            parseEventList();
            parseStyleList(SubtitleStyleScope::SubtitleFile);
            parseInfoList();
            fileList->setCurrentItem(fileList->topLevelItem(styleFileSideBar->currentRow()));
            eventFileSideBar->setCurrentRow(styleFileSideBar->currentRow());
            infoFileSideBar->setCurrentRow(styleFileSideBar->currentRow());
        } else {
            QSignalBlocker bk4(styleFileSideBar);
            styleFileSideBar->setCurrentItem(previous);
        }
    });
    connect(infoFileSideBar, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *previous) {
        QSignalBlocker bk(fileList);
        QSignalBlocker bk2(eventFileSideBar);
        QSignalBlocker bk3(styleFileSideBar);
        int id = current->data(Qt::UserRole).toInt();
        if (id > -1) {
            int ix = infoFileSideBar->row(current);
            m_controller->subtitlesMenuActivated(ix);

            m_activeSubFile = id;
            parseEventList();
            parseStyleList(SubtitleStyleScope::SubtitleFile);
            parseInfoList();
            fileList->setCurrentItem(fileList->topLevelItem(infoFileSideBar->currentRow()));
            eventFileSideBar->setCurrentRow(infoFileSideBar->currentRow());
            styleFileSideBar->setCurrentRow(infoFileSideBar->currentRow());
        } else {
            QSignalBlocker bk4(infoFileSideBar);
            infoFileSideBar->setCurrentItem(previous);
        }
    });
    connect(buttonNewFile, &QPushButton::clicked, this, [this]() { addSubtitleFile(); });
    connect(buttonDuplicateFile, &QPushButton::clicked, this, &ManageSubtitles::duplicateFile);
    connect(buttonDeleteFile, &QPushButton::clicked, this, &ManageSubtitles::deleteFile);
    connect(buttonNewLayer, &QPushButton::clicked, this, &ManageSubtitles::addLayer);
    connect(buttonDuplicateLayer, &QPushButton::clicked, this, &ManageSubtitles::duplicateLayer);
    connect(buttonDeleteLayer, &QPushButton::clicked, this, &ManageSubtitles::deleteLayer);
    connect(buttonNewStyle, &QPushButton::clicked, this, [this]() { addStyle(currentStyleScope()); });

    connect(buttonDuplicateStyle, &QPushButton::clicked, this, [this]() { duplicateStyle(currentStyleScope()); });
    connect(buttonDeleteStyle, &QPushButton::clicked, this, [this]() { deleteStyle(currentStyleScope()); });
    connect(buttonEditStyle, &QPushButton::clicked, this, [this]() { editStyle(currentStyleScope()); });

    styleList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(styleList, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showStyleMenu(pos); });

    // Import/Export menu
    QMenu *menu = new QMenu(this);
    QAction *importSub = new QAction(QIcon::fromTheme(QStringLiteral("document-import")), i18nc("@action:inmenu", "Import Subtitle"), this);
    QAction *exportSub = new QAction(QIcon::fromTheme(QStringLiteral("document-export")), i18nc("@action:inmenu", "Export Subtitle"), this);
    menu->addAction(importSub);
    menu->addAction(exportSub);
    connect(importSub, &QAction::triggered, this, &ManageSubtitles::importSubtitleFile);
    connect(exportSub, &QAction::triggered, m_controller, &TimelineController::exportSubtitle);
    buttonMenuFile->setMenu(menu);

    // Download new shared (cross-project) style sets from the KDE store. The action
    // hides itself when KNewStuff is not available (KAuthorized::GHNS).
    m_downloadStyleAction = new KNSWidgets::Action(i18n("Download New Subtitle Styles…"), QStringLiteral(":data/kdenlive_subtitle_styles.knsrc"), this);
    connect(m_downloadStyleAction, &KNSWidgets::Action::dialogFinished, this, [this](const QList<KNSCore::Entry> &changedEntries) {
        if (changedEntries.isEmpty() || !m_model) {
            return;
        }
        SubtitleStyleLibrary *library = m_model->styleLibrary();
        if (!library) {
            return;
        }
        // Pick up the newly installed (or removed) sets and refresh the visible list.
        // Mirrors the library's own saveStyle()/importSet(): reload() does not notify on
        // its own, so emit libraryChanged (wired to SubtitleModel::modelChanged) to have
        // the .ass regenerated and the newly downloaded styles usable right away.
        library->reload();
        Q_EMIT library->libraryChanged();
        parseStyleList(currentStyleScope());
    });
}

ManageSubtitles::~ManageSubtitles()
{
    QSignalBlocker bk(fileList);
    QSignalBlocker bk2(eventFileSideBar);
    QSignalBlocker bk3(styleFileSideBar);
    QSignalBlocker bk4(eventList);
    QSignalBlocker bk5(styleList);
}

void ManageSubtitles::parseFileList()
{
    QSignalBlocker bk(fileList);
    fileList->clear();
    QMap<std::pair<int, QString>, QString> subs = m_model->getSubtitlesList();
    QMapIterator<std::pair<int, QString>, QString> i(subs);
    QTreeWidgetItem *currentItem = nullptr;
    while (i.hasNext()) {
        i.next();

        // set subtitle
        QTreeWidgetItem *item = new QTreeWidgetItem(fileList, {i.key().second, i.value()});
        item->setData(0, Qt::UserRole, i.key().first);

        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (m_activeSubFile > -1 && m_activeSubFile == i.key().first) {
            currentItem = item;
        }
    }
    if (currentItem) {
        fileList->setCurrentItem(currentItem);
    }
    buttonDeleteFile->setEnabled(fileList->topLevelItemCount() > 1);
    parseSideBar();
}

void ManageSubtitles::parseEventList()
{
    QSignalBlocker bk(eventList);
    eventList->clear();
    // m_model->activateSubtitle(i.key().first);
    const int maxLayer = m_model->getMaxLayer();
    const QList<std::pair<std::pair<int, GenTime>, SubtitleEvent>> allSubs = m_model->getAllSubtitles();

    // add layer items
    for (int i = 0; i <= maxLayer; i++) {
        new QTreeWidgetItem(eventList,
                            {i18n("Layer ") + QString::number(i), i18n("with ") + QString::number(0) + i18n(" event"), m_model->getLayerDefaultStyle(i)});
    }

    // add subtitle events
    for (const auto &sub : allSubs) {
        int count = eventList->topLevelItem(sub.first.first)->text(1).section(" ", 1, 1).toInt() + 1;
        eventList->topLevelItem(sub.first.first)->setText(1, "with " + QString::number(count) + (count == 1 ? i18n(" event") : i18n(" events")));
        auto *item = new QTreeWidgetItem(eventList->topLevelItem(sub.first.first),
                                         {sub.first.second.toString(), sub.second.endTime().toString(), sub.second.styleName(), sub.second.text()});
        int sid = m_model->getIdForStartPos(sub.first.first, sub.first.second);
        item->setData(0, Qt::UserRole, sid);
    }

    eventList->setCurrentItem(eventList->topLevelItem(0));
    buttonDeleteLayer->setEnabled(eventList->topLevelItemCount() > 1);
}

void ManageSubtitles::parseStyleList(SubtitleStyleScope scope)
{
    QSignalBlocker bk(styleList);
    styleList->clear();

    std::map<QString, SubtitleStyle> styles;
    SubtitleStyleLibrary *library = m_model->styleLibrary();
    switch (scope) {
    case SubtitleStyleScope::Project:
        styles = m_model->getAllSubtitleStyles(true);
        break;
    case SubtitleStyleScope::Shared:
        if (library) {
            styles = library->sharedStyles();
        }
        break;
    case SubtitleStyleScope::SubtitleFile:
    default:
        styles = m_model->getAllSubtitleStyles(false);
        break;
    }

    for (const auto &style : styles) {
        const QString name = style.first;
        QString displayName = name;
        QString toolTip = i18n("Style name: %1", name);
        if (scope == SubtitleStyleScope::Project && m_model->isProjectStyleOverridingShared(name)) {
            displayName = i18n("%1 (overrides shared style)", name);
            toolTip = i18n("This project style overrides a shared style with the same name.");
        } else if (scope == SubtitleStyleScope::Shared && library && library->isBuiltinStyle(name)) {
            displayName = i18n("%1 (built-in)", name);
            toolTip = i18n("This style is provided by a read-only built-in style set.");
        }
        QStringList animationLabels;
        const QString animationIn = styleAnimationLabel(style.second.animation(), false);
        const QString animationOut = styleAnimationLabel(style.second.animationOut(), true);
        if (!animationIn.isEmpty()) {
            animationLabels << animationIn;
        }
        if (!animationOut.isEmpty()) {
            animationLabels << animationOut;
        }
        if (!animationLabels.isEmpty()) {
            const QString animationLabel = animationLabels.join(QStringLiteral(" + "));
            displayName = i18n("%1 [%2]", displayName, animationLabel);
            toolTip = i18n("%1 Animation: %2.", toolTip, animationLabel);
        }
        QTreeWidgetItem *styleItem = new QTreeWidgetItem(styleList, {displayName,
                                                                     style.second.fontName(),
                                                                     QString::number(style.second.fontSize()),
                                                                     style.second.bold() ? "Yes" : "No",
                                                                     style.second.italic() ? "Yes" : "No",
                                                                     style.second.underline() ? "Yes" : "No",
                                                                     style.second.strikeOut() ? "Yes" : "No",
                                                                     "",
                                                                     "",
                                                                     "",
                                                                     "",
                                                                     QString::number(style.second.outline()),
                                                                     QString::number(style.second.shadow()),
                                                                     "",
                                                                     QString::number(style.second.scaleX()),
                                                                     QString::number(style.second.scaleY()),
                                                                     QString::number(style.second.spacing()),
                                                                     QString::number(style.second.angle()),
                                                                     "",
                                                                     QString::number(style.second.marginL()),
                                                                     QString::number(style.second.marginR()),
                                                                     QString::number(style.second.marginV()),
                                                                     QString::number(style.second.encoding())});
        styleItem->setData(0, Qt::UserRole, name);
        styleItem->setToolTip(0, toolTip);
        QLabel *labelPrimary = new QLabel();
        labelPrimary->setStyleSheet(QStringLiteral("background-color: %1").arg(style.second.primaryColour().name()));
        styleList->setItemWidget(styleItem, 7, labelPrimary);
        QLabel *labelSecondary = new QLabel();
        labelSecondary->setStyleSheet(QStringLiteral("background-color: %1").arg(style.second.secondaryColour().name()));
        styleList->setItemWidget(styleItem, 8, labelSecondary);
        QLabel *labelOutline = new QLabel();
        labelOutline->setStyleSheet(QStringLiteral("background-color: %1").arg(style.second.outlineColour().name()));
        styleList->setItemWidget(styleItem, 9, labelOutline);
        QLabel *labelBack = new QLabel();
        labelBack->setStyleSheet(QStringLiteral("background-color: %1").arg(style.second.backColour().name()));
        styleList->setItemWidget(styleItem, 10, labelBack);
        switch (style.second.alignment()) {
        case 1:
            styleItem->setText(13, i18n("Bottom Left"));
            break;
        case 2:
            styleItem->setText(13, i18n("Bottom"));
            break;
        case 3:
            styleItem->setText(13, i18n("Bottom Right"));
            break;
        case 4:
            styleItem->setText(13, i18n("Left"));
            break;
        case 5:
            styleItem->setText(13, i18n("Center"));
            break;
        case 6:
            styleItem->setText(13, i18n("Right"));
            break;
        case 7:
            styleItem->setText(13, i18n("Top Left"));
            break;
        case 8:
            styleItem->setText(13, i18n("Top"));
            break;
        case 9:
            styleItem->setText(13, i18n("Top Right"));
            break;
        }

        switch (style.second.borderStyle()) {
        case 1:
            styleItem->setText(18, i18n("Default"));
            break;
        case 3:
            styleItem->setText(18, i18n("Box as Border"));
            break;
        case 4:
            styleItem->setText(18, i18n("Box as Shadow"));
            break;
        }
    }
    styleList->setCurrentItem(styleList->topLevelItem(0));
    updateStyleButtons();
}

void ManageSubtitles::parseInfoList()
{
    QSignalBlocker bk(infoList);
    infoList->clear();
    std::map<QString, QString> info = m_model->scriptInfo();
    for (const auto &i : info) {
        new QTreeWidgetItem(infoList, {i.first, i.second});
    }
}

void ManageSubtitles::parseSideBar()
{
    QSignalBlocker bk1(eventFileSideBar);
    QSignalBlocker bk2(styleFileSideBar);
    QSignalBlocker bk3(infoFileSideBar);
    eventFileSideBar->clear();
    styleFileSideBar->clear();
    infoFileSideBar->clear();
    for (int topLevelIndex = 0; topLevelIndex < fileList->topLevelItemCount(); topLevelIndex++) {
        QTreeWidgetItem *topLevelItem = fileList->topLevelItem(topLevelIndex);
        QListWidgetItem *newItem = new QListWidgetItem(eventFileSideBar);
        newItem->setText(topLevelItem->text(0));
        newItem->setData(Qt::UserRole, topLevelItem->data(0, Qt::UserRole));

        newItem = new QListWidgetItem(styleFileSideBar);
        newItem->setText(topLevelItem->text(0));
        newItem->setData(Qt::UserRole, topLevelItem->data(0, Qt::UserRole));

        newItem = new QListWidgetItem(infoFileSideBar);
        newItem->setText(topLevelItem->text(0));
        newItem->setData(Qt::UserRole, topLevelItem->data(0, Qt::UserRole));
    }
    QListWidgetItem *newItem = new QListWidgetItem(styleFileSideBar);
    newItem->setText(i18n("Project"));
    QFont font = newItem->font();
    font.setBold(true);
    newItem->setData(Qt::FontRole, font);
    newItem->setData(Qt::UserRole, -1);

    newItem = new QListWidgetItem(styleFileSideBar);
    newItem->setText(i18n("Shared"));
    font = newItem->font();
    font.setBold(true);
    newItem->setData(Qt::FontRole, font);
    newItem->setData(Qt::UserRole, -2);

    eventFileSideBar->setCurrentRow(fileList->indexOfTopLevelItem(fileList->currentItem()));
    styleFileSideBar->setCurrentRow(fileList->indexOfTopLevelItem(fileList->currentItem()));
    infoFileSideBar->setCurrentRow(fileList->indexOfTopLevelItem(fileList->currentItem()));
}

void ManageSubtitles::updateSubtitle(QTreeWidgetItem *item, int column)
{
    if (item->parent() == nullptr) {
        if (column == 0) {
            // An item was renamed
            m_model->updateModelName(item->data(0, Qt::UserRole).toInt(), item->text(0));
            m_controller->refreshSubtitlesComboIndex();
        }
        if (column == 1) {
            item->setText(1, m_model->getSubtitlesList()[{item->data(0, Qt::UserRole).toInt(), item->text(0)}]);
        }
    }
}

void ManageSubtitles::addSubtitleFile(const QString name)
{
    m_model->createNewSubtitle(name);
    parseFileList();
    // Makes last item active
    fileList->setCurrentItem(fileList->topLevelItem(fileList->topLevelItemCount() - 1));
}

void ManageSubtitles::addLayer()
{
    m_model->requestCreateLayer();
    parseEventList();
    eventList->setCurrentItem(eventList->topLevelItem(eventList->topLevelItemCount() - 1));
}

void ManageSubtitles::addStyle(SubtitleStyleScope scope)
{
    bool ok;
    QString styleName;
    SubtitleStyle style = SubtitleStyleEdit::getStyle(this, SubtitleStyle(), styleName, m_model, scope, &ok);
    if (ok) {
        applyStyle(styleName, style, scope);
        parseStyleList(scope);
        selectStyleByName(styleName);
    }
}

void ManageSubtitles::duplicateFile()
{
    m_model->createNewSubtitle(QString(), fileList->currentItem()->data(0, Qt::UserRole).toInt());
    parseFileList();
    // Makes last item active
    fileList->setCurrentItem(fileList->topLevelItem(fileList->topLevelItemCount() - 1));
}

void ManageSubtitles::duplicateLayer()
{
    QTreeWidgetItem *current = eventList->currentItem();
    while (current->parent() != nullptr) {
        current = current->parent();
    }
    m_model->requestCreateLayer(current->text(0).section(" ", 1, 1).toInt());
    parseEventList();
    eventList->setCurrentItem(eventList->topLevelItem(eventList->topLevelItemCount() - 1));
}

void ManageSubtitles::duplicateStyle(SubtitleStyleScope scope)
{
    const QString sourceName = currentStyleName();
    if (sourceName.isEmpty()) {
        return;
    }
    const SubtitleStyle style = styleForScope(sourceName, scope);
    QString styleName;
    int suffix = 1;
    const QStringList existingStyleNames = namesForScope(scope);
    while (true) {
        styleName = QStringLiteral("Style %1").arg(suffix);
        if (!existingStyleNames.contains(styleName)) {
            break;
        }
        suffix++;
    }
    applyStyle(styleName, style, scope);
    parseStyleList(scope);
    selectStyleByName(styleName);
}

void ManageSubtitles::deleteFile()
{
    const QString name = fileList->currentItem()->text(0);
    const QString path = fileList->currentItem()->text(1);
    if (KMessageBox::warningContinueCancel(
            this, i18n("This will delete all subtitle entries in <b>%1</b>,<br/>as well as the subtitle file: %2", name, path)) != KMessageBox::Continue) {
        return;
    }
    qDebug() << ":::: DELETING SUBTITLE WITH TREE INDEX: " << fileList->indexOfTopLevelItem(fileList->currentItem());
    QTreeWidgetItem *currentItem = fileList->currentItem();
    int ix = fileList->indexOfTopLevelItem(currentItem);
    int id = currentItem->data(0, Qt::UserRole).toInt();
    m_model->deleteSubtitle(id);
    parseFileList();
    buttonDeleteFile->setEnabled(!(fileList->topLevelItemCount() == 1));
    if (ix < fileList->topLevelItemCount()) {
        fileList->setCurrentItem(fileList->topLevelItem(ix));
    } else {
        fileList->setCurrentItem(fileList->topLevelItem(ix - 1));
    }
}

void ManageSubtitles::deleteEvent()
{
    QTreeWidgetItem *currentItem = eventList->currentItem();
    if (currentItem->parent() == nullptr) {
        return;
    }
    int sid = currentItem->data(0, Qt::UserRole).toInt();
    if (sid > -1) {
        int layer = m_model->getLayerForId(sid);
        GenTime startPos = m_model->getStartPosForId(sid);
        SubtitleEvent event = m_model->getSubtitle(layer, startPos);
        Fun local_redo = [this, sid, startPos, event]() {
            m_model->removeSubtitle(sid);
            QPair<int, int> range = {startPos.frames(pCore->getCurrentFps()), event.endTime().frames(pCore->getCurrentFps())};
            pCore->invalidateRange(range);
            pCore->refreshProjectRange(range);
            return true;
        };
        Fun local_undo = [this, sid, layer, startPos, event]() {
            m_model->addSubtitle(sid, {layer, startPos}, event);
            QPair<int, int> range = {startPos.frames(pCore->getCurrentFps()), event.endTime().frames(pCore->getCurrentFps())};
            pCore->invalidateRange(range);
            pCore->refreshProjectRange(range);
            return true;
        };
        local_redo();
        pCore->pushUndo(local_undo, local_redo, i18n("Delete subtitle"));

        QTreeWidgetItem *parent = currentItem->parent();
        delete currentItem;
        int count = parent->childCount();
        parent->setText(1, "with " + QString::number(count) + (count == 1 ? i18n(" event") : i18n(" events")));
    }
}

void ManageSubtitles::deleteLayer()
{
    QTreeWidgetItem *currentItem = eventList->currentItem();
    if (currentItem->parent() != nullptr) {
        deleteEvent();
        return;
    }
    // Layer
    int layer = currentItem->text(0).section(" ", 1, 1).toInt();
    if (currentItem->childCount() > 0 &&
        KMessageBox::warningContinueCancel(this, i18n("This will delete all events in layer <b>%1</b>.", layer)) != KMessageBox::Continue) {
        return;
    }

    QList<int> expanded;
    for (int i = 0; i < eventList->topLevelItemCount(); ++i) {
        if (i != layer && eventList->topLevelItem(i)->isExpanded()) {
            expanded.append(i < layer ? i : i - 1);
        }
    }

    m_model->requestDeleteLayer(layer);
    parseEventList();
    buttonDeleteLayer->setEnabled(!(eventList->topLevelItemCount() == 1));
    if (layer < eventList->topLevelItemCount()) {
        eventList->setCurrentItem(eventList->topLevelItem(layer));
    } else {
        eventList->setCurrentItem(eventList->topLevelItem(layer - 1));
    }

    for (int ix : expanded) {
        if (ix < eventList->topLevelItemCount()) {
            eventList->topLevelItem(ix)->setExpanded(true);
        }
    }
}

void ManageSubtitles::deleteStyle(SubtitleStyleScope scope)
{
    const QString name = currentStyleName();
    if (name.isEmpty()) {
        return;
    }
    if (deleteStyleInScope(name, scope, true)) {
        parseStyleList(scope);
    }
}

void ManageSubtitles::editStyle(SubtitleStyleScope scope)
{
    const QString styleName = currentStyleName();
    if (styleName.isEmpty()) {
        return;
    }
    SubtitleStyle style = styleForScope(styleName, scope);
    bool ok;
    QString newName = styleName;
    style = SubtitleStyleEdit::getStyle(this, style, newName, m_model, scope, &ok);
    if (ok) {
        // If the name changed, the dialog already ensured it is not used by another style in this scope
        applyStyle(newName, style, scope);
        parseStyleList(scope);
        selectStyleByName(newName);
    }
}

void ManageSubtitles::importSubtitleFile()
{
    QScopedPointer<ImportSubtitle> d(new ImportSubtitle(QString(), this));
    d->create_track->setChecked(true);
    if (d->exec() == QDialog::Accepted && !d->subtitle_url->url().isEmpty()) {
        if (d->create_track->isChecked()) {
            // Create a new subtitle entry
            addSubtitleFile(d->track_name->text());
        }
        int offset = 0, startFramerate = 30.00, targetFramerate = 30.00;
        if (d->cursor_pos->isChecked()) {
            offset = pCore->getMonitorPosition();
        }
        if (d->transform_framerate_check_box->isChecked()) {
            startFramerate = d->caption_original_framerate->value();
            targetFramerate = d->caption_target_framerate->value();
        }
        m_model->importSubtitle(d->subtitle_url->url().toLocalFile(), offset, true, startFramerate, targetFramerate,
                                d->codecs_list->currentData().toString().toUtf8());
        parseFileList();
        parseStyleList(SubtitleStyleScope::SubtitleFile);
        parseEventList();
        parseInfoList();
    }
}

SubtitleStyleScope ManageSubtitles::currentStyleScope() const
{
    if (styleFileSideBar->currentItem() == nullptr) {
        return SubtitleStyleScope::SubtitleFile;
    }
    return scopeForRole(styleFileSideBar->currentItem()->data(Qt::UserRole).toInt());
}

QString ManageSubtitles::currentStyleName() const
{
    const QTreeWidgetItem *item = styleList->currentItem();
    if (item == nullptr) {
        return QString();
    }
    const QVariant data = item->data(0, Qt::UserRole);
    return data.isValid() ? data.toString() : item->text(0);
}

QStringList ManageSubtitles::namesForScope(SubtitleStyleScope scope) const
{
    QStringList names;
    switch (scope) {
    case SubtitleStyleScope::Project:
        for (const auto &style : m_model->getAllSubtitleStyles(true)) {
            names.append(style.first);
        }
        break;
    case SubtitleStyleScope::Shared: {
        const SubtitleStyleLibrary *library = m_model->styleLibrary();
        if (library) {
            for (const auto &style : library->sharedStyles()) {
                names.append(style.first);
            }
        }
        break;
    }
    case SubtitleStyleScope::SubtitleFile:
    default:
        for (const auto &style : m_model->getAllSubtitleStyles(false)) {
            names.append(style.first);
        }
        break;
    }
    return names;
}

SubtitleStyle ManageSubtitles::styleForScope(const QString &name, SubtitleStyleScope scope) const
{
    switch (scope) {
    case SubtitleStyleScope::Project:
        if (m_model->getAllSubtitleStyles(true).find(name) != m_model->getAllSubtitleStyles(true).end()) {
            return m_model->getSubtitleStyle(name, true);
        }
        break;
    case SubtitleStyleScope::Shared: {
        const SubtitleStyleLibrary *library = m_model->styleLibrary();
        if (library && library->hasStyle(name)) {
            return library->style(name);
        }
        break;
    }
    case SubtitleStyleScope::SubtitleFile:
    default:
        if (m_model->getAllSubtitleStyles(false).find(name) != m_model->getAllSubtitleStyles(false).end()) {
            return m_model->getSubtitleStyle(name, false);
        }
        break;
    }
    return SubtitleStyle();
}

void ManageSubtitles::applyStyle(const QString &name, const SubtitleStyle &style, SubtitleStyleScope scope)
{
    switch (scope) {
    case SubtitleStyleScope::Project:
        m_model->setSubtitleStyle(name, style, true);
        break;
    case SubtitleStyleScope::Shared: {
        SubtitleStyleLibrary *library = m_model->styleLibrary();
        if (library) {
            QString error;
            if (!library->saveStyle(name, style, &error)) {
                KMessageBox::error(this, error);
            }
        }
        break;
    }
    case SubtitleStyleScope::SubtitleFile:
    default:
        m_model->setSubtitleStyle(name, style, false);
        break;
    }
}

bool ManageSubtitles::deleteStyleInScope(const QString &name, SubtitleStyleScope scope, bool confirm)
{
    if (scope == SubtitleStyleScope::SubtitleFile && name == QLatin1String("Default")) {
        if (confirm) {
            KMessageBox::error(this, i18n("Cannot delete default style."));
        }
        return false;
    }
    if (confirm) {
        QString message;
        if (scope == SubtitleStyleScope::SubtitleFile) {
            message =
                i18n("This will delete the style <b>%1</b> and all its properties.\nSubtitles using this style will be reset to style <b>Default</b>.", name);
        } else {
            message = i18n("This will delete the style <b>%1</b> and all its properties.", name);
        }
        if (KMessageBox::warningContinueCancel(this, message) != KMessageBox::Continue) {
            return false;
        }
    }
    switch (scope) {
    case SubtitleStyleScope::Project:
        m_model->deleteSubtitleStyle(name, true);
        break;
    case SubtitleStyleScope::Shared: {
        SubtitleStyleLibrary *library = m_model->styleLibrary();
        if (!library) {
            return false;
        }
        if (library->isBuiltinStyle(name)) {
            if (confirm) {
                KMessageBox::error(this, i18n("Built-in styles cannot be deleted."));
            }
            return false;
        }
        QString error;
        if (!library->deleteStyle(name, &error)) {
            if (confirm) {
                KMessageBox::error(this, error);
            }
            return false;
        }
        break;
    }
    case SubtitleStyleScope::SubtitleFile:
    default:
        m_model->deleteSubtitleStyle(name, false);
        break;
    }
    return true;
}

void ManageSubtitles::updateStyleButtons()
{
    const SubtitleStyleScope scope = currentStyleScope();
    QTreeWidgetItem *current = styleList->currentItem();
    const bool hasCurrent = current != nullptr;
    bool canEdit = hasCurrent;
    bool canDelete = hasCurrent;
    bool canDuplicate = hasCurrent;
    if (hasCurrent) {
        const QString name = currentStyleName();
        if (scope == SubtitleStyleScope::SubtitleFile && name == QLatin1String("Default")) {
            // The default style can be edited but not deleted
            canDelete = false;
        }
        if (scope == SubtitleStyleScope::Shared) {
            const SubtitleStyleLibrary *library = m_model->styleLibrary();
            if (library && library->isBuiltinStyle(name)) {
                // Built-in styles are read-only
                canEdit = false;
                canDelete = false;
            }
        }
    }
    buttonEditStyle->setEnabled(canEdit);
    buttonDeleteStyle->setEnabled(canDelete);
    buttonDuplicateStyle->setEnabled(canDuplicate);
}

void ManageSubtitles::selectSideBarScope(int role)
{
    for (int i = 0; i < styleFileSideBar->count(); ++i) {
        if (styleFileSideBar->item(i)->data(Qt::UserRole).toInt() == role) {
            styleFileSideBar->setCurrentRow(i);
            return;
        }
    }
}

void ManageSubtitles::selectStyleByName(const QString &name)
{
    for (int i = 0; i < styleList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = styleList->topLevelItem(i);
        const QVariant data = item->data(0, Qt::UserRole);
        if ((data.isValid() ? data.toString() : item->text(0)) == name) {
            styleList->setCurrentItem(item);
            return;
        }
    }
}

void ManageSubtitles::copyStyleToScope(SubtitleStyleScope scope)
{
    const QString sourceName = currentStyleName();
    if (sourceName.isEmpty()) {
        return;
    }
    const SubtitleStyleScope source = currentStyleScope();
    const SubtitleStyle style = styleForScope(sourceName, source);
    const QString newName = uniqueStyleName(sourceName, namesForScope(scope));
    applyStyle(newName, style, scope);
    if (scope == SubtitleStyleScope::Project) {
        selectSideBarScope(-1);
    } else if (scope == SubtitleStyleScope::Shared) {
        selectSideBarScope(-2);
    }
    parseStyleList(scope);
    selectStyleByName(newName);
}

void ManageSubtitles::showStyleMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = styleList->itemAt(pos);
    if (item) {
        styleList->setCurrentItem(item);
    }
    const SubtitleStyleScope scope = currentStyleScope();
    const bool hasStyle = styleList->currentItem() != nullptr;

    QMenu menu(this);
    QAction *copyToProject = nullptr;
    QAction *copyToShared = nullptr;
    if (hasStyle) {
        if (scope != SubtitleStyleScope::Project) {
            copyToProject = menu.addAction(i18n("Copy to Project Styles"));
        }
        if (scope != SubtitleStyleScope::Shared) {
            copyToShared = menu.addAction(i18n("Copy to Shared Styles"));
        }
        menu.addSeparator();
    }
    QAction *importAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-import")), i18n("Import Style Set..."));
    QAction *exportAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-export")), i18n("Export Style Set..."));
    exportAction->setEnabled(styleList->topLevelItemCount() > 0);
    if (m_downloadStyleAction) {
        menu.addSeparator();
        menu.addAction(m_downloadStyleAction);
    }

    QAction *chosen = menu.exec(styleList->viewport()->mapToGlobal(pos));
    if (chosen == nullptr) {
        return;
    }
    if (chosen == copyToProject) {
        copyStyleToScope(SubtitleStyleScope::Project);
    } else if (chosen == copyToShared) {
        copyStyleToScope(SubtitleStyleScope::Shared);
    } else if (chosen == importAction) {
        importStyleSet();
    } else if (chosen == exportAction) {
        exportStyleSet();
    }
}

void ManageSubtitles::importStyleSet()
{
    const QString path = QFileDialog::getOpenFileName(this, i18n("Import Style Set"), QDir::homePath(), i18n("Subtitle Style Set (*.ass)"));
    if (path.isEmpty()) {
        return;
    }
    SubtitleStyleLibrary *library = m_model->styleLibrary();
    if (!library) {
        return;
    }
    QString error;
    if (!library->importSet(path, &error)) {
        KMessageBox::error(this, error);
        return;
    }
    selectSideBarScope(-2);
    parseStyleList(SubtitleStyleScope::Shared);
}

void ManageSubtitles::exportStyleSet()
{
    SubtitleStyleLibrary *library = m_model->styleLibrary();
    if (!library) {
        return;
    }
    const SubtitleStyleScope scope = currentStyleScope();
    const QString path = QFileDialog::getSaveFileName(this, i18n("Export Style Set"), QDir::homePath() + QStringLiteral("/subtitle-styles.ass"),
                                                      i18n("Subtitle Style Set (*.ass)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    bool exported = false;
    if (scope == SubtitleStyleScope::Shared) {
        const QString setName = library->styleSetFor(currentStyleName());
        if (!setName.isEmpty()) {
            exported = library->exportSet(setName, path, &error);
        }
    }
    if (!exported) {
        std::map<QString, SubtitleStyle> styles;
        for (const QString &name : namesForScope(scope)) {
            styles[name] = styleForScope(name, scope);
        }
        exported = library->exportStyles(path, styles, &error);
    }
    if (!exported) {
        KMessageBox::error(this, error);
    }
}
