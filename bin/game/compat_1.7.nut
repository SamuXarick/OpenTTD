/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

GSLog.Info("1.7 API compatibility in effect.");

/* 1.9 adds a vehicle type parameter. */
GSBridge._GetName <- GSBridge.GetName;
GSBridge.GetName <- function(bridge_id)
{
	return GSBridge._GetName(bridge_id, GSVehicle.VT_RAIL);
}

/* 1.11 adds a tile parameter. */
GSCompany._ChangeBankBalance <- GSCompany.ChangeBankBalance;
GSCompany.ChangeBankBalance <- function(company, delta, expenses_type)
{
	return GSCompany._ChangeBankBalance(company, delta, expenses_type, GSMap.TILE_INVALID);
}

/* 13 really checks RoadType against RoadType */
GSRoad._HasRoadType <- GSRoad.HasRoadType;
GSRoad.HasRoadType <- function(tile, road_type)
{
	local list = GSRoadTypeList(GSRoad.GetRoadTramType(road_type));
	foreach (rt, _ in list) {
		if (GSRoad._HasRoadType(tile, rt)) {
			return true;
		}
	}
	return false;
}

GSList.Valuate <-
GSBridgeList.Valuate <-
GSBridgeList_Length.Valuate <-
GSCargoList.Valuate <-
GSCargoList_IndustryAccepting.Valuate <-
GSCargoList_IndustryProducing.Valuate <-
GSCargoList_StationAccepting.Valuate <-
GSClientList.Valuate <-
GSClientList_Company.Valuate <-
GSDepotList.Valuate <-
GSEngineList.Valuate <-
GSGroupList.Valuate <-
GSIndustryList.Valuate <-
GSIndustryList_CargoAccepting.Valuate <-
GSIndustryList_CargoProducing.Valuate <-
GSIndustryTypeList.Valuate <-
GSNewGRFList.Valuate <-
GSObjectTypeList.Valuate <-
GSRailTypeList.Valuate <-
GSRoadTypeList.Valuate <-
GSSignList.Valuate <-
GSStationList.Valuate <-
GSStationList_Cargo.Valuate <-
GSStationList_CargoPlanned.Valuate <-
GSStationList_CargoWaiting.Valuate <-
GSStationList_CargoWaitingByFrom.Valuate <-
GSStationList_CargoWaitingViaByFrom.Valuate <-
GSStationList_CargoWaitingByVia.Valuate <-
GSStationList_CargoWaitingFromByVia.Valuate <-
GSStationList_CargoPlannedByFrom.Valuate <-
GSStationList_CargoPlannedViaByFrom.Valuate <-
GSStationList_CargoPlannedByVia.Valuate <-
GSStationList_CargoPlannedFromByVia.Valuate <-
GSStationList_Vehicle.Valuate <-
GSStoryPageElementList.Valuate <-
GSStoryPageList.Valuate <-
GSSubsidyList.Valuate <-
GSTileList.Valuate <-
GSTileList_IndustryAccepting.Valuate <-
GSTileList_IndustryProducing.Valuate <-
GSTileList_StationType.Valuate <-
GSTileList_StationCoverage.Valuate <-
GSTownList.Valuate <-
GSTownEffectList.Valuate <-
GSVehicleList.Valuate <-
GSVehicleList_Station.Valuate <-
GSVehicleList_Depot.Valuate <-
GSVehicleList_SharedOrders.Valuate <-
GSVehicleList_Group.Valuate <-
GSVehicleList_DefaultGroup.Valuate <-
GSWaypointList.Valuate <-
GSWaypointList_Vehicle.Valuate <-
function(valuator_function, ...)
{
	local copy_list = GSList();
	copy_list.AddList(this);
	copy_list.Sort(GSList.SORT_BY_ITEM, GSList.SORT_ASCENDING);

	for (local item = copy_list.Begin(); !copy_list.IsEnd(); item = copy_list.Next()) {
		local i = 0;
		if (vargc == 0) copy_list.SetValue(item, valuator_function(item).tointeger());
		if (vargc == 1) copy_list.SetValue(item, valuator_function(item, vargv[i++]).tointeger());
		if (vargc == 2) copy_list.SetValue(item, valuator_function(item, vargv[i++], vargv[i++]).tointeger());
		if (vargc == 3) copy_list.SetValue(item, valuator_function(item, vargv[i++], vargv[i++], vargv[i++]).tointeger());
		if (vargc == 4) copy_list.SetValue(item, valuator_function(item, vargv[i++], vargv[i++], vargv[i++], vargv[i++]).tointeger());
		if (vargc == 5) copy_list.SetValue(item, valuator_function(item, vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++]).tointeger());
		if (vargc == 6) copy_list.SetValue(item, valuator_function(item, vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++]).tointeger());
		if (vargc == 7) copy_list.SetValue(item, valuator_function(item, vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++]).tointeger());
		if (vargc == 8) copy_list.SetValue(item, valuator_function(item, vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++]).tointeger());
		if (vargc == 9) copy_list.SetValue(item, valuator_function(item, vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++]).tointeger());
		if (vargc == 10) copy_list.SetValue(item, valuator_function(item, vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++], vargv[i++]).tointeger());
		if (vargc >= 11) {
			GSLog.Error("too many arguments in valuator function");
			assert(false);
		}
	}

	this.Clear();
	this.AddList(copy_list);
}
