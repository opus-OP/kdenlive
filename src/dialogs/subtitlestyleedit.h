/*  This file is part of Kdenlive. See www.kdenlive.org.
    SPDX-FileCopyrightText: 2024 Chengkun Chen <serix2004@gmail.com>
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "bin/model/subtitlestylelibrary.h"
#include "definitions.h"
#include "ui_editsubstyle_ui.h"
#include <QDialog>
#include <mlt++/MltProducer.h>
#include <qdir.h>

class SubtitleModel;

class SubtitleStyleEdit : public QDialog, public Ui::SubtitleStyleEdit_UI
{
    Q_OBJECT

public:
    static const SubtitleStyle getStyle(QWidget *parent, const SubtitleStyle &style, QString &styleName, std::shared_ptr<const SubtitleModel> model,
                                        SubtitleStyleScope scope, bool *ok);

private:
    explicit SubtitleStyleEdit(QWidget *parent = nullptr);
    ~SubtitleStyleEdit();

    SubtitleStyle m_style;
    bool *m_ok{nullptr};
    int m_width = 1920;
    int m_height = 1080;
    QString m_previewPath;
    QSize m_lastSize;
    std::unique_ptr<Mlt::Profile> m_previewProfile;
    std::unique_ptr<Mlt::Producer> m_previewProducer;
    std::unique_ptr<Mlt::Filter> m_previewFilter;
    QFile m_previewFile;
    void setProperties(const SubtitleStyle &style, const QString &styleName, const int previewWidth, const int previewHeight, const QString previewPath,
                       bool *ok);
    void updateProperties();
    /** @brief Text of the preview event with the current animation tags applied */
    QString previewText() const;

    /** @brief Widgets of one animation tab (appearance or disappearance) */
    struct AnimationWidgets
    {
        QComboBox *preset = nullptr;
        QSpinBox *delay = nullptr;
        QSpinBox *duration = nullptr;
        QSpinBox *direction = nullptr;
        QDoubleSpinBox *intensity = nullptr;
        QCheckBox *ease = nullptr;
    };
    /** @brief Widgets of the appearance (entrance) tab */
    AnimationWidgets animationInWidgets() const;
    /** @brief Widgets of the disappearance (exit) tab */
    AnimationWidgets animationOutWidgets() const;
    /** @brief Fill @p combo with the presets of an entrance or exit animation */
    void populateAnimationPresets(QComboBox *combo, bool isOut) const;
    /** @brief Read the animation described by @p widgets */
    SubtitleAnimation animationFromWidgets(const AnimationWidgets &widgets, bool isOut) const;
    /** @brief Write @p animation into @p widgets */
    void setAnimationWidgets(const AnimationWidgets &widgets, const SubtitleAnimation &animation, bool isOut);
    /** @brief Enable or disable the animation fields depending on the selected preset */
    void updateAnimationWidgets(const AnimationWidgets &widgets, bool isOut);
    /** @brief Apply the defaults of the newly selected animation preset */
    void animationPresetChanged(const AnimationWidgets &widgets, bool isOut);
    /** @brief Preset data of the preset matching @p animation */
    static int presetForAnimation(const SubtitleAnimation &animation, bool isOut);
};
