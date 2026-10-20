/*
    SPDX-FileCopyrightText: 2007 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "definitions.h"
#include <KLocalizedString>

#include <QColor>
#include <cmath>
#include <utility>

#ifdef CRASH_AUTO_TEST
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <rttr/registration>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

RTTR_REGISTRATION
{
    using namespace rttr;
    // clang-format off
    registration::enumeration<GroupType>("GroupType")(
        value("Normal", GroupType::Normal),
        value("Selection", GroupType::Selection),
        value("AVSplit", GroupType::AVSplit),
        value("Leaf", GroupType::Leaf)
        );
    registration::enumeration<PlaylistState::ClipState>("PlaylistState")(
        value("VideoOnly", PlaylistState::VideoOnly),
        value("AudioOnly", PlaylistState::AudioOnly),
        value("Disabled", PlaylistState::Disabled)
        );
    // clang-format on
}
#endif

QDebug operator<<(QDebug qd, const ItemInfo &info)
{
    qd << "ItemInfo " << &info;
    qd << "\tTrack" << info.trackId;
    qd << "\tStart pos: " << info.position;
    qd << "\tEnd pos: " << info.endPos;
    qd << "\tCrop start: " << info.cropStart;
    qd << "\tCrop duration: " << info.playTime;
    return qd.maybeSpace();
}

CommentedTime::CommentedTime()
    : m_time(GenTime(0))

{
}

CommentedTime::CommentedTime(const GenTime &time, QString comment, int markerType, const GenTime &duration)
    : m_time(time)
    , m_comment(std::move(comment))
    , m_type(markerType)
    , m_duration(duration)
{
}

CommentedTime::CommentedTime(const QString &hash, const GenTime &time)
    : m_time(time)
    , m_comment(hash.section(QLatin1Char(':'), 1))
    , m_type(hash.section(QLatin1Char(':'), 0, 0).toInt())
{
}

QString CommentedTime::comment() const
{
    return (m_comment.isEmpty() ? i18n("Marker") : m_comment);
}

GenTime CommentedTime::time() const
{
    return m_time;
}

void CommentedTime::setComment(const QString &comm)
{
    m_comment = comm;
}

void CommentedTime::setTime(const GenTime &t)
{
    m_time = t;
}

void CommentedTime::setMarkerType(int newtype)
{
    m_type = newtype;
}

QString CommentedTime::hash() const
{
    return QString::number(m_type) + QLatin1Char(':') + (m_comment.isEmpty() ? i18n("Marker") : m_comment);
}

int CommentedTime::markerType() const
{
    return m_type;
}

bool CommentedTime::operator>(const CommentedTime &op) const
{
    return m_time > op.time();
}

bool CommentedTime::operator<(const CommentedTime &op) const
{
    return m_time < op.time();
}

bool CommentedTime::operator>=(const CommentedTime &op) const
{
    return m_time >= op.time();
}

bool CommentedTime::operator<=(const CommentedTime &op) const
{
    return m_time <= op.time();
}

bool CommentedTime::operator==(const CommentedTime &op) const
{
    return m_time == op.time();
}

bool CommentedTime::operator!=(const CommentedTime &op) const
{
    return m_time != op.time();
}

const QString groupTypeToStr(GroupType t)
{
    switch (t) {
    case GroupType::Normal:
        return QStringLiteral("Normal");
    case GroupType::Selection:
        return QStringLiteral("Selection");
    case GroupType::AVSplit:
        return QStringLiteral("AVSplit");
    case GroupType::Leaf:
        return QStringLiteral("Leaf");
    }
    Q_ASSERT(false);
    return QString();
}
GroupType groupTypeFromStr(const QString &s)
{
    std::vector<GroupType> types{GroupType::Selection, GroupType::Normal, GroupType::AVSplit, GroupType::Leaf};
    for (const auto &t : types) {
        if (s == groupTypeToStr(t)) {
            return t;
        }
    }
    Q_ASSERT(false);
    return GroupType::Normal;
}

std::pair<bool, bool> stateToBool(PlaylistState::ClipState state)
{
    return {state == PlaylistState::VideoOnly, state == PlaylistState::AudioOnly};
}
PlaylistState::ClipState stateFromBool(std::pair<bool, bool> av)
{
    Q_ASSERT(!av.first || !av.second);
    if (av.first) {
        return PlaylistState::VideoOnly;
    } else if (av.second) {
        return PlaylistState::AudioOnly;
    } else {
        return PlaylistState::Disabled;
    }
}

SubtitleEvent::SubtitleEvent(const QString &eventString, const double fps, const double factor, std::pair<int, GenTime> *start)
{
    m_isDialogue = (eventString.section(":", 0, 0) == "Dialogue");
    QStringList parts = eventString.section(":", 1).split(",");
    if (parts.size() < 10) {
        // Invalid subtitle event
        return;
    }

    // parse the parts
    int layer = parts[0].toInt();
    GenTime startTime = stringtoTime(parts[1], fps, factor);
    GenTime endTime = stringtoTime(parts[2], fps, factor);
    QString styleName = parts[3];
    QString name = parts[4];
    int marginL = parts[5].toInt();
    int marginR = parts[6].toInt();
    int marginV = parts[7].toInt();
    QString effect = parts[8];
    QString text = "";
    for (int i = 9; i < parts.size() - 1; i++) {
        text += parts.at(i) + ',';
    }
    text += parts.at(parts.size() - 1);
    text.replace(QLatin1String("\\n"), QLatin1String("\n"));

    // check the validity
    if (layer < 0 || endTime <= startTime) {
        // Invalid subtitle event
        return;
    }

    // m_layer = layer;
    // m_startTime = startTime;
    m_endTime = endTime;
    m_styleName = styleName;
    m_name = name;
    m_marginL = marginL;
    m_marginR = marginR;
    m_marginV = marginV;
    m_effect = effect;
    m_text = text;

    // Pass the layer and start time if needed
    if (start != nullptr) {
        start->first = layer;
        start->second = startTime;
    }
}

bool SubtitleEvent::operator==(const SubtitleEvent &op) const
{
    return m_isDialogue == op.m_isDialogue && m_endTime == op.m_endTime && m_styleName == op.m_styleName && m_name == op.m_name && m_marginL == op.m_marginL &&
           m_marginR == op.m_marginR && m_marginV == op.m_marginV && m_effect == op.m_effect && m_text == op.m_text;
}

bool SubtitleEvent::operator!=(const SubtitleEvent &op) const
{
    return m_isDialogue != op.m_isDialogue || m_endTime != op.m_endTime || m_styleName != op.m_styleName || m_name != op.m_name || m_marginL != op.m_marginL ||
           m_marginR != op.m_marginR || m_marginV != op.m_marginV || m_effect != op.m_effect || m_text != op.m_text;
}

QString SubtitleEvent::toString(int layer, GenTime start) const
{
    QString result;

    if (this->isDialogue())
        result += QStringLiteral("Dialogue: ");
    else
        result += QStringLiteral("Comment: ");

    result += QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10")
                  .arg(layer)
                  .arg(this->timeToString(start, 0))
                  .arg(this->timeToString(this->endTime(), 0))
                  .arg(this->styleName())
                  .arg(this->name())
                  .arg(this->marginL())
                  .arg(this->marginR())
                  .arg(this->marginV())
                  .arg(this->effect())
                  .arg(this->text());
    return result;
}

GenTime SubtitleEvent::stringtoTime(const QString &str, const double fps, const double factor)
{
    QStringList total, secs;
    double milliseconds = 0;
    GenTime pos;
    total = str.split(QLatin1Char(':'));
    if (total.count() == 3) {
        // There is an hour timestamp
        milliseconds += atoi(total.takeFirst().toStdString().c_str()) * 60 * 60 * 1000;
    }
    if (total.count() == 2) {
        // minutes
        milliseconds += atoi(total.at(0).toStdString().c_str()) * 60 * 1000;
        if (total.at(1).contains(QLatin1Char('.'))) {
            secs = total.at(1).split(QLatin1Char('.')); // ssa file, sbv file or vtt file
        } else {
            secs = total.at(1).split(QLatin1Char(',')); // srt file
        }
        if (secs.count() < 2) {
            // seconds
            milliseconds += atoi(total.at(1).toStdString().c_str()) * 1000;
        } else {
            milliseconds += atoi(secs.at(0).toStdString().c_str()) * 1000;
            // milliseconds or centiseconds
            if (total.at(1).contains(QLatin1Char('.'))) {
                if (secs.at(1).length() == 2) {
                    // ssa file
                    // milliseconds = centiseconds * 10
                    milliseconds += atoi(secs.at(1).toStdString().c_str()) * 10;
                } else {
                    // vtt file or sbv file
                    milliseconds += atoi(secs.at(1).toStdString().c_str());
                }
            } else {
                // srt file
                milliseconds += atoi(secs.at(1).toStdString().c_str());
            }
        }
    } else {
        // invalid time found
        Q_ASSERT(false);
        return GenTime();
    }

    pos = GenTime(milliseconds / 1000) / factor;
    // Ensure times are aligned with our project's frames
    int frames = pos.frames(fps);
    return GenTime(frames, fps);
}

QString SubtitleEvent::timeToString(const GenTime &time, const int format)
{
    int millisecond = (int)time.ms();
    int second = millisecond / 1000;
    millisecond %= 1000;
    int centisecond = (millisecond % 10 > 5) ? (millisecond / 10 + 1) : (millisecond / 10);
    int minute = second / 60;
    second %= 60;
    int hour = minute / 60;
    minute %= 60;

    switch (format) {
    // ASS Format
    case 0:
        return QStringLiteral("%1:%2:%3.%4")
            .arg(hour, 2, 10, QLatin1Char('0'))
            .arg(minute, 2, 10, QLatin1Char('0'))
            .arg(second, 2, 10, QLatin1Char('0'))
            .arg(centisecond, 2, 10, QLatin1Char('0'));
        break;
    // SRT Format
    case 1:
        return QStringLiteral("%1:%2:%3,%4")
            .arg(hour, 2, 10, QLatin1Char('0'))
            .arg(minute, 2, 10, QLatin1Char('0'))
            .arg(second, 2, 10, QLatin1Char('0'))
            .arg(millisecond, 3, 10, QLatin1Char('0'));
        break;
    default:
        return QString();
        break;
    }
}

namespace {
/** @brief Convert a Unicode code point to a QString (handles astral characters) */
QString codePointToString(uint codePoint)
{
    if (codePoint <= 0xFFFF) {
        return QString(QChar(static_cast<ushort>(codePoint)));
    }
    const uint value = codePoint - 0x10000;
    return QString(QChar(static_cast<ushort>(0xD800 + (value >> 10)))) + QChar(static_cast<ushort>(0xDC00 + (value & 0x3FF)));
}

