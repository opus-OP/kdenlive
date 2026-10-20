/*
    SPDX-FileCopyrightText: 2022 Jean-Baptiste Mardelle <jb@kdenlive.org>
    SPDX-FileCopyrightText: 2022 Eric Jiang
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "catch.hpp"
#include "test_utils.hpp"
// test specific headers
#include "bin/model/subtitlestylelibrary.h"
#include "core.h"
#include "definitions.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>

using namespace fakeit;

namespace {
/** @brief Build a valid ASS V4+ style line */
QString styleLine(const QString &name, const QString &font)
{
    return QStringLiteral("Style: %1,%2,40,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100.00,100.00,0.00,0.00,1,2.00,1.00,2,10,10,20,1")
        .arg(name, font);
}

/** @brief Write a minimal ASS style set file, optionally with extra [Kdenlive Extradata] lines */
void writeSetFile(const QString &path, const QString &title, const QStringList &styleLines, const QStringList &extraData = QStringList())
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "[Script Info]\n; Kdenlive subtitle style library\nScriptType: v4.00+\n";
    if (!title.isEmpty()) {
        out << "Title: " << title << '\n';
    }
    out << "\n[Kdenlive Extradata]\n";
    for (const QString &line : extraData) {
        out << line << '\n';
    }
    out << "\n[V4+ Styles]\n";
    out << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
           "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n";
    for (const QString &line : styleLines) {
        out << line << '\n';
    }
    out << "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    file.close();
}
} // namespace

