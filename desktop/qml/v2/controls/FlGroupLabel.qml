import QtQuick
import DFEE

// Subgroup caption inside a section ("Grain", "Halation"): 11/600, caption color.
Text {
    width: parent ? parent.width : 0
    topPadding: 4
    color: Theme.textCaption
    font.pixelSize: Theme.fontCaption
    font.weight: Font.DemiBold
}