QString animationKindToString(SubtitleAnimationKind kind)
{
    switch (kind) {
    case SubtitleAnimationKind::Fade:
        return QStringLiteral("fade");
    case SubtitleAnimationKind::Move:
        return QStringLiteral("move");
    case SubtitleAnimationKind::Scale:
        return QStringLiteral("scale");
    case SubtitleAnimationKind::Rotate:
        return QStringLiteral("rotate");
    case SubtitleAnimationKind::Typewriter:
        return QStringLiteral("typewriter");
    case SubtitleAnimationKind::Erase:
        return QStringLiteral("erase");
    case SubtitleAnimationKind::None:
    default:
        return QStringLiteral("none");
    }
}

SubtitleAnimationKind animationKindFromString(const QString &value)
{
    const QString kind = value.trimmed().toLower();
    if (kind == QLatin1String("fade")) {
        return SubtitleAnimationKind::Fade;
    }
    if (kind == QLatin1String("move")) {
        return SubtitleAnimationKind::Move;
    }
    if (kind == QLatin1String("scale")) {
        return SubtitleAnimationKind::Scale;
    }
    if (kind == QLatin1String("rotate")) {
        return SubtitleAnimationKind::Rotate;
    }
    if (kind == QLatin1String("typewriter")) {
        return SubtitleAnimationKind::Typewriter;
    }
    if (kind == QLatin1String("erase")) {
        return SubtitleAnimationKind::Erase;
    }
    return SubtitleAnimationKind::None;
}

