import QtQuick
import QtQuick.Controls
import DFEE

ApplicationWindow {
    id: root
    objectName: "v2Root"
    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: "Film Lab"
    color: Theme.window
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody

    // Focus model (see desktop/DESIGN.md): popups hand focus back to a plain Item,
    // never to contentItem (a focus scope that returns it to its last child).
    function returnFocus() { keySink.forceActiveFocus(); }
    Item { id: keySink }
    // True while a text field has focus: bare-key shortcuts stay off.
    property bool textEntry: false
}
