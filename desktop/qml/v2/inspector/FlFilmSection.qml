import QtQuick
import DFEE

// Film: the stock card (box art, name, "type · ISO", chevron) that opens the stock
// picker, the stock's one-line look, and the strength of its tone curve.
FlInspectorSection {
    id: sec
    title: "Film"
    group: "film"
    readonly property var stockInfo: {
        const m = engine.stockModel;
        for (let i = 0; i < m.length; ++i) if (m[i].id === engine.stock) return m[i];
        return { id: "none", name: "None", typeLabel: "", iso: 0, blurb: "" };
    }
    readonly property bool hasFilm: stockInfo.id !== "none"
    summary: hasFilm ? stockInfo.name : "No film"

    Rectangle {
        id: card
        objectName: "filmCard"
        width: parent.width
        height: 60
        radius: Theme.radiusCard
        color: cardHover.hovered ? "#303033" : Theme.card
        border.width: 1
        border.color: "#0dffffff"
        HoverHandler { id: cardHover }
        MouseArea { anchors.fill: parent; onClicked: picker.open() }
        Rectangle {
            id: art
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 40
            height: 40
            radius: 5
            color: Theme.inset
            clip: true
            Image {
                anchors.fill: parent
                visible: sec.hasFilm
                source: sec.hasFilm ? "qrc:/boxart/" + sec.stockInfo.id + ".svg" : ""
                sourceSize: Qt.size(80, 80)
                fillMode: Image.PreserveAspectCrop
            }
            FlIcon {
                anchors.centerIn: parent
                visible: !sec.hasFilm
                name: "film-strip"
                size: 18
                color: Theme.textTertiary
            }
        }
        Column {
            anchors.left: art.right
            anchors.leftMargin: 12
            anchors.right: chevron.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                objectName: "filmCardName"
                width: parent.width
                text: sec.hasFilm ? sec.stockInfo.name : "No film"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: sec.hasFilm
                    ? sec.stockInfo.typeLabel + (sec.stockInfo.iso > 0 ? " · ISO " + sec.stockInfo.iso : "")
                    : "Choose a film stock"
                color: Theme.textCaption
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
        FlIcon {
            id: chevron
            name: "caret-right"
            size: 12
            color: Theme.textTertiary
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
        }
        FlListPopup {
            id: picker
            objectName: "stockPicker"
            rowPrefix: "stock_"
            y: card.height + 4
            currentId: engine.stock
            rows: engine.stockModel.map(s => ({
                id: s.id,
                name: s.name,
                group: s.id === "none" ? "" : (s.groupLabel || ""),
                detail: "",                     // names only: the card shows type, ISO and look
                art: s.id === "none" ? "" : "qrc:/boxart/" + s.id + ".svg"
            }))
            onPicked: (id) => engine.stock = id
        }
    }
    Text {
        visible: sec.hasFilm && (sec.stockInfo.blurb || "").length > 0
        width: parent.width
        text: sec.stockInfo.blurb || ""
        wrapMode: Text.WordWrap
        color: Theme.textCaption
        font.pixelSize: Theme.fontCaption
        lineHeight: 1.3
    }
    FlFilmSlider {
        controlKey: "profile_strength"
        label: "Strength"
        from: 0; to: 200; neutral: 100
        suffix: "%"
        tip: "Master strength of this stock's authored tone curve. At 100, you get the stock's intended baseline; lower softens its toe, midtones, and shoulder together, while higher reinforces them within that stock's safe limits."
    }
}