/** @brief Acceleration argument of the \\t tag used to emulate easing.
 *
 * libass interprets the (optional) acceleration argument as a power exponent: the value 1
 * (or an omitted argument) means linear interpolation, a value below 1 decelerates towards
 * the end and 0 jumps to the final value immediately. ASS has no true S-curve, so easing is
 * approximated by a single deceleration exponent.
 */
QString animationEaseArgument(bool easeInOut)
{
    return easeInOut ? QStringLiteral("0.5") : QStringLiteral("1");
}
} // namespace

QString SubtitleAnimation::toString() const
{
    return QStringLiteral("kind=%1;dur=%2;delay=%3;dir=%4;intensity=%5;ease=%6")
        .arg(animationKindToString(kind))
        .arg(durationMs)
        .arg(delayMs)
        .arg(direction)
        .arg(QString::number(intensity, 'g', 10))
        .arg(easeInOut ? 1 : 0);
}

SubtitleAnimation SubtitleAnimation::fromString(const QString &value)
{
    SubtitleAnimation animation;
    const QStringList tokens = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const int separator = token.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }
        const QString key = token.left(separator).trimmed().toLower();
        const QString tokenValue = token.mid(separator + 1).trimmed();
        if (key == QLatin1String("kind")) {
            animation.kind = animationKindFromString(tokenValue);
        } else if (key == QLatin1String("dur")) {
            bool ok = false;
            const int duration = tokenValue.toInt(&ok);
            if (ok) {
                // Clamp so that a corrupt file can never overflow the tag arithmetic below.
                animation.durationMs = qBound(0, duration, kSubtitleAnimationMaxDurationMs);
            }
        } else if (key == QLatin1String("delay")) {
            bool ok = false;
            const int delay = tokenValue.toInt(&ok);
            if (ok) {
                animation.delayMs = qBound(0, delay, kSubtitleAnimationMaxDurationMs);
            }
        } else if (key == QLatin1String("dir")) {
            bool ok = false;
            const int direction = tokenValue.toInt(&ok);
            if (ok) {
                animation.direction = direction;
            }
        } else if (key == QLatin1String("intensity")) {
            bool ok = false;
            const double intensity = tokenValue.toDouble(&ok);
            if (ok && std::isfinite(intensity) && intensity > 0) {
                animation.intensity = qBound(kSubtitleAnimationMinIntensity, intensity, kSubtitleAnimationMaxIntensity);
            }
        } else if (key == QLatin1String("ease")) {
            animation.easeInOut = tokenValue != QLatin1String("0");
        }
    }
    return animation;
}