TEST_CASE("Subtitle animation data", "[Subtitles]")
{
    SECTION("toString / fromString round trip")
    {
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Scale;
        animation.durationMs = 750;
        animation.delayMs = 120;
        animation.direction = 2;
        animation.intensity = 1.5;
        animation.easeInOut = false;
        const SubtitleAnimation parsed = SubtitleAnimation::fromString(animation.toString());
        CHECK(parsed.kind == SubtitleAnimationKind::Scale);
        CHECK(parsed.durationMs == 750);
        CHECK(parsed.delayMs == 120);
        CHECK(parsed.direction == 2);
        CHECK(parsed.intensity == Approx(1.5));
        CHECK_FALSE(parsed.easeInOut);
    }

    SECTION("Tolerant parse of malformed data")
    {
        const SubtitleAnimation parsed = SubtitleAnimation::fromString(QStringLiteral("garbage;;kind=;dur=abc;delay=-40;intensity=-1"));
        CHECK_FALSE(parsed.isEnabled());
        // Invalid values fall back to the defaults
        CHECK(parsed.durationMs == 500);
        CHECK(parsed.delayMs == 0);
        CHECK(parsed.intensity == Approx(1.0));
    }

    SECTION("Fade generates a fad tag")
    {
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Fade;
        animation.durationMs = 400;
        // Event duration in milliseconds, alignment, frame size, horizontal margins, vertical margin,
        // font size, line count and estimated text width
        // A fade preset only fades in; the exit is a separate (disabled by default) animation
        CHECK(animation.toAssPrefix(20000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) == QStringLiteral("{\\an2\\fad(400,0)}"));
    }

    SECTION("Move generates a single move tag")
    {
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Move;
        animation.durationMs = 400;
        animation.direction = 0;
        // Slide in from the bottom: text height is round(1 * 60 * 1.45) = 87
        CHECK(animation.toAssPrefix(20000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) == QStringLiteral("{\\an2\\move(960,1207,960,1040,0,400)}"));
    }

    SECTION("Scale emits an explicit start value before the t tag")
    {
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Scale;
        animation.durationMs = 400;
        animation.intensity = 1.5;
        animation.easeInOut = true;
        CHECK(animation.toAssPrefix(20000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) ==
              QStringLiteral("{\\an2\\pos(960,1040)\\fscx100\\fscy100\\t(0,400,0.5,\\fscx150\\fscy150)}"));
    }

    SECTION("Rotate uses an origin and an explicit initial angle")
    {
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Rotate;
        animation.durationMs = 400;
        animation.direction = 0;
        animation.easeInOut = false;
        CHECK(animation.toAssPrefix(20000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) ==
              QStringLiteral("{\\an2\\pos(960,1040)\\org(960,1040)\\frz0\\t(0,400,1,\\frz360)}"));
    }

    SECTION("Typewriter wraps ASCII characters")
    {
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Typewriter;
        animation.durationMs = 200;
        CHECK(animation.applyToText(QStringLiteral("AB"), 2000) ==
              QStringLiteral("{\\alpha&HFF&\\t(0,50,\\alpha&H00&)}A{\\alpha&HFF&\\t(100,150,\\alpha&H00&)}B"));
    }

    SECTION("Typewriter wraps Cyrillic characters without breaking them")
    {
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Typewriter;
        animation.durationMs = 200;
        CHECK(animation.applyToText(QStringLiteral("ЖЖ"), 2000) ==
              QStringLiteral("{\\alpha&HFF&\\t(0,50,\\alpha&H00&)}Ж{\\alpha&HFF&\\t(100,150,\\alpha&H00&)}Ж"));
    }

    SECTION("Typewriter animates spaces like any other character")
    {
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Typewriter;
        animation.durationMs = 300;
        CHECK(animation.applyToText(QStringLiteral("A B"), 2000) ==
              QStringLiteral("{\\alpha&HFF&\\t(0,50,\\alpha&H00&)}A{\\alpha&HFF&\\t(100,150,\\alpha&H00&)} "
                             "{\\alpha&HFF&\\t(200,250,\\alpha&H00&)}B"));
    }

    SECTION("Typewriter keeps user override blocks and attaches them to the next character")
    {
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Typewriter;
        animation.durationMs = 200;
        CHECK(animation.applyToText(QStringLiteral("{\\b1}Hi"), 2000) ==
              QStringLiteral("{\\b1}{\\alpha&HFF&\\t(0,50,\\alpha&H00&)}H{\\alpha&HFF&\\t(100,150,\\alpha&H00&)}i"));
    }

    SECTION("Disabled and non typewriter animations leave the text unchanged")
    {
        SubtitleAnimation animation;
        CHECK(animation.applyToText(QStringLiteral("Hello"), 2000) == QStringLiteral("Hello"));
        animation.kind = SubtitleAnimationKind::Fade;
        CHECK(animation.applyToText(QStringLiteral("Hello"), 2000) == QStringLiteral("Hello"));
    }

    SECTION("Exit animation is anchored at the end of the event")
    {
        SubtitleAnimationSet animations;
        animations.out.kind = SubtitleAnimationKind::Fade;
        animations.out.durationMs = 300;
        // A fade out is the second argument of \fad and starts at the end of the 4 s event
        CHECK(animations.toAssPrefix(4000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) == QStringLiteral("{\\an2\\fad(0,300)}"));

        animations.out = SubtitleAnimation();
        animations.out.kind = SubtitleAnimationKind::Move;
        animations.out.durationMs = 500;
        animations.out.direction = 0;
        // Slide out to the bottom: starts at the anchor at 3500 ms and leaves the frame at 4000 ms
        CHECK(animations.toAssPrefix(4000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) == QStringLiteral("{\\an2\\move(960,1040,960,1207,3500,4000)}"));
    }

    SECTION("Entrance and exit animations share one event")
    {
        SubtitleAnimationSet animations;
        animations.in.kind = SubtitleAnimationKind::Fade;
        animations.in.durationMs = 400;
        animations.out.kind = SubtitleAnimationKind::Fade;
        animations.out.durationMs = 300;
        CHECK(animations.toAssPrefix(4000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) == QStringLiteral("{\\an2\\fad(400,300)}"));

        // Two slides cannot share one \move, the exit degrades to a fade
        animations.in = SubtitleAnimation();
        animations.in.kind = SubtitleAnimationKind::Move;
        animations.in.durationMs = 400;
        animations.out = SubtitleAnimation();
        animations.out.kind = SubtitleAnimationKind::Move;
        animations.out.durationMs = 400;
        const QString prefix = animations.toAssPrefix(4000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100);
        CHECK(prefix.contains(QStringLiteral("\\move(")));
        CHECK(prefix.endsWith(QStringLiteral("\\fad(0,400)}")));
    }

    SECTION("Short events shrink both animations proportionally")
    {
        SubtitleAnimationSet animations;
        animations.in.kind = SubtitleAnimationKind::Fade;
        animations.in.durationMs = 800;
        animations.out.kind = SubtitleAnimationKind::Fade;
        animations.out.durationMs = 800;
        // 1000 ms event: both fades shrink to half of the requested duration
        CHECK(animations.toAssPrefix(1000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) == QStringLiteral("{\\an2\\fad(500,500)}"));
    }

    SECTION("Erase animates characters from left to right by default")
    {
        SubtitleAnimationSet animations;
        animations.out.kind = SubtitleAnimationKind::Erase;
        animations.out.durationMs = 200;
        // The 2000 ms event erases the two characters during its last 200 ms
        CHECK(animations.applyToText(QStringLiteral("AB"), 2000) ==
              QStringLiteral("{\\alpha&H00&\\t(1800,1850,\\alpha&HFF&)}A{\\alpha&H00&\\t(1900,1950,\\alpha&HFF&)}B"));
    }

    SECTION("Erase can run right to left")
    {
        SubtitleAnimationSet animations;
        animations.out.kind = SubtitleAnimationKind::Erase;
        animations.out.durationMs = 200;
        animations.out.direction = 1;
        CHECK(animations.applyToText(QStringLiteral("AB"), 2000) ==
              QStringLiteral("{\\alpha&H00&\\t(1900,1950,\\alpha&HFF&)}A{\\alpha&H00&\\t(1800,1850,\\alpha&HFF&)}B"));
    }

    SECTION("A typewriter entering and an erase exiting are generated per character")
    {
        SubtitleAnimationSet animations;
        animations.in.kind = SubtitleAnimationKind::Typewriter;
        animations.in.durationMs = 200;
        animations.out.kind = SubtitleAnimationKind::Erase;
        animations.out.durationMs = 200;
        // No \fad is emitted, the alpha channel is driven by the per character tags
        CHECK(animations.toAssPrefix(4000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) == QStringLiteral("{\\an2}"));
        CHECK(animations.applyToText(QStringLiteral("AB"), 4000) == QStringLiteral("{\\alpha&HFF&\\t(0,50,\\alpha&H00&)\\t(3800,3850,\\alpha&HFF&)}A"
                                                                                   "{\\alpha&HFF&\\t(100,150,\\alpha&H00&)\\t(3900,3950,\\alpha&HFF&)}B"));
    }

    SECTION("A fade combined with an erase is generated per character")
    {
        SubtitleAnimationSet animations;
        animations.in.kind = SubtitleAnimationKind::Fade;
        animations.in.durationMs = 400;
        animations.out.kind = SubtitleAnimationKind::Erase;
        animations.out.durationMs = 200;
        // The entrance fade is applied to every character at once (no \fad tag is used here)
        CHECK(animations.toAssPrefix(4000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100) == QStringLiteral("{\\an2}"));
        CHECK(animations.applyToText(QStringLiteral("AB"), 4000) == QStringLiteral("{\\alpha&HFF&\\t(0,400,\\alpha&H00&)\\t(3800,3850,\\alpha&HFF&)}A"
                                                                                   "{\\alpha&HFF&\\t(0,400,\\alpha&H00&)\\t(3900,3950,\\alpha&HFF&)}B"));
    }

    SECTION("Erase falls back to a fade for texts with override blocks")
    {
        SubtitleAnimationSet animations;
        animations.out.kind = SubtitleAnimationKind::Erase;
        animations.out.durationMs = 200;
        animations = animations.resolveForText(QStringLiteral("{\\b1}Hi"));
        CHECK(animations.out.kind == SubtitleAnimationKind::Fade);
        const QString prefix = animations.toAssPrefix(4000, 2, 1920, 1080, 40, 40, 40, 60, 1, 100);
        CHECK(prefix == QStringLiteral("{\\an2\\fad(0,200)}"));
        CHECK(animations.applyToText(QStringLiteral("{\\b1}Hi"), 4000) == QStringLiteral("{\\b1}Hi"));
    }
}

