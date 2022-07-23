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

#define _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS

#include "GlobalIlluminationCiVct.hh"

#include "CiVctCascadePrivate.hh"

#include <string>
#include <utility>
#include <vector>

#include <sdf/Link.hh>
#include <sdf/Model.hh>

#include <ignition/common/Console.hh>
#include <ignition/common/Profiler.hh>

#include <ignition/plugin/Register.hh>

#include <ignition/math/Pose3.hh>
#include <ignition/math/Vector3.hh>

#include <ignition/transport/Node.hh>

#include <ignition/gui/Application.hh>
#include <ignition/gui/Conversions.hh>
#include <ignition/gui/GuiEvents.hh>
#include <ignition/gui/MainWindow.hh>

#include "ignition/gazebo/Entity.hh"
#include "ignition/gazebo/EntityComponentManager.hh"
#include "ignition/gazebo/components/Name.hh"
#include "ignition/gazebo/components/World.hh"
#include "ignition/gazebo/rendering/RenderUtil.hh"

#include "ignition/rendering/Camera.hh"
#include "ignition/rendering/GlobalIlluminationCiVct.hh"
#include "ignition/rendering/LidarVisual.hh"
#include "ignition/rendering/RenderEngine.hh"
#include "ignition/rendering/RenderTypes.hh"
#include "ignition/rendering/RenderingIface.hh"
#include "ignition/rendering/Scene.hh"

#include "ignition/gazebo/Util.hh"
#include "ignition/gazebo/components/Link.hh"
#include "ignition/gazebo/components/Model.hh"
#include "ignition/gazebo/components/ParentEntity.hh"
#include "ignition/gazebo/components/Pose.hh"
#include "ignition/gazebo/components/Sensor.hh"

#include "ignition/msgs/laserscan.pb.h"

#include "Tsa.hh"

// clang-format off
namespace ignition
{
namespace gazebo
{
inline namespace IGNITION_GAZEBO_VERSION_NAMESPACE
{
  /// \brief Private data class for GlobalIlluminationCiVct
  class IGNITION_GAZEBO_HIDDEN GlobalIlluminationCiVctPrivate
  {
    /// \brief Transport node
    public: transport::Node node;

    /// \brief Scene Pointer
    public: rendering::ScenePtr scene GUARDED_BY(serviceMutex);

    /// \brief Each cascade created by GI.
    /// We directly access the data in CiVctCascade from UI thread
    /// because it's safe to do so:
    ///   - Ogre2 doesn't invoke any side effect (i.e. build must be called)
    ///   - Ogre2 won't issue rendering commands (all rendering must
    ///     happen in the main thread, regardless of whether it's protected)
    public: std::vector<std::unique_ptr<CiVctCascadePrivate>> cascades
      GUARDED_BY(serviceMutex);

    /// \brief Pointer to GlobalIlluminationCiVct
    public: rendering::GlobalIlluminationCiVctPtr gi GUARDED_BY(serviceMutex);

    /// \brief Toggles this GI on/off. Only one can be active at the same time.
    public: bool enabled GUARDED_BY(serviceMutex){false};

    /// \brief See rendering::GlobalIlluminationCiVct::SetResolution
    public: uint32_t resolution[3] GUARDED_BY(serviceMutex){16u, 16u, 16u};

    /// \brief See rendering::GlobalIlluminationCiVct::SetBounceCount
    public: uint32_t bounceCount GUARDED_BY(serviceMutex){6u};

    /// \brief See rendering::GlobalIlluminationCiVct::SetHighQuality
    public: bool highQuality GUARDED_BY(serviceMutex){true};

    /// \brief See rendering::GlobalIlluminationCiVct::SetAnisotropic
    public: bool anisotropic GUARDED_BY(serviceMutex){true};

    /// \brief See rendering::GlobalIlluminationCiVct::DebugVisualizationMode
    public: uint32_t debugVisMode GUARDED_BY(
      serviceMutex){ rendering::GlobalIlluminationCiVct::DVM_None };

    /// \brief Camera from where the CIVCT cascades are centered around
    public: rendering::CameraPtr bindCamera GUARDED_BY(serviceMutex){ nullptr };

    /// \brief Available cameras for binding
    public: QStringList availableCameras;

#ifdef VCT_DISABLED
    /// \brief URI sequence to the lidar link
    public: std::string lidarString{""};

    /// \brief Pose of the lidar visual
    public: math::Pose3d lidarPose{math::Pose3d::Zero};

    /// \brief Topic name to subscribe
    public: std::string topicName{""};
#endif

#ifdef VCT_DISABLED
    /// \brief Entity representing the sensor in the world
    public: gazebo::Entity lidarEntity;
#endif

    /// \brief Mutex for variable mutated by the checkbox and spinboxes
    /// callbacks.
    public: std::mutex serviceMutex;

    /// \brief Initialization flag
    public: bool initialized{false};

    /// \brief Reset visual flag
    public: bool resetVisual{false};

    /// \brief GI visual display dirty flag
    public: bool visualDirty GUARDED_BY(serviceMutex){false};

    /// \brief GI visual display dirty flag; but it is fast/quick to rebuild
    public: bool lightingDirty GUARDED_BY(serviceMutex){false};

    /// \brief GI debug visualization is dirty. Only used by GUI.
    /// Not in simulation.
    public: bool debugVisualizationDirty GUARDED_BY(serviceMutex){false};

    /// \brief lidar sensor entity dirty flag
    public: bool lidarEntityDirty{true};
  };
}
}
}
// clang-format on

