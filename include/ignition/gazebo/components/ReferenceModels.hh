/*
 * Copyright (C) 2021 Open Source Robotics Foundation
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
#ifndef IGNITION_GAZEBO_COMPONENTS_REFERENCEMODELS_HH_
#define IGNITION_GAZEBO_COMPONENTS_REFERENCEMODELS_HH_

#include <iostream>
#include <set>

#include <ignition/gazebo/Entity.hh>
#include <ignition/gazebo/components/Factory.hh>
#include <ignition/gazebo/components/Component.hh>
#include <ignition/gazebo/config.hh>

namespace ignition
{
namespace gazebo
{
// Inline bracket to help doxygen filtering.
inline namespace IGNITION_GAZEBO_VERSION_NAMESPACE {
namespace components
{
  /// \brief Data structure that holds information about which models for
  /// a given link view this link as the model's canonical link. An example
  /// of multiple models having the same canonical link could be nested models.
  struct ReferenceModelsInfo
  {
    /// \brief A topological ordering of the models which view this link as its
    /// canonical link.
    std::set<Entity> models;

    /// \brief Add a model that references this link as its canonical link.
    /// This should be called when a canonical link is assigned to a model.
    /// \param[in] _model The model to be added.
    public: void AddModel(const Entity &_model)
    {
      this->models.insert(_model);
    }

    /// \brief Remove a model that no longer references this link as its
    /// canonical link. This should be called when a model is removed/deleted.
    /// \param[in] _model The model to be removed.
    public: void RemoveModel(const Entity &_model)
    {
      this->models.erase(_model);
    }

    public: bool operator==(const ReferenceModelsInfo &_info) const
    {
      return this->models == _info.models;
    }

    public: bool operator!=(const ReferenceModelsInfo &_info) const
    {
      return !(*this == _info);
    }
  };
}

namespace serializers
{
  /// \brief Serializer for ReferenceModelsInfo object
  class ReferenceModelsInfoSerializer
  {
    /// \brief Serialization for ReferenceModelsInfo.
    /// \param[out] _out Output stream.
    /// \param[in] _info ReferenceModelsInfo object to stream.
    /// \return The stream.
    public: static std::ostream &Serialize(std::ostream &_out,
                const components::ReferenceModelsInfo &_info)
    {
      for (const auto &model : _info.models)
        _out << model << " ";

      return _out;
    }

    /// \brief Deserialization for ReferenceModelsInfo.
    /// \param[in] _in Input stream.
    /// \param[out] _info ReferenceModelsInfo object to populate.
    /// \return The stream.
    public: static std::istream &Deserialize(std::istream &_in,
                components::ReferenceModelsInfo &_info)
    {
      _info.models.clear();
      Entity model;
      while (_in >> model)
        _info.AddModel(model);

      return _in;
    };
  };
}

namespace components
{
  /// \brief A component that gives a mapping between a link and all of the
  /// models this link serves as a canonical link for. The models in the
  /// mapping are in topological order. This component should only be applied
  /// to links.
  using ReferenceModels =
    Component<ReferenceModelsInfo, class ReferenceModelsTag,
      serializers::ReferenceModelsInfoSerializer>;
  IGN_GAZEBO_REGISTER_COMPONENT("ign_gazebo_components.ReferenceModels",
      ReferenceModels)
}
}
}
}

#endif