TEST_CASE("Subtitle style library round trip", "[Subtitles]")
{
    QTemporaryDir builtinDir;
    QTemporaryDir userDir;
    REQUIRE(builtinDir.isValid());
    REQUIRE(userDir.isValid());
    const QStringList builtinDirs = {builtinDir.path()};
    const QString builtinPath = QDir(builtinDir.path()).absoluteFilePath(QStringLiteral("default.ass"));
    const QString userPath = QDir(userDir.path()).absoluteFilePath(QStringLiteral("user.ass"));

    SECTION("Save, reload and delete a style")
    {
        writeSetFile(builtinPath, QStringLiteral("default"), {styleLine(QStringLiteral("Builtin"), QStringLiteral("Arial"))});
        SubtitleStyleLibrary library(builtinDirs, userDir.path());
        REQUIRE(library.styleSetNames().contains(QStringLiteral("default")));
        CHECK(library.hasStyle(QStringLiteral("Builtin")));
        CHECK(library.isBuiltinStyle(QStringLiteral("Builtin")));

        const SubtitleStyle style(styleLine(QStringLiteral("MyStyle"), QStringLiteral("Courier")));
        QString error;
        REQUIRE(library.saveStyle(QStringLiteral("MyStyle"), style, &error));
        CHECK(error.isEmpty());
        CHECK(QFile::exists(userPath));
        CHECK(library.userStyles().count(QStringLiteral("MyStyle")) == 1U);
        CHECK(library.style(QStringLiteral("MyStyle")).fontName() == QStringLiteral("Courier"));
        CHECK(library.styleSetFor(QStringLiteral("MyStyle")) == QStringLiteral("user"));

        // Reloading from disk keeps the style
        library.reload();
        CHECK(library.style(QStringLiteral("MyStyle")).fontName() == QStringLiteral("Courier"));

        REQUIRE(library.deleteStyle(QStringLiteral("MyStyle"), &error));
        CHECK(library.userStyles().count(QStringLiteral("MyStyle")) == 0U);
        // Built-in styles cannot be deleted
        CHECK_FALSE(library.deleteStyle(QStringLiteral("Builtin"), &error));
        CHECK_FALSE(error.isEmpty());
        CHECK(library.hasStyle(QStringLiteral("Builtin")));
    }

    SECTION("Unknown extradata keys are preserved")
    {
        writeSetFile(builtinPath, QStringLiteral("default"), {styleLine(QStringLiteral("Builtin"), QStringLiteral("Arial"))});
        writeSetFile(userPath, QStringLiteral("user"), {styleLine(QStringLiteral("MyStyle"), QStringLiteral("Courier"))},
                     {QStringLiteral("LibraryVersion: 1"), QStringLiteral("Animation: MyStyle|kind=fade;dur=400")});
        SubtitleStyleLibrary library(builtinDirs, userDir.path());
        // Add another style to the same user set, which rewrites the file
        const SubtitleStyle style(styleLine(QStringLiteral("Other"), QStringLiteral("Verdana")));
        QString error;
        REQUIRE(library.saveStyle(QStringLiteral("Other"), style, &error));
        QFile file(userPath);
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        CHECK(content.contains(QStringLiteral("Animation: MyStyle|kind=fade;dur=400")));
        CHECK(content.contains(QStringLiteral("LibraryVersion: 1")));
        CHECK(content.contains(QStringLiteral("Style: MyStyle")));
        CHECK(content.contains(QStringLiteral("Style: Other")));
    }

    SECTION("Import and export a set")
    {
        writeSetFile(builtinPath, QStringLiteral("default"), {styleLine(QStringLiteral("Builtin"), QStringLiteral("Arial"))});
        const QString importPath = QDir(userDir.path()).absoluteFilePath(QStringLiteral("News.ass"));
        writeSetFile(importPath, QStringLiteral("News"), {styleLine(QStringLiteral("Breaking"), QStringLiteral("Verdana"))});
        SubtitleStyleLibrary library(builtinDirs, userDir.path());
        QString error;
        REQUIRE(library.importSet(importPath, &error));
        CHECK(library.hasStyle(QStringLiteral("Breaking")));
        CHECK(library.styleSetFor(QStringLiteral("Breaking")) == QStringLiteral("News"));

        const QString exportPath = QDir(userDir.path()).absoluteFilePath(QStringLiteral("exported.ass"));
        REQUIRE(library.exportSet(QStringLiteral("News"), exportPath, &error));
        QFile file(exportPath);
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        CHECK(content.contains(QStringLiteral("Style: Breaking")));
    }

    SECTION("Export styles that do not live in the library")
    {
        writeSetFile(builtinPath, QStringLiteral("default"), {styleLine(QStringLiteral("Shared"), QStringLiteral("Arial"))});
        SubtitleStyleLibrary library(builtinDirs, userDir.path());
        const QString exportPath = QDir(userDir.path()).absoluteFilePath(QStringLiteral("project-export.ass"));
        QString error;

        // Nothing to export
        CHECK_FALSE(library.exportStyles(exportPath, {}, &error));
        CHECK_FALSE(error.isEmpty());

        // A project style unknown to the library, and one overriding a shared style of the same name
        SubtitleStyle projectOnly(styleLine(QStringLiteral("ProjectOnly"), QStringLiteral("Courier")));
        SubtitleAnimation fade;
        fade.kind = SubtitleAnimationKind::Fade;
        projectOnly.setAnimation(fade);
        std::map<QString, SubtitleStyle> styles;
        styles[QStringLiteral("ProjectOnly")] = projectOnly;
        styles[QStringLiteral("Shared")] = SubtitleStyle(styleLine(QStringLiteral("Shared"), QStringLiteral("Verdana")));
        REQUIRE(library.exportStyles(exportPath, styles, &error));

        QFile file(exportPath);
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        CHECK(content.contains(QStringLiteral("Style: ProjectOnly,Courier")));
        // The exported definition is the one passed in, not the shared one
        CHECK(content.contains(QStringLiteral("Style: Shared,Verdana")));
        CHECK_FALSE(content.contains(QStringLiteral("Style: Shared,Arial")));
        CHECK(content.contains(QStringLiteral("Animation: ProjectOnly|kind=fade")));
    }

    SECTION("Animation lines are attached to styles, regenerated and malformed ones preserved")
    {
        writeSetFile(builtinPath, QStringLiteral("default"), {styleLine(QStringLiteral("Builtin"), QStringLiteral("Arial"))});
        writeSetFile(userPath, QStringLiteral("user"), {styleLine(QStringLiteral("MyStyle"), QStringLiteral("Courier"))},
                     {QStringLiteral("LibraryVersion: 1"), QStringLiteral("Animation: MyStyle|kind=fade;dur=400;delay=50;dir=0;intensity=1;ease=1"),
                      QStringLiteral("Animation: Missing|kind=move;dur=200"), QStringLiteral("Animation: MyStyle|kind=bogus"),
                      QStringLiteral("UnknownKey: keepme")});
        SubtitleStyleLibrary library(builtinDirs, userDir.path());
        // The valid animation is attached to its style
        REQUIRE(library.hasStyle(QStringLiteral("MyStyle")));
        CHECK(library.style(QStringLiteral("MyStyle")).animation().kind == SubtitleAnimationKind::Fade);
        CHECK(library.style(QStringLiteral("MyStyle")).animation().durationMs == 400);
        CHECK(library.style(QStringLiteral("MyStyle")).animation().delayMs == 50);

        // Rewrite the set and check that the animation and the malformed keys are still there
        QString error;
        REQUIRE(library.saveStyle(QStringLiteral("Other"), SubtitleStyle(styleLine(QStringLiteral("Other"), QStringLiteral("Verdana"))), &error));
        QFile file(userPath);
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        CHECK(content.contains(QStringLiteral("Animation: MyStyle|kind=fade;dur=400;delay=50;dir=0;intensity=1;ease=1")));
        CHECK(content.contains(QStringLiteral("Animation: Missing|kind=move;dur=200")));
        CHECK(content.contains(QStringLiteral("Animation: MyStyle|kind=bogus")));
        CHECK(content.contains(QStringLiteral("UnknownKey: keepme")));
    }
}