using namespace ignition;
using namespace gazebo;

// Q_DECLARE_METATYPE(CiVctCascadePrivate);

/////////////////////////////////////////////////
GlobalIlluminationCiVct::GlobalIlluminationCiVct() :
  GuiSystem(),
  dataPtr(new GlobalIlluminationCiVctPrivate)
{
  // no ops
}

/////////////////////////////////////////////////
GlobalIlluminationCiVct::~GlobalIlluminationCiVct()
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  this->dataPtr->gi.reset();
}

/////////////////////////////////////////////////
void GlobalIlluminationCiVct::LoadGlobalIlluminationCiVct()
  REQUIRES(this->dataPtr->serviceMutex)
{
  auto loadedEngNames = rendering::loadedEngines();
  if (loadedEngNames.empty())
  {
    return;
  }

  // assume there is only one engine loaded
  auto engineName = loadedEngNames[0];
  if (loadedEngNames.size() > 1)
  {
    igndbg << "More than one engine is available. "
           << "GlobalIlluminationCiVct plugin will use engine [" << engineName
           << "]" << std::endl;
  }
  auto engine = rendering::engine(engineName);
  if (!engine)
  {
    ignerr << "Internal error: failed to load engine [" << engineName
           << "]. GlobalIlluminationCiVct plugin won't work." << std::endl;
    return;
  }

  if (engine->SceneCount() == 0)
    return;

  // assume there is only one scene
  // load scene
  auto scene = engine->SceneByIndex(0);
  if (!scene)
  {
    ignerr << "Internal error: scene is null." << std::endl;
    return;
  }

  if (!scene->IsInitialized() || scene->VisualCount() == 0)
  {
    return;
  }

  // Create lidar visual
  igndbg << "Creating GlobalIlluminationCiVct" << std::endl;

  auto root = scene->RootVisual();
  this->dataPtr->gi = scene->CreateGlobalIlluminationCiVct();
  if (!this->dataPtr->gi)
  {
    ignwarn << "Failed to create GlobalIlluminationCiVct, GI plugin won't work."
            << std::endl;

    ignition::gui::App()
      ->findChild<ignition::gui::MainWindow *>()
      ->removeEventFilter(this);
  }
  else
  {
    this->dataPtr->gi->SetParticipatingVisuals(
      rendering::GlobalIlluminationBase::DYNAMIC_VISUALS |
      rendering::GlobalIlluminationBase::STATIC_VISUALS);
    this->dataPtr->scene = scene;
    this->dataPtr->initialized = true;
  }
}

/// \brief XML helper to retrieve values and handle errors
/// \param[in] _elem XML element to read
/// \param[out] _valueToSet Value to set. Left unmodified on error
/// \return True if _valueToSet was successfully set
static bool GetXmlBool(const tinyxml2::XMLElement *_elem, bool &_valueToSet)
{
  bool value = false;

  if (_elem->QueryBoolText(&value) != tinyxml2::XML_SUCCESS)
  {
    ignerr << "Failed to parse <" << _elem->Name()
           << "> value: " << _elem->GetText() << std::endl;
    return false;
  }
  else
  {
    _valueToSet = value;
    return true;
  }
}

