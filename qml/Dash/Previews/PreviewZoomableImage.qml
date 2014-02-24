/*
 * Copyright (C) 2014 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick 2.0
import Ubuntu.Components 0.1
import "../../Components"

/*! \brief Preview widget for image.

    This widget shows image contained in widgetData["source"],
    can be zoomable accordingly with widgetData["zoomable"].
 */

PreviewWidget {
    id: root
    implicitHeight: units.gu(22)

    Flickable {
        id: flickable
        objectName: "flickable"
        clip: true
        contentHeight: imageContainer.height
        contentWidth: imageContainer.width
        onHeightChanged: image.calculateSize()
        onWidthChanged: image.calculateSize()
        anchors.fill: parent

        Item {
            id: imageContainer
            objectName: "imageContainer"
            width: Math.max(image.width * image.scale, flickable.width)
            height: Math.max(image.height * image.scale, flickable.height)

            AnimatedImage {
                id: image
                objectName: "image"
                property real prevScale
                smooth: !flickable.movingVertically
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
                source: widgetData["source"]

                function calculateSize() {
                    scale = Math.min(flickable.width / width, flickable.height / height) * 0.98;
                    pinchArea.minScale = scale;
                    prevScale = Math.min(scale, 1);
                }

                onScaleChanged: {
                    if ((width * scale) > flickable.width) {
                        var xoff = (flickable.width / 2 + flickable.contentX) * scale / prevScale;
                        flickable.contentX = xoff - flickable.width / 2;
                    }
                    if ((height * scale) > flickable.height) {
                        var yoff = (flickable.height / 2 + flickable.contentY) * scale / prevScale;
                        flickable.contentY = yoff - flickable.height / 2;
                    }

                    prevScale = scale;
                }

                onStatusChanged: {
                    if (status == Image.Ready) {
                        calculateSize();
                        playing = true;
                    }
                }
            }
        }

        PinchArea {
            id: pinchArea
            objectName: "pinchArea"
            property real minScale: 1.0
            property real lastScale: 1.0
            anchors.fill: parent
            enabled: widgetData["zoomable"] ? widgetData["zoomable"] : false

            pinch.target: image
            pinch.minimumScale: minScale
        
            onPinchFinished: flickable.returnToBounds()
        }

        MouseArea {
            id: mouseArea
            objectName: "mouseArea"

            anchors.fill: parent
            property bool doubleClicked: false
            property bool swipeDone: false
            property int startX
            property int startY
            property real startScale: pinchArea.minScale
            enabled: widgetData["zoomable"] ? widgetData["zoomable"] : false

            onWheel: {
                if (wheel.angleDelta.y > 0) {
                    startScale = image.scale;
                    image.scale = startScale + 0.1;
                } else if (wheel.angleDelta.y < 0) {
                    startScale = image.scale;
                    if (image.scale > 0.1) {
                        image.scale = startScale - 0.1;
                    }
                }
                wheel.accepted = true;
            }

            onPressed: {
                startX = (mouse.x / image.scale);
                startY = (mouse.y / image.scale);
                startScale = image.scale;
            }

            onReleased: {
                if (image.scale == startScale) {
                    var deltaX = (mouse.x / image.scale) - startX;
                    var deltaY = (mouse.y / image.scale) - startY;

                    if (image.scale == pinchArea.minScale && 
                            (Math.abs(deltaX) > 50 || Math.abs(deltaY) > 50)) {
                        swipeDone = true;
                    }
                }
            }
        }
    }
}