TEST_CASE("Subtitle style library cascade", "[Subtitles]")
{
    QTemporaryDir builtinDir;
    QTemporaryDir userDir;
    REQUIRE(builtinDir.isValid());
    REQUIRE(userDir.isValid());
    const QStringList builtinDirs = {builtinDir.path()};
    const QString builtinPath = QDir(builtinDir.path()).absoluteFilePath(QStringLiteral("default.ass"));
    const QString userPath = QDir(userDir.path()).absoluteFilePath(QStringLiteral("user.ass"));

    SECTION("User styles override built-in styles with the same name")
    {
        writeSetFile(builtinPath, QStringLiteral("default"),
                     {styleLine(QStringLiteral("Collide"), QStringLiteral("Arial")), styleLine(QStringLiteral("BuiltinOnly"), QStringLiteral("Arial"))});
        writeSetFile(userPath, QStringLiteral("user"), {styleLine(QStringLiteral("Collide"), QStringLiteral("Courier"))});
        SubtitleStyleLibrary library(builtinDirs, userDir.path());
        // Built-in only style resolves
        CHECK(library.style(QStringLiteral("BuiltinOnly")).fontName() == QStringLiteral("Arial"));
        CHECK(library.isBuiltinStyle(QStringLiteral("BuiltinOnly")));
        // Name collision: the user style wins in the shared resolution
        CHECK(library.hasStyle(QStringLiteral("Collide")));
        CHECK(library.style(QStringLiteral("Collide")).fontName() == QStringLiteral("Courier"));
        CHECK(library.sharedStyles().at(QStringLiteral("Collide")).fontName() == QStringLiteral("Courier"));
        CHECK(library.userStyles().at(QStringLiteral("Collide")).fontName() == QStringLiteral("Courier"));
        CHECK(library.builtinStyles().at(QStringLiteral("Collide")).fontName() == QStringLiteral("Arial"));
        CHECK_FALSE(library.isBuiltinStyle(QStringLiteral("Collide")));
        // Unknown styles are not resolved
        bool found = true;
        library.style(QStringLiteral("Missing"), &found);
        CHECK_FALSE(found);
    }

    SECTION("Deleting a user override reveals the built-in style")
    {
        writeSetFile(builtinPath, QStringLiteral("default"), {styleLine(QStringLiteral("Collide"), QStringLiteral("Arial"))});
        writeSetFile(userPath, QStringLiteral("user"), {styleLine(QStringLiteral("Collide"), QStringLiteral("Courier"))});
        SubtitleStyleLibrary library(builtinDirs, userDir.path());
        REQUIRE(library.style(QStringLiteral("Collide")).fontName() == QStringLiteral("Courier"));
        QString error;
        REQUIRE(library.deleteStyle(QStringLiteral("Collide"), &error));
        CHECK(library.isBuiltinStyle(QStringLiteral("Collide")));
        CHECK(library.style(QStringLiteral("Collide")).fontName() == QStringLiteral("Arial"));
    }
}