/// \brief XML helper to retrieve values and handle errors
/// \param[in] _elem XML element to read
/// \param[out] _valueToSet Value to set. Left unmodified on error
/// \return True if _valueToSet was successfully set
static bool GetXmlFloat(const tinyxml2::XMLElement *_elem, float &_valueToSet)
{
  float value = 0;

  if (_elem->QueryFloatText(&value) != tinyxml2::XML_SUCCESS)
  {
    ignerr << "Failed to parse <" << _elem->Name()
           << "> value: " << _elem->GetText() << std::endl;
    return false;
  }
  else
  {
    _valueToSet = value;
    return true;
  }
}

/// \brief XML helper to retrieve values and handle errors
/// \param[in] _elem XML element to read
/// \param[out] _valueToSet Value to set. Left unmodified on error
/// \return True if _valueToSet was successfully set
static bool GetXmlUint32(const tinyxml2::XMLElement *_elem,
                         uint32_t &_valueToSet)
{
  int value = 0;

  if (_elem->QueryIntText(&value) != tinyxml2::XML_SUCCESS)
  {
    ignerr << "Failed to parse <" << _elem->Name()
           << "> value: " << _elem->GetText() << std::endl;
    return false;
  }
  else
  {
    _valueToSet = static_cast<uint32_t>(value);
    return true;
  }
}

/// \brief XML helper to retrieve values and handle errors
/// \param[in] _elem XML element to read
/// \param[out] _valueToSet Values to set. Left unmodified on error.
/// Its array length must be >= 3
/// \return True if _valueToSet was successfully set
static bool GetXmlUint32x3(const tinyxml2::XMLElement *_elem,
                           uint32_t _valueToSet[3])
{
  std::istringstream stream(_elem->GetText());
  math::Vector3i values3;
  stream >> values3;

  _valueToSet[0] = static_cast<uint32_t>(values3.X());
  _valueToSet[1] = static_cast<uint32_t>(values3.Y());
  _valueToSet[2] = static_cast<uint32_t>(values3.Z());

  return true;
}

/////////////////////////////////////////////////
void GlobalIlluminationCiVct::LoadConfig(
  const tinyxml2::XMLElement *_pluginElem)
{
  if (this->title.empty())
    this->title = "Global Illumination (VCT)";

  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);

  if (auto elem = _pluginElem->FirstChildElement("enabled"))
  {
    GetXmlBool(elem, this->dataPtr->enabled);
  }
  if (auto elem = _pluginElem->FirstChildElement("highQuality"))
  {
    GetXmlBool(elem, this->dataPtr->highQuality);
  }
  if (auto elem = _pluginElem->FirstChildElement("anisotropic"))
  {
    GetXmlBool(elem, this->dataPtr->anisotropic);
  }
  if (auto elem = _pluginElem->FirstChildElement("resolution"))
  {
    GetXmlUint32x3(elem, this->dataPtr->resolution);
  }
  if (auto elem = _pluginElem->FirstChildElement("bounceCount"))
  {
    GetXmlUint32(elem, this->dataPtr->bounceCount);
  }
  if (auto elem = _pluginElem->FirstChildElement("debugVisMode"))
  {
    const std::string text = elem->GetText();
    if (text == "none")
    {
      this->dataPtr->debugVisMode =
        rendering::GlobalIlluminationCiVct::DVM_None;
    }
    else if (text == "albedo")
    {
      this->dataPtr->debugVisMode =
        rendering::GlobalIlluminationCiVct::DVM_Albedo;
    }
    else if (text == "normal")
    {
      this->dataPtr->debugVisMode =
        rendering::GlobalIlluminationCiVct::DVM_Normal;
    }
    else if (text == "emissive")
    {
      this->dataPtr->debugVisMode =
        rendering::GlobalIlluminationCiVct::DVM_Emissive;
    }
    else if (text == "lighting")
    {
      this->dataPtr->debugVisMode =
        rendering::GlobalIlluminationCiVct::DVM_Lighting;
    }
    else
    {
      GetXmlUint32(elem, this->dataPtr->debugVisMode);
    }
  }

  ignition::gui::App()
    ->findChild<ignition::gui::MainWindow *>()
    ->installEventFilter(this);
}

