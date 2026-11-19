// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Fonts.h"
#include "app/Settings.h"
#include "theme/Theme.h"
#include "youtube/YouTubeController.h"

#include <QDesktopServices>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(SwiftifyPlugin)

using namespace swiftify;

class YouTubeViewTest : public QObject {
    Q_OBJECT

private slots:
    void captureUrl(const QUrl &url) { m_openedUrl = url; }
    void searchAndPlaybackUseTheOfficialBrowser();

private:
    QUrl m_openedUrl;
};

void YouTubeViewTest::searchAndPlaybackUseTheOfficialBrowser()
{
    qunsetenv("SWIFTIFY_YOUTUBE_API_KEY");
    qunsetenv("SWIFTIFY_GOOGLE_CLIENT_ID");
    qunsetenv("SWIFTIFY_GOOGLE_CLIENT_SECRET");
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    fonts::loadIconFont();
    fonts::loadInterfaceFont();
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "captureUrl");
    const auto cleanup = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("https")); });

    QTemporaryDir config;
    QVERIFY(config.isValid());
    Settings settings(config.filePath(QStringLiteral("swiftify.ini")));
    Theme theme(&settings);
    youtube::YouTubeController music;
    QQmlApplicationEngine engine;
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    engine.setInitialProperties({{QStringLiteral("music"), QVariant::fromValue(&music)}});
    engine.loadFromModule("Swiftify", "YouTubeWindow");

    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *field = window->findChild<QObject *>(QStringLiteral("youtubeSearchField"));
    auto *button = window->findChild<QObject *>(QStringLiteral("youtubeSearchButton"));
    QVERIFY(field);
    QVERIFY(button);
    QVERIFY(!button->property("enabled").toBool());
    const QString query = QStringLiteral("a+b & 日本語");
    QVERIFY(field->setProperty("text", query));
    QVERIFY(button->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QCOMPARE(m_openedUrl.host(), QStringLiteral("music.youtube.com"));
    QCOMPARE(m_openedUrl.path(), QStringLiteral("/search"));
    QCOMPARE(QUrlQuery(m_openedUrl).queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded), query);
    QVERIFY(m_openedUrl.toEncoded().contains("q=a%2Bb"));

    music.play(QStringLiteral("dQw4w9WgXcQ"));
    QCOMPARE(m_openedUrl.toString(), QStringLiteral("https://music.youtube.com/watch?v=dQw4w9WgXcQ"));
    music.play(QStringLiteral("invalid"));
    QCOMPARE(m_openedUrl.path(), QStringLiteral("/watch"));
    QCoreApplication::processEvents();
    QCOMPARE(warnings.size(), 0);
}

QTEST_MAIN(YouTubeViewTest)
#include "tst_youtubeview.moc"