SubtitleAnimationSet SubtitleAnimationSet::resolveForText(const QString &text) const
{
    SubtitleAnimationSet resolved = *this;
    const bool needsDegradation = text.size() > kTypewriterMaxChars || text.contains(QLatin1Char('{')) || text.contains(QLatin1Char('}'));
    if (needsDegradation) {
        // User authored override blocks and very long texts cannot be wrapped character by
        // character safely: fall back to a fade for the affected animation.
        if (resolved.in.kind == SubtitleAnimationKind::Typewriter) {
            resolved.in.kind = SubtitleAnimationKind::Fade;
        }
        if (resolved.out.kind == SubtitleAnimationKind::Erase) {
            resolved.out.kind = SubtitleAnimationKind::Fade;
        }
    }
    return resolved;
}

void SubtitleAnimationSet::resolveWindows(int eventDurationMs, qint64 *inStart, qint64 *inEnd, qint64 *outStart, qint64 *outEnd) const
{
    auto scaled = [](qint64 value, double factor) -> qint64 {
        if (value <= 0) {
            return 0;
        }
        return qMax<qint64>(1, qRound(value * factor));
    };

    const bool hasIn = in.isEnabled();
    const bool hasOut = out.isEnabled();
    qint64 inDuration = hasIn ? qMax(0, in.durationMs) : 0;
    qint64 outDuration = hasOut ? qMax(0, out.durationMs) : 0;
    // Clamp so that a corrupt file can never overflow the tag arithmetic below (qint64 keeps the
    // sum of an already clamped delay and duration from overflowing).
    qint64 inDelay = hasIn ? qBound<qint64>(0, static_cast<qint64>(in.delayMs), static_cast<qint64>(kSubtitleAnimationMaxDurationMs)) : 0;
    qint64 outDelay = hasOut ? qBound<qint64>(0, static_cast<qint64>(out.delayMs), static_cast<qint64>(kSubtitleAnimationMaxDurationMs)) : 0;
    const qint64 duration = qMax<qint64>(0, eventDurationMs);

    if (duration > 0 && inDuration + outDuration > 0) {
        const qint64 available = duration - inDelay - outDelay;
        if (available <= 0) {
            // The delays alone fill the event: drop them so that both animations still get a chance
            inDelay = 0;
            outDelay = 0;
            if (inDuration + outDuration > duration) {
                const double factor = static_cast<double>(duration) / static_cast<double>(inDuration + outDuration);
                inDuration = scaled(inDuration, factor);
                outDuration = scaled(outDuration, factor);
            }
        } else if (inDuration + outDuration > available) {
            // Proportional shrink: a short event shortens both animations instead of losing one
            const double factor = static_cast<double>(available) / static_cast<double>(inDuration + outDuration);
            inDuration = scaled(inDuration, factor);
            outDuration = scaled(outDuration, factor);
        }
    }

    if (hasIn) {
        *inStart = duration > 0 ? qMin(inDelay, duration) : inDelay;
        *inEnd = duration > 0 ? qMin(*inStart + inDuration, duration) : *inStart + inDuration;
        if (*inEnd < *inStart) {
            *inEnd = *inStart;
        }
    } else {
        *inStart = -1;
        *inEnd = -1;
    }
    if (hasOut) {
        // The exit animation is anchored at the end of the event: its delay counts backwards
        const qint64 end = duration > 0 ? duration - qMin(outDelay, duration) : outDuration;
        *outEnd = qMax<qint64>(0, end);
        *outStart = qMax<qint64>(0, *outEnd - outDuration);
    } else {
        *outStart = -1;
        *outEnd = -1;
    }
}

