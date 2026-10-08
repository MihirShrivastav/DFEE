import QtQuick
import QtQuick.Controls
import DFEE

// Menu divider: a hairline with breathing room, not the style's bright default.
MenuSeparator {
    topPadding: 4
    bottomPadding: 4
    leftPadding: 8
    rightPadding: 8
    contentItem: Rectangle {
        implicitHeight: 1
        color: Theme.hairline
    }
}