/////////////////////////////////////////////////
bool GlobalIlluminationCiVct::eventFilter(QObject *_obj, QEvent *_event)
{
  if (_event->type() == ignition::gui::events::Render::kType)
  {
    // This event is called in Scene3d's RenderThread, so it's safe to make
    // rendering calls here

    std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
    if (!this->dataPtr->initialized)
    {
      this->LoadGlobalIlluminationCiVct();
    }

    if (this->dataPtr->gi)
    {
      if (this->dataPtr->resetVisual)
      {
#ifdef VCT_DISABLED
        this->dataPtr->lidar->ClearPoints();
#endif
        this->dataPtr->resetVisual = false;
      }

      if (!this->dataPtr->visualDirty && !this->dataPtr->gi->Enabled() &&
          this->dataPtr->enabled)
      {
        // If we're here, GI was disabled externally. This can happen
        // if e.g. another GI solution was enabled (only one can be active)
        this->dataPtr->enabled = false;
        this->EnabledChanged();
      }

      if (this->dataPtr->visualDirty)
      {
        this->dataPtr->gi->SetBounceCount(this->dataPtr->bounceCount);
        this->dataPtr->gi->SetHighQuality(this->dataPtr->highQuality);

        if (this->dataPtr->gi->Started())
        {
          // Ogre-Next may crash if some of the settings above are
          // changed while visualizing is enabled.
          this->dataPtr->gi->SetDebugVisualization(
            rendering::GlobalIlluminationCiVct::DVM_None);
        }

        if (this->dataPtr->enabled)
        {
          if (!this->dataPtr->gi->Started())
          {
            this->dataPtr->gi->Bind(this->dataPtr->bindCamera);
            this->dataPtr->gi->Start(this->dataPtr->bounceCount,
                                     this->dataPtr->anisotropic);
          }
          else
          {
            this->dataPtr->gi->NewSettings(this->dataPtr->bounceCount,
                                           this->dataPtr->anisotropic);
          }
          this->dataPtr->gi->Build();
          this->dataPtr->scene->SetActiveGlobalIllumination(this->dataPtr->gi);
        }
        else
        {
          this->dataPtr->scene->SetActiveGlobalIllumination(nullptr);
        }

        if (this->dataPtr->gi->Started())
        {
          // Restore debug visualization to desired.
          this->dataPtr->gi->SetDebugVisualization(
            static_cast<
              rendering::GlobalIlluminationCiVct::DebugVisualizationMode>(
              this->dataPtr->debugVisMode));
        }

#ifdef VCT_DISABLED
        this->dataPtr->lidar->SetWorldPose(this->dataPtr->lidarPose);
        this->dataPtr->lidar->Update();
#endif
        this->dataPtr->visualDirty = false;
        this->dataPtr->lightingDirty = false;
        this->dataPtr->debugVisualizationDirty = false;
      }
      else if (this->dataPtr->lightingDirty)
      {
        this->dataPtr->gi->SetBounceCount(this->dataPtr->bounceCount);
        this->dataPtr->gi->SetHighQuality(this->dataPtr->highQuality);

        if (this->dataPtr->gi->Enabled())
        {
          this->dataPtr->gi->SetDebugVisualization(
            rendering::GlobalIlluminationCiVct::DVM_None);

          this->dataPtr->gi->LightingChanged();

          this->dataPtr->gi->SetDebugVisualization(
            static_cast<
              rendering::GlobalIlluminationCiVct::DebugVisualizationMode>(
              this->dataPtr->debugVisMode));

          this->dataPtr->debugVisualizationDirty = false;
        }
        this->dataPtr->lightingDirty = false;
      }
      else if (this->dataPtr->debugVisualizationDirty)
      {
        this->dataPtr->gi->SetDebugVisualization(
          static_cast<
            rendering::GlobalIlluminationCiVct::DebugVisualizationMode>(
            this->dataPtr->debugVisMode));
        this->dataPtr->debugVisualizationDirty = false;
      }
    }
    else
    {
      ignerr << "GI pointer is not set" << std::endl;
    }
  }

  // Standard event processing
  return QObject::eventFilter(_obj, _event);
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::Update(const UpdateInfo &,
                                     EntityComponentManager &_ecm)
{
  IGN_PROFILE("GlobalIlluminationCiVct::Update");

  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);

  if (this->dataPtr->lidarEntityDirty)
  {
#ifdef VCT_DISABLED
    auto lidarURIVec = common::split(common::trimmed(this->dataPtr->gi), "::");
    if (lidarURIVec.size() > 0)
    {
      auto baseEntity =
        _ecm.EntityByComponents(components::Name(lidarURIVec[0]));
      if (!baseEntity)
      {
        ignerr << "Error entity " << lidarURIVec[0]
               << " doesn't exist and cannot be used to set lidar visual pose"
               << std::endl;
        return;
      }
      else
      {
        auto parent = baseEntity;
        bool success = false;
        for (size_t i = 0u; i < lidarURIVec.size() - 1; i++)
        {
          auto children =
            _ecm.EntitiesByComponents(components::ParentEntity(parent));
          bool foundChild = false;
          for (auto child : children)
          {
            std::string nextstring = lidarURIVec[i + 1];
            auto comp = _ecm.Component<components::Name>(child);
            if (!comp)
            {
              continue;
            }
            std::string childname = std::string(comp->Data());
            if (nextstring.compare(childname) == 0)
            {
              parent = child;
              foundChild = true;
              if (i + 1 == lidarURIVec.size() - 1)
              {
                success = true;
              }
              break;
            }
          }
          if (!foundChild)
          {
            ignerr << "The entity could not be found."
                   << "Error displaying lidar visual" << std::endl;
            return;
          }
        }
        if (success)
        {
#  ifdef VCT_DISABLED
          this->dataPtr->lidarEntity = parent;
#  endif
          this->dataPtr->lidarEntityDirty = false;
        }
      }
    }
#endif
  }

  // Only update lidarPose if the lidarEntity exists and the lidar is
  // initialized and the sensor message is yet to arrive.
  //
  // If we update the worldpose on the physics thread **after** the sensor
  // data arrives, the visual is offset from the obstacle if the sensor is
  // moving fast.
  if (!this->dataPtr->lidarEntityDirty && this->dataPtr->initialized &&
      !this->dataPtr->visualDirty)
  {
#ifdef VCT_DISABLED
    this->dataPtr->lidarPose = worldPose(this->dataPtr->lidarEntity, _ecm);
#endif
  }
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::UpdateDebugVisualizationMode(int _mode)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);

  rendering::GlobalIlluminationCiVct::DebugVisualizationMode
    debugVisualizationMode = rendering::GlobalIlluminationCiVct::DVM_None;

  if (_mode >= rendering::GlobalIlluminationCiVct::DVM_Albedo &&
      _mode <= rendering::GlobalIlluminationCiVct::DVM_None)
  {
    debugVisualizationMode =
      static_cast<rendering::GlobalIlluminationCiVct::DebugVisualizationMode>(
        _mode);
  }

  this->dataPtr->gi->SetDebugVisualization(debugVisualizationMode);
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::OnTopic(const QString &_topicName)
{
#ifdef VCT_DISABLED
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  if (!this->dataPtr->topicName.empty() &&
      !this->dataPtr->node.Unsubscribe(this->dataPtr->topicName))
  {
    ignerr << "Unable to unsubscribe from topic [" << this->dataPtr->topicName
           << "]" << std::endl;
  }
  this->dataPtr->topicName = _topicName.toStdString();

  // Reset visualization
  this->dataPtr->resetVisual = true;

  // Create new subscription
  if (!this->dataPtr->node.Subscribe(this->dataPtr->topicName,
                                     &GlobalIlluminationCiVct::OnScan, this))
  {
    ignerr << "Unable to subscribe to topic [" << this->dataPtr->topicName
           << "]\n";
    return;
  }
  this->dataPtr->visualDirty = false;
  ignmsg << "Subscribed to " << this->dataPtr->topicName << std::endl;
#endif
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::SetEnabled(const bool _enabled)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  this->dataPtr->enabled = _enabled;
  this->dataPtr->visualDirty = true;
}