QString SubtitleAnimationSet::toAssPrefix(int eventDurationMs, int alignment, int frameWidth, int frameHeight, int marginL, int marginR, int marginV,
                                          double fontSize, int lineCount, int textWidth) const
{
    if (!isEnabled()) {
        return QString();
    }
    if (frameWidth <= 0) {
        frameWidth = 1920;
    }
    if (frameHeight <= 0) {
        frameHeight = 1080;
    }
    if (alignment < 1 || alignment > 9) {
        alignment = 2;
    }
    if (lineCount < 1) {
        lineCount = 1;
    }
    if (fontSize <= 0) {
        fontSize = frameHeight / 18.0;
    }
    if (textWidth < 0) {
        textWidth = 0;
    }

    SubtitleAnimationKind inKind = in.isEnabled() ? in.kind : SubtitleAnimationKind::None;
    SubtitleAnimationKind outKind = out.isEnabled() ? out.kind : SubtitleAnimationKind::None;
    if (inKind == SubtitleAnimationKind::Move && outKind == SubtitleAnimationKind::Move) {
        // libass allows a single \\move per event: keep the entrance and fade the event out instead.
        outKind = SubtitleAnimationKind::Fade;
    }
    // \\fad must not be mixed with the per character \\t(\\alpha) driven animations
    const bool perCharacter = (inKind == SubtitleAnimationKind::Typewriter || outKind == SubtitleAnimationKind::Erase);

    qint64 inStart = -1;
    qint64 inEnd = -1;
    qint64 outStart = -1;
    qint64 outEnd = -1;
    resolveWindows(eventDurationMs, &inStart, &inEnd, &outStart, &outEnd);

    // The anchor honours the style margins so left/right aligned events keep their expected position.
    const int column = (alignment - 1) % 3;
    const int row = (alignment - 1) / 3;
    const int anchorX = (column == 0) ? marginL : (column == 1 ? frameWidth / 2 : frameWidth - marginR);
    const int anchorY = (row == 0) ? frameHeight - marginV : (row == 2 ? marginV : frameHeight / 2);
    // Estimated text block height: one line is about 1.45 times the font size.
    const int textHeight = qRound(lineCount * fontSize * 1.45);

    // Off screen position used by the slides, either as the start (entrance) or as the end (exit).
    // The horizontal starts use the estimated text width so that the whole text leaves the frame
    // (and a slide from the right still moves when the anchor is the right edge).
    auto offScreen = [&](int direction, int *x, int *y) {
        *x = anchorX;
        *y = anchorY;
        switch (direction) {
        case 1: // above
            *y = -textHeight - marginV;
            break;
        case 2: // left
            *x = -frameWidth;
            break;
        case 3: // right
            *x = frameWidth + textWidth;
            break;
        case 0: // below
        default:
            *y = frameHeight + textHeight + marginV;
            break;
        }
    };

    // \\move and \\pos are mutually exclusive (the first one wins), so emit exactly one of them
    QString position;
    if (inKind == SubtitleAnimationKind::Move) {
        int x1 = anchorX;
        int y1 = anchorY;
        offScreen(in.direction, &x1, &y1);
        position = QStringLiteral("\\move(%1,%2,%3,%4,%5,%6)").arg(x1).arg(y1).arg(anchorX).arg(anchorY).arg(inStart).arg(inEnd);
    } else if (outKind == SubtitleAnimationKind::Move) {
        int x2 = anchorX;
        int y2 = anchorY;
        offScreen(out.direction, &x2, &y2);
        position = QStringLiteral("\\move(%1,%2,%3,%4,%5,%6)").arg(anchorX).arg(anchorY).arg(x2).arg(y2).arg(outStart).arg(outEnd);
    }

    QString geometry;
    if (position.isEmpty() && (inKind == SubtitleAnimationKind::Scale || inKind == SubtitleAnimationKind::Rotate || outKind == SubtitleAnimationKind::Scale ||
                               outKind == SubtitleAnimationKind::Rotate)) {
        // Scale and rotate need a stable anchor; a slide already provides the position
        geometry += QStringLiteral("\\pos(%1,%2)").arg(anchorX).arg(anchorY);
    }

    auto rotationAngle = [](int direction) {
        switch (direction) {
        case 1:
            return -90;
        case 2:
            return 90;
        case 3:
            return 180;
        case 0:
        default:
            return 360;
        }
    };

    QString transitions;
    const bool inScale = inKind == SubtitleAnimationKind::Scale;
    const bool outScale = outKind == SubtitleAnimationKind::Scale;
    const double inTarget = qMax(kSubtitleAnimationMinIntensity, in.intensity);
    if (inScale || outScale) {
        // \\t requires an explicit initial value before it; an entrance zoom out starts from the
        // inverse of its target.
        const int startPercent = (inScale && inTarget < 1.0) ? qRound(100.0 / inTarget) : 100;
        geometry += QStringLiteral("\\fscx%1\\fscy%1").arg(startPercent);
        if (inScale) {
            transitions +=
                QStringLiteral("\\t(%1,%2,%3,\\fscx%4\\fscy%4)").arg(inStart).arg(inEnd).arg(animationEaseArgument(in.easeInOut)).arg(qRound(inTarget * 100.0));
        }
        if (outScale) {
            const double outTarget = qMax(kSubtitleAnimationMinIntensity, out.intensity);
            transitions += QStringLiteral("\\t(%1,%2,%3,\\fscx%4\\fscy%4)")
                               .arg(outStart)
                               .arg(outEnd)
                               .arg(animationEaseArgument(out.easeInOut))
                               .arg(qRound(outTarget * 100.0));
        }
    }
    if (inKind == SubtitleAnimationKind::Rotate || outKind == SubtitleAnimationKind::Rotate) {
        // \\org sets the rotation centre (the anchor), \\frz0 is the required initial value
        geometry += QStringLiteral("\\org(%1,%2)\\frz0").arg(anchorX).arg(anchorY);
        if (inKind == SubtitleAnimationKind::Rotate) {
            transitions +=
                QStringLiteral("\\t(%1,%2,%3,\\frz%4)").arg(inStart).arg(inEnd).arg(animationEaseArgument(in.easeInOut)).arg(rotationAngle(in.direction));
        }
        if (outKind == SubtitleAnimationKind::Rotate) {
            transitions +=
                QStringLiteral("\\t(%1,%2,%3,\\frz%4)").arg(outStart).arg(outEnd).arg(animationEaseArgument(out.easeInOut)).arg(rotationAngle(out.direction));
        }
    }

    QString alpha;
    if (!perCharacter) {
        // \\fad() has no delay argument, so a delayed fade starts at the event edge instead
        const qint64 fadeIn = (inKind == SubtitleAnimationKind::Fade) ? qMax<qint64>(0, inEnd - inStart) : 0;
        const qint64 fadeOut = (outKind == SubtitleAnimationKind::Fade) ? qMax<qint64>(0, outEnd - outStart) : 0;
        if (fadeIn > 0 || fadeOut > 0) {
            alpha = QStringLiteral("\\fad(%1,%2)").arg(fadeIn).arg(fadeOut);
        }
    }

    return QStringLiteral("{\\an%1").arg(alignment) + position + geometry + alpha + transitions + QLatin1Char('}');
}

