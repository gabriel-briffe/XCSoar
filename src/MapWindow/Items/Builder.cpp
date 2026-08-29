// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Builder.hpp"
#include "MapItem.hpp"
#include "List.hpp"
#include "util/StaticArray.hxx"
#include "Engine/Task/TaskManager.hpp"
#include "Engine/Task/Ordered/OrderedTask.hpp"
#include "Engine/Task/Ordered/Points/OrderedTaskPoint.hpp"
#include "Engine/Waypoint/Waypoints.hpp"
#include "Renderer/WaypointReachability.hpp"
#include "Renderer/WaypointRendererSettings.hpp"
#include "Projection/MapWindowProjection.hpp"
#include "Computer/Settings.hpp"
#include "NMEA/Aircraft.hpp"
#include "Task/ProtectedTaskManager.hpp"
#include "Task/ProtectedRoutePlanner.hpp"
#include "Engine/Task/AbstractTask.hpp"
#include "Engine/Task/Unordered/UnorderedTaskPoint.hpp"
#include "Engine/Task/Ordered/Points/OrderedTaskPoint.hpp"
#include "Engine/Task/Visitors/TaskPointVisitor.hpp"
#include "Engine/Task/Points/Type.hpp"
#include "NMEA/Info.hpp"
#include "Terrain/RasterTerrain.hpp"

namespace {

class TaskWaypointIdCollector final : public TaskPointConstVisitor {
  StaticArray<unsigned, 32> &ids;

  void Add(const WaypointPtr &wp) noexcept {
    if (wp != nullptr)
      ids.checked_append(wp->id);
  }

public:
  explicit TaskWaypointIdCollector(StaticArray<unsigned, 32> &_ids) noexcept
    :ids(_ids) {}

  void Visit(const TaskPoint &tp) override {
    switch (tp.GetType()) {
    case TaskPointType::UNORDERED:
      Add(((const UnorderedTaskPoint &)tp).GetWaypointPtr());
      break;
    case TaskPointType::START:
    case TaskPointType::AST:
    case TaskPointType::AAT:
    case TaskPointType::FINISH:
      Add(((const OrderedTaskPoint &)tp).GetWaypointPtr());
      break;
    }
  }
};

void
CollectTaskWaypointIds(const ProtectedTaskManager &task,
                       StaticArray<unsigned, 32> &ids) noexcept
{
  ProtectedTaskManager::Lease task_manager(task);
  const AbstractTask *active = task_manager->GetActiveTask();
  if (active == nullptr)
    return;

  TaskWaypointIdCollector collector(ids);
  active->AcceptTaskPointVisitor(collector);
}

} // namespace

void
MapItemListBuilder::AddLocation(const NMEAInfo &basic,
                                const RasterTerrain *terrain)
{
  if (list.full())
    return;

  GeoVector vector;
  if (basic.location_available)
    vector = basic.location.DistanceBearing(location);
  else
    vector.SetInvalid();

  double elevation = LocationMapItem::UNKNOWN_ELEVATION;
  if (terrain != nullptr)
    elevation = terrain->GetTerrainHeight(location)
      .ToDouble(LocationMapItem::UNKNOWN_ELEVATION);

  list.append(new LocationMapItem(location, vector, elevation));
}

void
MapItemListBuilder::AddArrivalAltitudes(
    const ProtectedRoutePlanner &route_planner,
    const RasterTerrain *terrain, double safety_height)
{
  if (list.full())
    return;

  // Calculate terrain elevation if possible
  double elevation = LocationMapItem::UNKNOWN_ELEVATION;
  if (terrain != nullptr)
    elevation = terrain->GetTerrainHeight(location)
      .ToDouble(LocationMapItem::UNKNOWN_ELEVATION);

  // Calculate target altitude
  double target_elevation = 0;
  if (elevation > ArrivalAltitudeMapItem::UNKNOWN_ELEVATION_THRESHOLD)
    target_elevation += elevation;

  // Save destination point incl. elevation and safety height
  const AGeoPoint destination(location, target_elevation);

  // Calculate arrival altitudes
  if (auto reach = route_planner.FindPositiveArrival(destination))
    list.append(new ArrivalAltitudeMapItem(elevation, *reach, safety_height));
}

void
MapItemListBuilder::AddSelfIfNear(const GeoPoint &self, Angle bearing)
{
  if (!list.full() && location.DistanceS(self) < range)
    list.append(new SelfMapItem(self, bearing));
}

void
MapItemListBuilder::AddWaypoints(const Waypoints &waypoints,
                                 const ProtectedRoutePlanner *route_planner,
                                 const ProtectedTaskManager *task,
                                 const MapWindowProjection &projection,
                                 const MoreData &basic,
                                 const DerivedInfo &calculated,
                                 const ComputerSettings &settings,
                                 const WaypointRendererSettings &waypoint_settings)
{
  StaticArray<unsigned, 32> task_waypoint_ids;
  if (task != nullptr)
    CollectTaskWaypointIds(*task, task_waypoint_ids);

  waypoints.VisitWithinRange(location, range, [&](const auto &w){
    if (list.full())
      return;

    const bool in_task = task_waypoint_ids.contains(w->id);
    if (!IsMapWaypointVisible(*w, waypoint_settings, projection, in_task))
      return;

    /* calculate the reachability the same way the map does, so the
       icon in the dialog matches the one on the map */
    auto reachable = WaypointReachability::INVALID;
    if (w->IsLandable() || w->flags.watched) {
      if (!w->IsLandable() ||
          waypoint_settings.IsLandableReachDecorated())
        reachable = CalculateWaypointReach(*w, route_planner, basic,
                                           calculated, settings.polar,
                                           settings.task).reachability;
    }

    list.append(new WaypointMapItem(w, reachable));
  });
}

void
MapItemListBuilder::AddTaskOZs(const ProtectedTaskManager &task)
{
  ProtectedTaskManager::Lease task_manager(task);
  if (task_manager->GetMode() != TaskType::ORDERED)
    return;

  const OrderedTask &ordered_task = task_manager->GetOrderedTask();

  AircraftState a;
  a.location = location;

  for (unsigned i = 0, size = ordered_task.TaskSize(); i < size; i++) {
    if (list.full())
      break;

    const OrderedTaskPoint &task_point = ordered_task.GetTaskPoint(i);
    if (!task_point.IsInSector(a))
      continue;

    const ObservationZonePoint &oz = task_point.GetObservationZone();
    list.append(new TaskOZMapItem(i, oz, task_point.GetType(),
                                  task_point.GetWaypointPtr()));
  }
}