//////////////////////////////////////////////////
bool GlobalIlluminationCiVct::Enabled() const
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  return this->dataPtr->enabled;
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::SetBounceCount(const uint32_t _bounces)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  this->dataPtr->bounceCount = _bounces;
  this->dataPtr->lightingDirty = true;
}

//////////////////////////////////////////////////
uint32_t GlobalIlluminationCiVct::BounceCount() const
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  return this->dataPtr->bounceCount;
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::SetHighQuality(const bool _quality)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  this->dataPtr->highQuality = _quality;
  this->dataPtr->lightingDirty = true;
}

//////////////////////////////////////////////////
bool GlobalIlluminationCiVct::HighQuality() const
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  return this->dataPtr->highQuality;
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::SetAnisotropic(const bool _anisotropic)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  this->dataPtr->anisotropic = _anisotropic;
  this->dataPtr->lightingDirty = true;
}

//////////////////////////////////////////////////
bool GlobalIlluminationCiVct::Anisotropic() const
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  return this->dataPtr->anisotropic;
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::SetDebugVisualizationMode(const uint32_t _visMode)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  this->dataPtr->debugVisMode = _visMode;
  this->dataPtr->debugVisualizationDirty = true;
}

//////////////////////////////////////////////////
uint32_t GlobalIlluminationCiVct::DebugVisualizationMode() const
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  return this->dataPtr->debugVisMode;
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::OnCamareBind(const QString &_cameraName)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);

  auto scene = this->dataPtr->scene.get();
  rendering::SensorPtr sensor = scene->SensorByName(_cameraName.toStdString());
  rendering::CameraPtr asCamera =
    std::dynamic_pointer_cast<rendering::Camera>(sensor);

  if (asCamera)
  {
    this->dataPtr->bindCamera = asCamera;
  }
  else
  {
    this->CameraListChanged();
  }
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::OnRefreshCameras()
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);

  auto scene = this->dataPtr->scene.get();
  const unsigned int sensorCount = scene->SensorCount();
  for (unsigned int i = 0u; i < sensorCount; ++i)
  {
    rendering::SensorPtr sensor = scene->SensorByIndex(i);
    rendering::CameraPtr asCamera =
      std::dynamic_pointer_cast<rendering::Camera>(sensor);

    if (asCamera)
    {
      this->dataPtr->availableCameras.push_back(
        QString::fromStdString(asCamera->Name()));

      if (!this->dataPtr->bindCamera)
        this->dataPtr->bindCamera = asCamera;
    }
  }

  this->CameraListChanged();
}

