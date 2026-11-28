// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Swiftify

ApplicationWindow {
    id: window

    required property YouTubeController music
    property bool privacyAccepted: false

    function withPrivacyConsent(action) {
        if (privacyAccepted) {
            action()
        } else {
            privacyDialog.nextAction = action
            privacyDialog.open()
        }
    }

    function submitSearch(term: string) {
        if (!term.trim()) return
        if (music.canSearch) {
            withPrivacyConsent(() => music.search(term))
        } else {
            music.search(term)
        }
    }

    title: "Swiftify"
    width: 1040
    height: 700
    minimumWidth: 565
    minimumHeight: 465
    visible: true
    color: Appearance.colors.layer0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            TitleText {
                text: "Swiftify"
                level: "display"
                Layout.fillWidth: true
            }

            TextButton {
                visible: window.music.oauthConfigured
                enabled: !window.music.accountBusy || window.music.signingIn
                variant: TextButton.Tonal
                symbol: window.music.signedIn ? "logout" : "account_circle"
                text: window.music.signingIn ? qsTr("Cancel sign-in")
                    : window.music.signedIn ? qsTr("Disconnect Google")
                    : qsTr("Sign in with Google")
                onClicked: {
                    if (window.music.signingIn) {
                        window.music.cancelSignIn()
                    } else if (window.music.signedIn) {
                        window.music.signOut()
                    } else {
                        window.withPrivacyConsent(() => signInDialog.open())
                    }
                }
            }
        }

        StyledText {
            Layout.fillWidth: true
            visible: window.music.channelName.length > 0
            text: qsTr("Connected YouTube channel: %1").arg(window.music.channelName)
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Appearance.colors.onSurfaceVariant
        }

        SegmentedControl {
            Layout.alignment: Qt.AlignLeft
            currentIndex: window.music.view === YouTubeController.Search ? 0
                : window.music.view === YouTubeController.Likes ? 1 : 2
            segments: [
                Segment { text: qsTr("Music videos"); symbol: "search" },
                Segment {
                    text: qsTr("Liked music")
                    symbol: "favorite"
                    enabled: window.music.channelName.length > 0
                },
                Segment {
                    text: qsTr("Playlists")
                    symbol: "queue_music"
                    enabled: window.music.channelName.length > 0
                }
            ]
            onActivated: index => {
                if (index === 0) {
                    window.music.showSearch()
                } else if (index === 1) {
                    window.music.showLikes()
                } else {
                    window.music.showPlaylists()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Appearance.colors.surfaceContainerLow
            radius: Appearance.rounding.veryLarge

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    visible: window.music.view === YouTubeController.Search
                    spacing: 9

                    SearchField {
                        id: searchField
                        objectName: "youtubeSearchField"
                        Layout.fillWidth: true
                        placeholder: qsTr("Search music videos")
                        onAccepted: window.submitSearch(text)
                    }

                    TextButton {
                        objectName: "youtubeSearchButton"
                        text: window.music.canSearch ? qsTr("Search") : qsTr("Search in browser")
                        enabled: searchField.text.trim().length > 0
                        onClicked: window.submitSearch(searchField.text)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: window.music.view === YouTubeController.PlaylistContents

                    IconButton {
                        symbol: "arrow_back"
                        Accessible.name: qsTr("Back to playlists")
                        onClicked: window.music.showPlaylists()
                    }

                    TitleText {
                        text: window.music.playlistTitle
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    TextButton {
                        text: qsTr("Open on YouTube")
                        symbol: "open_in_new"
                        variant: TextButton.Tonal
                        onClicked: window.music.openPlaylist()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: window.music.accountError.length > 0

                    StyledText {
                        Layout.fillWidth: true
                        text: window.music.accountError
                        wrapMode: Text.Wrap
                        color: Appearance.colors.error
                    }

                    TextButton {
                        visible: window.music.signedIn && !window.music.channelName
                        text: qsTr("Retry")
                        variant: TextButton.Tonal
                        onClicked: window.music.retryChannel()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: window.music.error.length > 0

                    StyledText {
                        Layout.fillWidth: true
                        text: window.music.error
                        wrapMode: Text.Wrap
                        color: Appearance.colors.error
                    }

                    TextButton {
                        visible: window.music.searched
                        text: qsTr("Retry")
                        variant: TextButton.Tonal
                        onClicked: window.music.retry()
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: results
                        objectName: "youtubeResults"
                        anchors.fill: parent
                        clip: true
                        spacing: 5
                        model: window.music.results
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: StyledScrollBar {}

                        delegate: Rectangle {
                            id: entry

                            required property string itemId
                            required property string itemTitle
                            required property string channel
                            required property url artwork
                            readonly property bool playlist: window.music.view === YouTubeController.Playlists

                            width: results.width - 14
                            height: 74
                            color: Appearance.colors.surfaceContainerHigh
                            radius: Appearance.rounding.normal

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 12

                                Rectangle {
                                    Layout.preferredWidth: 72
                                    Layout.preferredHeight: 52
                                    color: Appearance.colors.surfaceContainerHighest
                                    radius: Appearance.rounding.small
                                    clip: true

                                    MaterialSymbol {
                                        anchors.centerIn: parent
                                        text: entry.playlist ? "queue_music" : "music_note"
                                        color: Appearance.colors.onSurfaceVariant
                                        iconSize: 22
                                    }

                                    Image {
                                        anchors.fill: parent
                                        source: entry.artwork
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: false
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    StyledText {
                                        Layout.fillWidth: true
                                        text: entry.itemTitle
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.weight: Font.DemiBold
                                    }

                                    StyledText {
                                        Layout.fillWidth: true
                                        visible: entry.channel.length > 0
                                        text: entry.channel
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        color: Appearance.colors.onSurfaceVariant
                                        font.pixelSize: Appearance.fontSize.smallie
                                    }
                                }

                                TextButton {
                                    text: entry.playlist ? qsTr("Browse") : qsTr("Play in browser")
                                    Accessible.name: text + " " + entry.itemTitle
                                    symbol: entry.playlist ? "chevron_right" : "open_in_new"
                                    variant: TextButton.Tonal
                                    onClicked: {
                                        if (entry.playlist) {
                                            window.music.showPlaylist(entry.itemId, entry.itemTitle)
                                        } else {
                                            window.music.play(entry.itemId)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    LoadingSpinner {
                        anchors.centerIn: parent
                        visible: window.music.loading && results.count === 0
                    }

                    StyledText {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 24, 390)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Appearance.colors.onSurfaceVariant
                        visible: !window.music.loading && !window.music.error && results.count === 0
                        text: window.music.searched
                            ? window.music.hasMore ? qsTr("No music videos on this page.") : qsTr("No results.")
                            : window.music.canSearch ? qsTr("Search for an artist or song.")
                            : qsTr("Search opens YouTube Music in your default browser.")
                    }
                }

                TextButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: window.music.hasMore
                    enabled: !window.music.loading
                    busy: window.music.loading
                    text: qsTr("Load more")
                    variant: TextButton.Tonal
                    onClicked: window.music.loadMore()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                id: brandButton

                implicitWidth: 240
                implicitHeight: 44
                padding: 0
                Accessible.name: qsTr("Developed with YouTube. Open YouTube.")
                onClicked: window.music.openSource()
                background: Rectangle {
                    radius: Appearance.rounding.small
                    color: brandButton.visualFocus || brandButton.hovered
                        ? Appearance.colors.surfaceContainerHigh : "transparent"
                }
                contentItem: Item {
                    Image {
                        anchors.centerIn: parent
                        width: parent.width
                        // Scale the original 700x250 asset with its transparent margin.
                        height: width * 250 / 700
                        source: Theme.dark
                            ? "qrc:/qt/qml/Swiftify/resources/youtube/developed-with-youtube-light.png"
                            : "qrc:/qt/qml/Swiftify/resources/youtube/developed-with-youtube-dark.png"
                        fillMode: Image.PreserveAspectFit
                    }
                }
            }

            Item { Layout.fillWidth: true }

            TextButton {
                text: qsTr("Privacy and terms")
                variant: TextButton.Text
                onClicked: privacyDialog.open()
            }
        }
    }

    StyledDialog {
        id: signInDialog
        headline: qsTr("Connect your Google account")
        body: qsTr("Swiftify requests read-only access to your YouTube likes and playlists. Google sign-in opens in your browser.")
        buttons: [
            TextButton {
                text: qsTr("Cancel")
                variant: TextButton.Text
                onClicked: signInDialog.close()
            },
            TextButton {
                text: qsTr("Continue")
                onClicked: {
                    signInDialog.accept()
                    window.music.signIn()
                }
            }
        ]
    }

    StyledDialog {
        id: privacyDialog

        property var nextAction: null

        preferredWidth: 520
        onRejected: nextAction = null
        headline: qsTr("Privacy and terms")
        body: qsTr("By using Swiftify's YouTube features you agree to the YouTube Terms of Service. Swiftify sends search terms to YouTube. When you connect Google, Swiftify reads your channel name, likes, and playlists to display them. Credentials and results stay in memory until the session ends. Swiftify shares this data with Google and YouTube to make API requests. Disconnect Google to clear account data and request revocation.")
        TextButton {
            text: qsTr("YouTube Terms of Service")
            variant: TextButton.Text
            onClicked: window.music.openYouTubeTerms()
        }
        TextButton {
            text: qsTr("Google Privacy Policy")
            variant: TextButton.Text
            onClicked: window.music.openGooglePrivacy()
        }
        TextButton {
            text: qsTr("Manage Google app access")
            variant: TextButton.Text
            onClicked: window.music.openGoogleConnections()
        }
        TextButton {
            text: qsTr("Contact the maintainer")
            variant: TextButton.Text
            onClicked: Qt.openUrlExternally("https://github.com/kazaminosuke")
        }
        buttons: [
            TextButton {
                text: privacyDialog.nextAction ? qsTr("Cancel") : qsTr("Done")
                variant: TextButton.Text
                onClicked: privacyDialog.close()
            },
            TextButton {
                objectName: "youtubePrivacyAgree"
                text: qsTr("Agree and continue")
                visible: privacyDialog.nextAction !== null
                onClicked: {
                    const action = privacyDialog.nextAction
                    privacyDialog.nextAction = null
                    window.privacyAccepted = true
                    privacyDialog.accept()
                    if (action) action()
                }
            }
        ]
    }
}
