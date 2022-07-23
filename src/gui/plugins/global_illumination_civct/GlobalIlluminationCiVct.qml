/*
 * Copyright (C) 2022 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

import QtQuick 2.9
import QtQuick.Controls 2.1
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.3
//import QtQuick.Controls.Styles 1.4
//import CiVctCascadePrivate 1.0
import "qrc:/qml"

/*
import "qrc:/ComponentInspector"
import "qrc:/"*/

GridLayout {
  id: mainGridLayout
  columns: 6
  columnSpacing: 10
  Layout.minimumWidth: 350
  Layout.minimumHeight: 800
  anchors.fill: parent
  anchors.leftMargin: 10
  anchors.rightMargin: 10

  function isPowerOf2(n) {
    return (n & n-1) === 0;
  }

  // Returns the closest power of 2, rounding down if need to.
  //  floorPowerOf2( 4 ) = 4
  //  floorPowerOf2( 5 ) = 4
  function floorPowerOf2(n) {
    return 1 << (31 - Math.clz32(n));
  }

  // Returns the closest power of 2, rounding up if need to.
  //  floorPowerOf2( 4 ) = 4
  //  floorPowerOf2( 5 ) = 8
  function ceilPowerOf2(n) {
      if (isPowerOf2(n)) {
        return n;
      }
    return 1 << (31 - Math.clz32(n) + 1);
  }

  function nearestPowerOf2(n, oldValue=undefined) {
    if (oldValue === undefined) {
      return floorPowerOf2(n);
    }
    else {
      if (oldValue <= n) {
        return ceilPowerOf2(n);
      }
      else {
        return floorPowerOf2(n);
      }
    }
  }

  Button {
    id: addCascade
    text: qsTr("Add Cascade")
    Layout.columnSpan: 6
    Layout.fillWidth: true
    onClicked: {
      // cascade = GlobalIlluminationCiVct.AddCascade()
      var cascadeComponent = Qt.createComponent("CiVctCascadePrivate.qml");
      var cascadeObj = cascadeComponent.createObject(mainGridLayout);
      //cascadeObj.width = Qt.binding( function() {return mainGridLayout.width});
      //mainGridLayout.add
    }
  }

  CheckBox {
    Layout.alignment: Qt.AlignHCenter
    id: displayVisual
    Layout.columnSpan: 6
    Layout.fillWidth: true
    text: qsTr("Enabled")
    checked: GlobalIlluminationCiVct.enabled
    onToggled: {
      GlobalIlluminationCiVct.enabled = checked
    }
  }

  Text {
    Layout.columnSpan: 4
    id: bounceCountStr
    color: "dimgrey"
    text: qsTr("BounceCount")
  }

  IgnSpinBox {
    Layout.columnSpan: 2
    Layout.fillWidth: true
    id: bounceCount
    value: GlobalIlluminationCiVct.bounceCount
    minimumValue: 0
    maximumValue: 16
    decimals: 1
    onValueChanged: {
      GlobalIlluminationCiVct.bounceCount = value
    }
  }

  CheckBox {
    Layout.alignment: Qt.AlignHCenter
    id: highQuality
    Layout.columnSpan: 6
    Layout.fillWidth: true
    text: qsTr("High Quality")
    checked: GlobalIlluminationCiVct.highQuality
    onToggled: {
      GlobalIlluminationCiVct.highQuality = checked;
    }
  }

  CheckBox {
    Layout.alignment: Qt.AlignHCenter
    id: anisotropic
    Layout.columnSpan: 6
    Layout.fillWidth: true
    text: qsTr("Anisotropic")
    checked: GlobalIlluminationCiVct.anisotropic
    onToggled: {
      GlobalIlluminationCiVct.anisotropic = checked;
    }
  }

  Text {
    Layout.columnSpan: 2
    id: debugVisualizationStr
    color: "dimgrey"
    text: qsTr("DebugVisualization")
  }

  ComboBox {
    Layout.columnSpan: 4
    id: debugVisualization
    Layout.fillWidth: true
    currentIndex: GlobalIlluminationCiVct.debugVisualizationMode
    model: ["Albedo", "Normal", "Emissive", "Lighting", "None"]
    onCurrentIndexChanged: {
      if (currentIndex < 0|| currentIndex > 4) {
        return;
      }
      GlobalIlluminationCiVct.debugVisualizationMode = currentIndex
    }
  }
}

/*##^##
Designer {
    D{i:0;autoSize:true;height:480;width:640}D{i:1}D{i:2}D{i:3}D{i:4}D{i:5}D{i:6}D{i:7}
D{i:8}D{i:9}D{i:10}D{i:11}D{i:12}D{i:13}D{i:14}D{i:15}D{i:16}D{i:17}D{i:18}D{i:19}
}
##^##*/