TEST_CASE("Read subtitle file", "[Subtitles]")
{
    // Create timeline
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);

    // Here we do some trickery to enable testing.
    // We mock the project class so that the undoStack function returns our undoStack
    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    KdenliveTests::resetNextId();
    QDir dir = QDir::temp();

    QString documentId = QString::number(QDateTime::currentMSecsSinceEpoch());
    document.setDocumentProperty(QStringLiteral("documentid"), documentId);

    // Initialize subtitle model
    std::shared_ptr<SubtitleModel> subtitleModel = timeline->createSubtitleModel();

    SECTION("Load a subtitle file")
    {
        QString subtitleFile = sourcesPath + "/dataset/01.srt";
        bool ok;
        QByteArray guessedEncoding = SubtitleModel::guessFileEncoding(subtitleFile, &ok);
        CHECK(guessedEncoding == "UTF-8");
        subtitleModel->importSubtitle(subtitleFile, 0, false, 30.00, 30.00, guessedEncoding);
        // Ensure the 3 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 3);
        QList<std::pair<std::pair<int, GenTime>, SubtitleEvent>> allSubs = subtitleModel->getAllSubtitles();
        QList<GenTime> sTime;
        QList<GenTime> controleTime;
        controleTime << GenTime(140, 25) << GenTime(265, 25) << GenTime(503, 25) << GenTime(628, 25) << GenTime(628, 25) << GenTime(875, 25);
        QStringList subtitlesText;
        QStringList control = {QStringLiteral("J'hésite à vérifier"), QStringLiteral("Ce test de sous-titres"), QStringLiteral("!! Quand même !!")};
        for (const auto &s : std::as_const(allSubs)) {
            subtitlesText << s.second.text();
            sTime << s.first.second;
            sTime << s.second.endTime();
        }
        // Ensure the texts are read correctly
        REQUIRE(subtitlesText == control);
        // Ensure timing is correct
        REQUIRE(sTime == controleTime);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    // TODO: qt6 fix
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    SECTION("Load a non-UTF-8 subtitle")
    {
        QString subtitleFile = sourcesPath + "/dataset/01-iso-8859-1.srt";
        bool ok;
        QByteArray guessedEncoding = SubtitleModel::guessFileEncoding(subtitleFile, &ok);
        qDebug() << "Guessed encoding: " << guessedEncoding;
        subtitleModel->importSubtitle(subtitleFile, 0, false, 30.00, 30.00, guessedEncoding);
        // Ensure the 3 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 3);
        QList<std::pair<std::pair<int, GenTime>, SubtitleEvent>> allSubs = subtitleModel->getAllSubtitles();
        QStringList subtitlesText;
        QStringList control = {QStringLiteral("J'hésite à vérifier"), QStringLiteral("Ce test de sous-titres"), QStringLiteral("!! Quand même !!")};
        for (const auto &s : std::as_const(allSubs)) {
            subtitlesText << s.second.text();
        }
        // Ensure that non-ASCII characters are read correctly
        CHECK(subtitlesText == control);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }
#endif

    SECTION("Load ASS file with commas")
    {
        QString subtitleFile = sourcesPath + "/dataset/subs-with-commas.ass";
        bool ok;
        QByteArray guessedEncoding = SubtitleModel::guessFileEncoding(subtitleFile, &ok);
        qDebug() << "Guessed encoding: " << guessedEncoding;
        subtitleModel->importSubtitle(subtitleFile, 0, false, 30.00, 30.00, guessedEncoding);
        // Ensure all 2 lines are loaded
        REQUIRE(subtitleModel->rowCount() == 2);
        QList<std::pair<std::pair<int, GenTime>, SubtitleEvent>> allSubs = subtitleModel->getAllSubtitles();
        QStringList subtitlesText;
        QStringList control = {QStringLiteral("Line with one comma, second part."), QStringLiteral("Line with two commas, second part, third part.")};
        for (const auto &s : std::as_const(allSubs)) {
            subtitlesText << s.second.text();
        }
        // Ensure that non-ASCII characters are read correctly
        CHECK(subtitlesText == control);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Load a broken subtitle file")
    {
        QString subtitleFile = sourcesPath + "/dataset/02.srt";
        subtitleModel->importSubtitle(subtitleFile);
        // Ensure the 2 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 2);
        QList<std::pair<std::pair<int, GenTime>, SubtitleEvent>> allSubs = subtitleModel->getAllSubtitles();
        QList<GenTime> sTime;
        QList<GenTime> controleTime;
        controleTime << GenTime(140, 25) << GenTime(265, 25) << GenTime(628, 25) << GenTime(875, 25);
        QStringList subtitlesText;
        QStringList control = {QStringLiteral("J'hésite à vérifier"), QStringLiteral("!! Quand même !!")};
        for (const auto &s : allSubs) {
            subtitlesText << s.second.text();
            sTime << s.first.second;
            sTime << s.second.endTime();
        }
        // Ensure the texts are read correctly
        REQUIRE(subtitlesText == control);
        // Ensure timing is correct
        REQUIRE(sTime == controleTime);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Preserve multiple spaces in subtitles")
    {
        QString subtitleFile = sourcesPath + "/dataset/multiple-spaces.srt";
        subtitleModel->importSubtitle(subtitleFile);
        const QList<std::pair<std::pair<int, GenTime>, SubtitleEvent>> allSubs = subtitleModel->getAllSubtitles();
        CHECK(allSubs.at(0).second.text().toStdString() == "three   spaces");
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Load SBV subtitle file")
    {
        QString subtitleFile = sourcesPath + "/dataset/01.sbv";
        subtitleModel->importSubtitle(subtitleFile);
        // Ensure the 3 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 3);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Load VTT subtitle file")
    {
        QString subtitleFile = sourcesPath + "/dataset/01.vtt";
        subtitleModel->importSubtitle(subtitleFile);
        // Ensure the 2 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 2);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Load SRT subtitle file with two dots")
    {
        QString subtitleFile = sourcesPath + "/dataset/subs-with-two-dots.srt";
        subtitleModel->importSubtitle(subtitleFile);
        // Ensure the 2 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 2);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Subtitles get whitespace removed at start and end of comment")
    {
        const QString subtitleFile = sourcesPath + "/dataset/extra-whitespace.srt";
        subtitleModel->importSubtitle(subtitleFile);

        // Add another subtitle starting with a newline, which would be invalid in an SRT
        const int subId = KdenliveTests::getNextId();
        REQUIRE(subtitleModel->addSubtitle(subId, {0, GenTime(60)},
                                           SubtitleEvent(true, GenTime(80), "Default", "", 0, 0, 0, "", QStringLiteral("\n   some more whitespaces  \n")),
                                           false, false));
        REQUIRE(subtitleModel->rowCount() == 2);

        const auto allSubs = subtitleModel->getAllSubtitles();
        CHECK(allSubs.at(0).second.text() == "surrounded by whitespace");
        CHECK(allSubs.at(1).second.text() == "some more whitespaces");

        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Comments in SRT export have no leading or trailing whitespace")
    {
        const int subId = KdenliveTests::getNextId();
        REQUIRE(subtitleModel->addSubtitle(subId, {0, GenTime(60)},
                                           SubtitleEvent(true, GenTime(80), "Default", "", 0, 0, 0, "", QStringLiteral("\n   surrounded by whitespace  \n")),
                                           false, false));
        REQUIRE(subtitleModel->rowCount() == 1);

        QTemporaryFile tmp;
        REQUIRE(tmp.open());
        tmp.close();

        subtitleModel->saveSubtitleData(subtitleModel->toJson(), tmp.fileName(), SubtitleModel::SubtitleSaveMode::Render);

        REQUIRE(tmp.open());
        CHECK(tmp.readAll().toStdString() == R"(1
00:01:00,000 --> 00:01:20,000
surrounded by whitespace

)");

        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Ensure 2 subtitles cannot be place at same frame position")
    {
        // In our current implementation, having 2 subtitles at same start time is not allowed
        int subId = KdenliveTests::getNextId();
        int subId2 = KdenliveTests::getNextId();
        int subId3 = KdenliveTests::getNextId();
        double fps = pCore->getCurrentFps();
        REQUIRE(subtitleModel->addSubtitle(subId, {0, GenTime(50, fps)},
                                           SubtitleEvent(true, GenTime(70, fps), "Default", "", 0, 0, 0, "", QStringLiteral("Hello")), false, false));
        REQUIRE(subtitleModel->addSubtitle(subId2, {0, GenTime(50, fps)},
                                           SubtitleEvent(true, GenTime(90, fps), "Default", "", 0, 0, 0, "", QStringLiteral("Hello2")), false, false) == false);
        REQUIRE(subtitleModel->addSubtitle(subId3, {0, GenTime(100, fps)},
                                           SubtitleEvent(true, GenTime(140, fps), "Default", "", 0, 0, 0, "", QStringLiteral("Second")), false, false));
        REQUIRE(subtitleModel->rowCount() == 2);
        REQUIRE(subtitleModel->moveSubtitle(subId, 0, GenTime(100, fps), false, false) == false);
        REQUIRE(subtitleModel->moveSubtitle(subId, 0, GenTime(300, fps), false, false));
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Ensure we cannot cut overlapping subtitles (it would create 2 subtitles at same frame position")
    {
        // In our current implementation, having 2 subtitles at same start time is not allowed
        int subId = KdenliveTests::getNextId();
        int subId2 = KdenliveTests::getNextId();
        double fps = pCore->getCurrentFps();
        REQUIRE(subtitleModel->addSubtitle(subId, {0, GenTime(50, fps)},
                                           SubtitleEvent(true, GenTime(70, fps), "Default", "", 0, 0, 0, "", QStringLiteral("Hello")), false, false));
        REQUIRE(subtitleModel->addSubtitle(subId2, {0, GenTime(60, fps)},
                                           SubtitleEvent(true, GenTime(90, fps), "Default", "", 0, 0, 0, "", QStringLiteral("Hello2")), false, false));
        REQUIRE(subtitleModel->rowCount() == 2);
        REQUIRE(timeline->requestClipsGroup({subId, subId2}));
        REQUIRE_FALSE(TimelineFunctions::requestClipCut(timeline, subId, 65));
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Read start/end time of the subtitles")
    {
        // srt
        QString subtitleFile = sourcesPath + "/dataset/01.srt";
        subtitleModel->importSubtitle(subtitleFile);
        // Ensure the 3 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 3);
        QList<std::pair<std::pair<int, GenTime>, SubtitleEvent>> allSubs = subtitleModel->getAllSubtitles();
        REQUIRE(allSubs.at(0).first.second.ms() - 5600 < 40);
        REQUIRE(allSubs.at(0).second.endTime().ms() - 10600 < 40);
        REQUIRE(allSubs.at(1).first.second.ms() - 20120 < 40);
        REQUIRE(allSubs.at(1).second.endTime().ms() - 25120 < 40);
        REQUIRE(allSubs.at(2).first.second.ms() - 25120 < 40);
        REQUIRE(allSubs.at(2).second.endTime().ms() - 35000 < 40);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);

        // sbv
        subtitleFile = sourcesPath + "/dataset/01.sbv";
        subtitleModel->importSubtitle(subtitleFile);
        // Ensure the 3 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 3);
        allSubs = subtitleModel->getAllSubtitles();
        REQUIRE(allSubs.at(0).first.second.ms() - 150000 < 40);
        REQUIRE(allSubs.at(0).second.endTime().ms() - 286000 < 40);
        REQUIRE(allSubs.at(1).first.second.ms() - 342000 < 40);
        REQUIRE(allSubs.at(1).second.endTime().ms() - 353000 < 40);
        REQUIRE(allSubs.at(2).first.second.ms() - 411000 < 40);
        REQUIRE(allSubs.at(2).first.second.ms() - 415000 < 40);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);

        // vtt
        subtitleFile = sourcesPath + "/dataset/01.vtt";
        subtitleModel->importSubtitle(subtitleFile);
        // Ensure the 2 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 2);
        allSubs = subtitleModel->getAllSubtitles();
        REQUIRE(allSubs.at(0).first.second.ms() - 600 < 40);
        REQUIRE(allSubs.at(0).second.endTime().ms() - 2000 < 40);
        REQUIRE(allSubs.at(1).first.second.ms() - 2400 < 40);
        REQUIRE(allSubs.at(1).second.endTime().ms() - 4400 < 40);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);

        // ASS
        subtitleFile = sourcesPath + "/dataset/subs-with-commas.ass";
        subtitleModel->importSubtitle(subtitleFile);
        // Ensure the 2 dialogues are loaded
        REQUIRE(subtitleModel->rowCount() == 2);
        allSubs = subtitleModel->getAllSubtitles();
        REQUIRE(allSubs.at(0).first.second.ms() - 580 < 40);
        REQUIRE(allSubs.at(0).second.endTime().ms() - 2980 < 40);
        REQUIRE(allSubs.at(1).first.second.ms() - 4330 < 40);
        REQUIRE(allSubs.at(1).second.endTime().ms() - 9100 < 40);
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Project subtitle style animations round-trip through JSON")
    {
        SubtitleStyle style(styleLine(QStringLiteral("Animated"), QStringLiteral("Arial")));
        SubtitleAnimation animation;
        animation.kind = SubtitleAnimationKind::Fade;
        animation.durationMs = 400;
        style.setAnimation(animation);
        subtitleModel->setSubtitleStyle(QStringLiteral("Animated"), style, true);

        const QString json = subtitleModel->globalStylesToJson();
        CHECK(json.contains(QStringLiteral("\"animation\"")));
        CHECK(json.contains(QStringLiteral("kind=fade;dur=400")));

        // Read the serialized value back through the project document
        document.setSequenceProperty(timeline->uuid(), QStringLiteral("globalSubtitleStyles"), json);
        const std::map<QString, SubtitleStyle> styles = document.globalSubtitleStyles(timeline->uuid());
        REQUIRE(styles.count(QStringLiteral("Animated")) == 1U);
        CHECK(styles.at(QStringLiteral("Animated")).animation().kind == SubtitleAnimationKind::Fade);
        CHECK(styles.at(QStringLiteral("Animated")).animation().durationMs == 400);

        // A legacy entry without an animation field loads as disabled
        QJsonObject oldEntry;
        oldEntry.insert(QStringLiteral("style"), styleLine(QStringLiteral("Old"), QStringLiteral("Arial")));
        QJsonArray oldArray;
        oldArray.push_back(oldEntry);
        document.setSequenceProperty(timeline->uuid(), QStringLiteral("globalSubtitleStyles"), QString::fromUtf8(QJsonDocument(oldArray).toJson()));
        const std::map<QString, SubtitleStyle> oldStyles = document.globalSubtitleStyles(timeline->uuid());
        REQUIRE(oldStyles.count(QStringLiteral("Old")) == 1U);
        CHECK_FALSE(oldStyles.at(QStringLiteral("Old")).animation().isEnabled());
    }

    SECTION("Style animations are injected into exported ASS files")
    {
        SubtitleStyle fadeStyle(styleLine(QStringLiteral("FadeStyle"), QStringLiteral("Arial")));
        SubtitleAnimation fade;
        fade.kind = SubtitleAnimationKind::Fade;
        fade.durationMs = 400;
        fadeStyle.setAnimation(fade);
        subtitleModel->setSubtitleStyle(QStringLiteral("FadeStyle"), fadeStyle, false);
        const int fadeId = KdenliveTests::getNextId();
        REQUIRE(subtitleModel->addSubtitle(
            fadeId, {0, GenTime(60)}, SubtitleEvent(true, GenTime(80), QStringLiteral("FadeStyle"), QString(), 0, 0, 0, QString(), QStringLiteral("Hello")),
            false, false));
        REQUIRE(subtitleModel->rowCount() == 1);

        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString assPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("out.ass"));
        subtitleModel->saveSubtitleData(subtitleModel->toJson(), assPath, SubtitleModel::SubtitleSaveMode::Render);
        QFile file(assPath);
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        // The fade tag is prepended to the text and the animation is stored in the Extradata section
        CHECK(content.contains(QStringLiteral("\\fad(400,0)")));
        CHECK(content.contains(QStringLiteral("Animation: FadeStyle|kind=fade;dur=400")));
        // The initial value is present so libass has something to interpolate from
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Style animations are injected for slides")
    {
        SubtitleStyle moveStyle(styleLine(QStringLiteral("MoveStyle"), QStringLiteral("Arial")));
        SubtitleAnimation move;
        move.kind = SubtitleAnimationKind::Move;
        move.durationMs = 500;
        move.direction = 1;
        moveStyle.setAnimation(move);
        subtitleModel->setSubtitleStyle(QStringLiteral("MoveStyle"), moveStyle, false);
        const int moveId = KdenliveTests::getNextId();
        REQUIRE(subtitleModel->addSubtitle(
            moveId, {0, GenTime(60)}, SubtitleEvent(true, GenTime(80), QStringLiteral("MoveStyle"), QString(), 0, 0, 0, QString(), QStringLiteral("Hello")),
            false, false));

        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString assPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("out.ass"));
        subtitleModel->saveSubtitleData(subtitleModel->toJson(), assPath, SubtitleModel::SubtitleSaveMode::Render);
        QFile file(assPath);
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        CHECK(content.contains(QStringLiteral("\\move(")));
        CHECK_FALSE(content.contains(QStringLiteral("\\pos(")));
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Events with an effect keep no position animation or typewriter")
    {
        SubtitleStyle moveStyle(styleLine(QStringLiteral("MoveStyle"), QStringLiteral("Arial")));
        SubtitleAnimation move;
        move.kind = SubtitleAnimationKind::Move;
        move.durationMs = 500;
        moveStyle.setAnimation(move);
        subtitleModel->setSubtitleStyle(QStringLiteral("MoveStyle"), moveStyle, false);

        SubtitleStyle typeStyle(styleLine(QStringLiteral("TypeStyle"), QStringLiteral("Arial")));
        SubtitleAnimation type;
        type.kind = SubtitleAnimationKind::Typewriter;
        type.durationMs = 500;
        typeStyle.setAnimation(type);
        subtitleModel->setSubtitleStyle(QStringLiteral("TypeStyle"), typeStyle, false);

        const int moveId = KdenliveTests::getNextId();
        REQUIRE(subtitleModel->addSubtitle(
            moveId, {0, GenTime(60)},
            SubtitleEvent(true, GenTime(80), QStringLiteral("MoveStyle"), QString(), 0, 0, 0, QStringLiteral("Scroll up;5;10;20"), QStringLiteral("Hello")),
            false, false));
        const int typeId = KdenliveTests::getNextId();
        REQUIRE(subtitleModel->addSubtitle(
            typeId, {0, GenTime(100)},
            SubtitleEvent(true, GenTime(120), QStringLiteral("TypeStyle"), QString(), 0, 0, 0, QStringLiteral("Banner;5;0;0;20"), QStringLiteral("Hello")),
            false, false));
        REQUIRE(subtitleModel->rowCount() == 2);

        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString assPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("out.ass"));
        subtitleModel->saveSubtitleData(subtitleModel->toJson(), assPath, SubtitleModel::SubtitleSaveMode::Render);
        QFile file(assPath);
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        // The effect column overrides the move/pos animations and the typewriter
        CHECK_FALSE(content.contains(QStringLiteral("\\move(")));
        CHECK_FALSE(content.contains(QStringLiteral("\\alpha&HFF&")));
        CHECK(content.contains(QStringLiteral("Scroll up;5;10;20")));
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("Exit animations are injected and stored separately")
    {
        SubtitleStyle style(styleLine(QStringLiteral("OutStyle"), QStringLiteral("Arial")));
        SubtitleAnimation fadeIn;
        fadeIn.kind = SubtitleAnimationKind::Fade;
        fadeIn.durationMs = 300;
        style.setAnimation(fadeIn);
        SubtitleAnimation fadeOut;
        fadeOut.kind = SubtitleAnimationKind::Fade;
        fadeOut.durationMs = 500;
        style.setAnimationOut(fadeOut);
        subtitleModel->setSubtitleStyle(QStringLiteral("OutStyle"), style, false);
        const int id = KdenliveTests::getNextId();
        REQUIRE(subtitleModel->addSubtitle(id, {0, GenTime(60)},
                                           SubtitleEvent(true, GenTime(80), QStringLiteral("OutStyle"), QString(), 0, 0, 0, QString(), QStringLiteral("Hello")),
                                           false, false));
        REQUIRE(subtitleModel->rowCount() == 1);

        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString assPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("out.ass"));
        subtitleModel->saveSubtitleData(subtitleModel->toJson(), assPath, SubtitleModel::SubtitleSaveMode::Render);
        QFile file(assPath);
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        CHECK(content.contains(QStringLiteral("\\fad(300,500)")));
        CHECK(content.contains(QStringLiteral("AnimationOut: OutStyle|kind=fade;dur=500")));
        // The editing file must not carry the injected tags
        const QString editPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("edit.ass"));
        subtitleModel->saveSubtitleData(subtitleModel->toJson(), editPath, SubtitleModel::SubtitleSaveMode::Editing);
        QFile editFile(editPath);
        REQUIRE(editFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString editContent = QString::fromUtf8(editFile.readAll());
        editFile.close();
        CHECK_FALSE(editContent.contains(QStringLiteral("\\fad(300,500)")));
        // The exit animation itself survives in the editing file
        CHECK(editContent.contains(QStringLiteral("AnimationOut: OutStyle|kind=fade;dur=500")));
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    SECTION("The editing file stays clean while the filter reads the render file")
    {
        CHECK(subtitleModel->styleLibrary() == SubtitleStyleLibrary::instance());

        SubtitleStyle style(styleLine(QStringLiteral("Anim"), QStringLiteral("Arial")));
        SubtitleAnimation fadeIn;
        fadeIn.kind = SubtitleAnimationKind::Fade;
        fadeIn.durationMs = 300;
        style.setAnimation(fadeIn);
        subtitleModel->setSubtitleStyle(QStringLiteral("Anim"), style, false);
        const int id = KdenliveTests::getNextId();
        REQUIRE(subtitleModel->addSubtitle(id, {0, GenTime(60)},
                                           SubtitleEvent(true, GenTime(80), QStringLiteral("Anim"), QString(), 0, 0, 0, QString(), QStringLiteral("Hello")),
                                           false, false));
        subtitleModel->jsontoSubtitle(subtitleModel->toJson());

        // The filter reads the render file, which carries the injected tags...
        const QString renderPath = subtitleModel->getUrl();
        REQUIRE(QFile::exists(renderPath));
        QFile renderFile(renderPath);
        REQUIRE(renderFile.open(QIODevice::ReadOnly | QIODevice::Text));
        CHECK(QString::fromUtf8(renderFile.readAll()).contains(QStringLiteral("\\fad(300,0)")));
        renderFile.close();

        // ...while the file handed out to the user (export) is the plain editing file
        const QString editingPath = subtitleModel->editingFilePath();
        REQUIRE(QFile::exists(editingPath));
        CHECK(editingPath != renderPath);
        QFile editingFile(editingPath);
        REQUIRE(editingFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString editingContent = QString::fromUtf8(editingFile.readAll());
        editingFile.close();
        CHECK_FALSE(editingContent.contains(QStringLiteral("\\fad(")));
        CHECK(editingContent.contains(QStringLiteral("Animation: Anim|kind=fade;dur=300")));
        subtitleModel->removeAllSubtitles();
        REQUIRE(subtitleModel->rowCount() == 0);
    }

    binModel->clean();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