QString SubtitleAnimation::toAssPrefix(int eventDurationMs, int alignment, int frameWidth, int frameHeight, int marginL, int marginR, int marginV,
                                       double fontSize, int lineCount, int textWidth) const
{
    SubtitleAnimationSet set;
    set.in = *this;
    return set.toAssPrefix(eventDurationMs, alignment, frameWidth, frameHeight, marginL, marginR, marginV, fontSize, lineCount, textWidth);
}

int SubtitleAnimation::estimateTextWidth(const QString &text, double fontSize)
{
    if (fontSize <= 0) {
        fontSize = 40.0;
    }
    int maxLineLength = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        maxLineLength = qMax(maxLineLength, line.size());
    }
    // w_text ~ characters * fontSize * (ScaleX / 100) * 0.55, assuming the style ScaleX is 100%
    // (it is not available here and 100 is the common default).
    return qRound(maxLineLength * fontSize * 0.55);
}

namespace {
/** @brief One unit of the per character animations: a character with the blocks attached to it. */
struct AnimationToken
{
    QString prefix;    // user override blocks attached to the following character, never animated
    QString character; // the character itself
    bool animate = true;
};

/** @brief Split @p text into the tokens consumed by the typewriter and erase animations.
 *
 * User authored override blocks (<tt>{...}</tt>) are attached to the following character while
 * stray braces and line breaks are preserved as is. Characters are kept as whole code points so
 * that astral characters (emoji) stay intact.
 */
QList<AnimationToken> animationTokens(const QString &text)
{
    QList<uint> codePoints;
    for (int i = 0; i < text.size(); ++i) {
        const QChar current = text.at(i);
        if (current.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            codePoints.append(QChar::surrogateToUcs4(current, text.at(i + 1)));
            i++;
        } else {
            codePoints.append(current.unicode());
        }
    }

    QList<AnimationToken> tokens;
    QString pendingBlocks;
    bool insideBlock = false;
    for (uint codePoint : codePoints) {
        if (codePoint == '{') {
            insideBlock = true;
            pendingBlocks += QLatin1Char('{');
            continue;
        }
        if (insideBlock) {
            pendingBlocks += codePointToString(codePoint);
            if (codePoint == '}') {
                insideBlock = false;
            }
            continue;
        }
        if (codePoint == '}' || codePoint == '\n' || codePoint == '\r') {
            // Preserve stray braces and line breaks without animating them
            if (!pendingBlocks.isEmpty()) {
                AnimationToken blockToken;
                blockToken.prefix = pendingBlocks;
                blockToken.animate = false;
                tokens.append(blockToken);
                pendingBlocks.clear();
            }
            AnimationToken rawToken;
            rawToken.character = codePointToString(codePoint);
            rawToken.animate = false;
            tokens.append(rawToken);
            continue;
        }
        AnimationToken token;
        token.prefix = pendingBlocks;
        token.character = codePointToString(codePoint);
        pendingBlocks.clear();
        tokens.append(token);
    }
    if (!pendingBlocks.isEmpty()) {
        AnimationToken token;
        token.prefix = pendingBlocks;
        token.animate = false;
        tokens.append(token);
    }
    return tokens;
}
} // namespace

QString SubtitleAnimation::applyToText(const QString &text, int eventDurationMs) const
{
    SubtitleAnimationSet set;
    set.in = *this;
    return set.applyToText(text, eventDurationMs);
}

