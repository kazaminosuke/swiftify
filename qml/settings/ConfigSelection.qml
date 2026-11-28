/*
 * Swiftify, a spotify player for desktop
 * Copyright (C) 2026  shred0s
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

pragma ComponentBehavior: Bound
import QtQuick
import Swiftify

// a setting that is one of a few choices: its name, then the choices as
// segmented buttons (end4's ConfigSelectionArray).
Item {
    id: root

    property string text
    property alias segments: choices.segments
    property alias currentIndex: choices.currentIndex
    property alias segmentPadding: choices.horizontalPadding
    property bool stackWhenNarrow: false
    readonly property bool stacked: stackWhenNarrow && width < label.implicitWidth + choices.implicitWidth + 30
    signal activated(int index)

    width: parent ? parent.width : 0
    implicitHeight: Math.max(52, choices.implicitHeight + 12 + (stacked ? label.implicitHeight + 6 : 0))
    opacity: enabled ? 1 : 0.38

    StyledText {
        id: label
        objectName: "selectionLabel"
        x: 12
        anchors.right: root.stacked ? parent.right : choices.left
        anchors.rightMargin: 12
        y: root.stacked ? 6 : (root.height - height) / 2
        text: root.text
        elide: Text.ElideRight
    }

    SegmentedControl {
        id: choices
        anchors.right: parent.right
        anchors.rightMargin: 6
        y: root.stacked ? label.y + label.height + 6 : (root.height - height) / 2
        segmentHeight: 32
        enabled: root.enabled
        onActivated: (index) => root.activated(index)
    }
}