//////////////////////////////////////////////////
QStringList GlobalIlluminationCiVct::CameraList()
{
  return this->dataPtr->availableCameras;
}

//////////////////////////////////////////////////
QObject *GlobalIlluminationCiVct::AddCascade()
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);

  rendering::CiVctCascade const *ref = nullptr;
  if (!this->dataPtr->cascades.empty())
    ref = this->dataPtr->cascades.back()->cascade.get();

  auto cascadeRendering = this->dataPtr->gi->AddCascade(ref);

  this->dataPtr->cascades.push_back(std::unique_ptr<CiVctCascadePrivate>(
    new CiVctCascadePrivate(this->dataPtr->serviceMutex, cascadeRendering)));

  if (!ref)
  {
    this->dataPtr->cascades.back()->cascade->SetThinWallCounter(1.0f);
  }

  return this->dataPtr->cascades.back().get();
}

//////////////////////////////////////////////////
void GlobalIlluminationCiVct::PopCascade()
{
  if (!this->dataPtr->cascades.empty())
  {
    this->dataPtr->cascades.pop_back();
    this->dataPtr->gi->PopCascade();
  }
}

//////////////////////////////////////////////////
QObject *GlobalIlluminationCiVct::GetCascade(int _idx) const
{
  std::lock_guard<std::mutex> lock(this->dataPtr->serviceMutex);
  return this->dataPtr->cascades[(size_t)_idx].get();
}

// Register this plugin
IGNITION_ADD_PLUGIN(ignition::gazebo::GlobalIlluminationCiVct,
                    ignition::gui::Plugin)