QString SubtitleAnimationSet::applyToText(const QString &text, int eventDurationMs) const
{
    if (text.isEmpty()) {
        return text;
    }
    // Callers are expected to pass the set returned by resolveForText (the model and the style
    // editor both do); applyToText itself stays tolerant of user override blocks and attaches them
    // to the following character, so a forgotten resolve can never corrupt a tag.
    const SubtitleAnimationSet &resolved = *this;
    const SubtitleAnimationKind inKind = resolved.in.isEnabled() ? resolved.in.kind : SubtitleAnimationKind::None;
    const SubtitleAnimationKind outKind = resolved.out.isEnabled() ? resolved.out.kind : SubtitleAnimationKind::None;
    if (inKind != SubtitleAnimationKind::Typewriter && outKind != SubtitleAnimationKind::Erase) {
        // The alpha channel is driven by \fad, no per character tag is needed
        return text;
    }

    const QList<AnimationToken> tokens = animationTokens(text);
    int characterCount = 0;
    for (const AnimationToken &token : tokens) {
        if (token.animate) {
            characterCount++;
        }
    }
    if (characterCount == 0) {
        return text;
    }

    qint64 inStart = -1;
    qint64 inEnd = -1;
    qint64 outStart = -1;
    qint64 outEnd = -1;
    resolved.resolveWindows(eventDurationMs, &inStart, &inEnd, &outStart, &outEnd);

    const double inStep = (inKind == SubtitleAnimationKind::Typewriter) ? static_cast<double>(qMax<qint64>(0, inEnd - inStart)) / characterCount : 0.0;
    const qint64 inRamp = qMax<qint64>(1, qRound(inStep / 2.0));
    const double outStep = (outKind == SubtitleAnimationKind::Erase) ? static_cast<double>(qMax<qint64>(0, outEnd - outStart)) / characterCount : 0.0;
    const qint64 outRamp = qMax<qint64>(1, qRound(outStep / 2.0));

    QString result;
    int index = 0;
    for (const AnimationToken &token : tokens) {
        result += token.prefix;
        if (!token.animate) {
            result += token.character;
            continue;
        }
        qint64 appearStart = -1;
        qint64 appearEnd = -1;
        if (inKind == SubtitleAnimationKind::Typewriter) {
            appearStart = inStart + qRound(index * inStep);
            appearEnd = appearStart + inRamp;
        } else if (inKind == SubtitleAnimationKind::Fade) {
            // A fade combined with a per character exit is generated per character as well
            appearStart = inStart;
            appearEnd = qMax(inEnd, inStart);
        }
        qint64 vanishStart = -1;
        qint64 vanishEnd = -1;
        if (outKind == SubtitleAnimationKind::Erase) {
            // The direction selects the erasing order: left to right by default, right to left if 1
            const int order = (resolved.out.direction == 1) ? (characterCount - 1 - index) : index;
            vanishStart = outStart + qRound(order * outStep);
            vanishEnd = vanishStart + outRamp;
        } else if (outKind == SubtitleAnimationKind::Fade) {
            vanishStart = outStart;
            vanishEnd = qMax(outEnd, outStart);
        }
        if (appearStart >= 0 && vanishStart >= 0) {
            // Never let the two transitions overlap on the same character
            vanishStart = qMax(vanishStart, appearEnd);
            vanishEnd = qMax(vanishEnd, vanishStart);
            result +=
                QStringLiteral("{\\alpha&HFF&\\t(%1,%2,\\alpha&H00&)\\t(%3,%4,\\alpha&HFF&)}").arg(appearStart).arg(appearEnd).arg(vanishStart).arg(vanishEnd);
        } else if (appearStart >= 0) {
            result += QStringLiteral("{\\alpha&HFF&\\t(%1,%2,\\alpha&H00&)}").arg(appearStart).arg(appearEnd);
        } else if (vanishStart >= 0) {
            result += QStringLiteral("{\\alpha&H00&\\t(%1,%2,\\alpha&HFF&)}").arg(vanishStart).arg(vanishEnd);
        }
        if (token.character == QLatin1String("\\")) {
            // A literal backslash immediately followed by the next character's injected block would
            // form an escape sequence (\{) and corrupt that tag. A zero width non joiner, which is
            // invisible, breaks the sequence while keeping the user's text readable.
            result += QStringLiteral("\\") + QChar(0x200C);
        } else {
            result += token.character;
        }
        index++;
    }
    return result;
}

