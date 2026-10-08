import QtQuick
import QtQuick.Controls
import QtCore
import DFEE

// Right-hand inspector: the pinned scope, then the sections in darkroom order in one
// scrolling column. Open/closed sections and the scope mode persist (QtCore Settings,
// category "inspector"; UI tests point `location` at a throwaway ini).
Rectangle {
    id: insp
    objectName: "inspector"
    color: Theme.panel

    Settings {
        id: prefs
        category: "inspector"
        location: uiSettingsLocation
        property bool filmOpen: true
        property bool exposureOpen: true
        property bool toneOpen: true
        property bool colorOpen: false
        property bool grainOpen: false
        property bool printOpen: false
        property bool fineTuneOpen: false
        property string scopeMode: "histogram"
    }

    Item {
        id: scopeBox
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: scope.height + 28
        FlScope {
            id: scope
            objectName: "scope"
            x: 16
            y: 14
            width: parent.width - 32
            mode: prefs.scopeMode
            onSwitchRequested: prefs.scopeMode = prefs.scopeMode === "histogram" ? "vectorscope" : "histogram"
        }
        FlHairline { anchors.bottom: parent.bottom }
    }

    Flickable {
        id: flick
        objectName: "inspectorFlick"
        anchors.top: scopeBox.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: sections.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: FlScrollBar {}
        Column {
            id: sections
            width: flick.width
            FlFilmSection {
                objectName: "filmSection"
                open: prefs.filmOpen
                onToggled: prefs.filmOpen = !prefs.filmOpen
            }
            FlExposureSection {
                objectName: "exposureSection"
                open: prefs.exposureOpen
                onToggled: prefs.exposureOpen = !prefs.exposureOpen
            }
            FlToneSection {
                objectName: "toneSection"
                open: prefs.toneOpen
                onToggled: prefs.toneOpen = !prefs.toneOpen
            }
            FlColorSection {
                objectName: "colorSection"
                open: prefs.colorOpen
                onToggled: prefs.colorOpen = !prefs.colorOpen
            }
            FlGrainLightSection {
                objectName: "grainSection"
                open: prefs.grainOpen
                onToggled: prefs.grainOpen = !prefs.grainOpen
            }
            FlPrintSection {
                objectName: "printSection"
                open: prefs.printOpen
                onToggled: prefs.printOpen = !prefs.printOpen
            }
            FlFineTuneSection {
                objectName: "fineTuneSection"
                open: prefs.fineTuneOpen
                onToggled: prefs.fineTuneOpen = !prefs.fineTuneOpen
            }
        }
    }
    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.hairline }
}