SubtitleStyle::SubtitleStyle(QString styleString)
{
    QStringList parts = styleString.section(":", 1).trimmed().split(',');
    // m_name = parts.at(0);
    m_fontName = parts.at(1);
    m_fontSize = parts.at(2).toDouble();
    // Color : &HAABBGGRR
    QString originPrimary = parts.at(3);
    m_primaryColour.setAlpha(255 - originPrimary.mid(2, 2).toInt(nullptr, 16));
    m_primaryColour.setBlue(originPrimary.mid(4, 2).toInt(nullptr, 16));
    m_primaryColour.setGreen(originPrimary.mid(6, 2).toInt(nullptr, 16));
    m_primaryColour.setRed(originPrimary.mid(8, 2).toInt(nullptr, 16));

    QString originSecondary = parts.at(4);
    m_secondaryColour.setAlpha(255 - originSecondary.mid(2, 2).toInt(nullptr, 16));
    m_secondaryColour.setBlue(originSecondary.mid(4, 2).toInt(nullptr, 16));
    m_secondaryColour.setGreen(originSecondary.mid(6, 2).toInt(nullptr, 16));
    m_secondaryColour.setRed(originSecondary.mid(8, 2).toInt(nullptr, 16));

    QString originOutline = parts.at(5);
    m_outlineColour.setAlpha(255 - originOutline.mid(2, 2).toInt(nullptr, 16));
    m_outlineColour.setBlue(originOutline.mid(4, 2).toInt(nullptr, 16));
    m_outlineColour.setGreen(originOutline.mid(6, 2).toInt(nullptr, 16));
    m_outlineColour.setRed(originOutline.mid(8, 2).toInt(nullptr, 16));

    QString originBack = parts.at(6);
    m_backColour.setAlpha(255 - originBack.mid(2, 2).toInt(nullptr, 16));
    m_backColour.setBlue(originBack.mid(4, 2).toInt(nullptr, 16));
    m_backColour.setGreen(originBack.mid(6, 2).toInt(nullptr, 16));
    m_backColour.setRed(originBack.mid(8, 2).toInt(nullptr, 16));

    m_bold = (parts.at(7) == "0" ? false : true);
    m_italic = (parts.at(8) == "0" ? false : true);
    m_underline = (parts.at(9) == "0" ? false : true);
    m_strikeOut = (parts.at(10) == "0" ? false : true);
    m_scaleX = parts.at(11).toDouble();
    m_scaleY = parts.at(12).toDouble();
    m_spacing = parts.at(13).toDouble();
    m_angle = parts.at(14).toDouble();
    m_borderStyle = parts.at(15).toInt();
    m_outline = parts.at(16).toDouble();
    m_shadow = parts.at(17).toDouble();
    m_alignment = parts.at(18).toInt();
    m_marginL = parts.at(19).toInt();
    m_marginR = parts.at(20).toInt();
    m_marginV = parts.at(21).toInt();
    m_encoding = parts.at(22).toInt();
}

QString SubtitleStyle::toString(QString name) const
{
    // Color : &HAABBGGRR
    return QStringLiteral("Style: %1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16,%17,%18,%19,%20,%21,%22,%23")
        .arg(name)
        .arg(m_fontName)
        .arg(m_fontSize, 0, 'f', 2)
        .arg("&H" + QStringLiteral("%1").arg(QString::number(255 - m_primaryColour.alpha(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_primaryColour.blue(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_primaryColour.green(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_primaryColour.red(), 16), 2, '0').toUpper())
        .arg("&H" + QStringLiteral("%1").arg(QString::number(255 - m_secondaryColour.alpha(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_secondaryColour.blue(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_secondaryColour.green(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_secondaryColour.red(), 16), 2, '0').toUpper())
        .arg("&H" + QStringLiteral("%1").arg(QString::number(255 - m_outlineColour.alpha(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_outlineColour.blue(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_outlineColour.green(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_outlineColour.red(), 16), 2, '0').toUpper())
        .arg("&H" + QStringLiteral("%1").arg(QString::number(255 - m_backColour.alpha(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_backColour.blue(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_backColour.green(), 16), 2, '0').toUpper() +
             QStringLiteral("%1").arg(QString::number(m_backColour.red(), 16), 2, '0').toUpper())
        .arg(m_bold ? -1 : 0)
        .arg(m_italic ? -1 : 0)
        .arg(m_underline ? -1 : 0)
        .arg(m_strikeOut ? -1 : 0)
        .arg(m_scaleX, 0, 'f', 2)
        .arg(m_scaleY, 0, 'f', 2)
        .arg(m_spacing, 0, 'f', 2)
        .arg(m_angle, 0, 'f', 2)
        .arg(m_borderStyle)
        .arg(m_outline, 0, 'f', 2)
        .arg(m_shadow, 0, 'f', 2)
        .arg(m_alignment)
        .arg(m_marginL)
        .arg(m_marginR)
        .arg(m_marginV)
        .arg(m_encoding);
}

GenTime CommentedTime::duration() const
{
    return m_duration;
}

void CommentedTime::setDuration(const GenTime &duration)
{
    m_duration = duration;
}

bool CommentedTime::hasRange() const
{
    return m_duration.seconds() > 0;
}

GenTime CommentedTime::endTime() const
{
    return m_time + m_duration;
}
