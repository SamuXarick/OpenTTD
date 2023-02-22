/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

AILog.Info("1.0 API compatibility in effect.");

AIRoad._BuildRoadStation <- AIRoad.BuildRoadStation;
AIRoad.BuildRoadStation <- function(tile, front, road_veh_type, station_id)
{
	if (AIRoad.IsRoadStationTile(tile) && AICompany.IsMine(AITile.GetOwner(tile))) return false;

	return AIRoad._BuildRoadStation(tile, front, road_veh_type, station_id);
}

AIRoad._BuildDriveThroughRoadStation <- AIRoad.BuildDriveThroughRoadStation;
AIRoad.BuildDriveThroughRoadStation <- function(tile, front, road_veh_type, station_id)
{
	if (AIRoad.IsRoadStationTile(tile) && AICompany.IsMine(AITile.GetOwner(tile))) return false;

	return AIRoad._BuildDriveThroughRoadStation(tile, front, road_veh_type, station_id);
}

AIBridgeList.HasNext <-
AIBridgeList_Length.HasNext <-
AICargoList.HasNext <-
AICargoList_IndustryAccepting.HasNext <-
AICargoList_IndustryProducing.HasNext <-
AIDepotList.HasNext <-
AIEngineList.HasNext <-
AIGroupList.HasNext <-
AIIndustryList.HasNext <-
AIIndustryList_CargoAccepting.HasNext <-
AIIndustryList_CargoProducing.HasNext <-
AIIndustryTypeList.HasNext <-
AIList.HasNext <-
AIRailTypeList.HasNext <-
AISignList.HasNext <-
AIStationList.HasNext <-
AIStationList_Vehicle.HasNext <-
AISubsidyList.HasNext <-
AITileList.HasNext <-
AITileList_IndustryAccepting.HasNext <-
AITileList_IndustryProducing.HasNext <-
AITileList_StationType.HasNext <-
AITownList.HasNext <-
AIVehicleList.HasNext <-
AIVehicleList_DefaultGroup.HasNext <-
AIVehicleList_Depot.HasNext <-
AIVehicleList_Group.HasNext <-
AIVehicleList_SharedOrders.HasNext <-
AIVehicleList_Station.HasNext <-
AIWaypointList.HasNext <-
AIWaypointList_Vehicle.HasNext <-
function()
{
	return !this.IsEnd();
}

AIIndustry._IsCargoAccepted <- AIIndustry.IsCargoAccepted;
AIIndustry.IsCargoAccepted <- function(industry_id, cargo_id)
{
	return AIIndustry._IsCargoAccepted(industry_id, cargo_id) != AIIndustry.CAS_NOT_ACCEPTED;
}

AIAbstractList <- AIList;

AIList.ChangeItem <- AIList.SetValue;

AIRail.ERR_NONUNIFORM_STATIONS_DISABLED <- 0xFFFF;

AICompany.GetCompanyValue <- function(company)
{
	return AICompany.GetQuarterlyCompanyValue(company, AICompany.CURRENT_QUARTER);
}

AITown.GetLastMonthTransported <- AITown.GetLastMonthSupplied;

AIEvent.AI_ET_INVALID <- AIEvent.ET_INVALID;
AIEvent.AI_ET_TEST <- AIEvent.ET_TEST;
AIEvent.AI_ET_SUBSIDY_OFFER <- AIEvent.ET_SUBSIDY_OFFER;
AIEvent.AI_ET_SUBSIDY_OFFER_EXPIRED <- AIEvent.ET_SUBSIDY_OFFER_EXPIRED;
AIEvent.AI_ET_SUBSIDY_AWARDED <- AIEvent.ET_SUBSIDY_AWARDED;
AIEvent.AI_ET_SUBSIDY_EXPIRED <- AIEvent.ET_SUBSIDY_EXPIRED;
AIEvent.AI_ET_ENGINE_PREVIEW <- AIEvent.ET_ENGINE_PREVIEW;
AIEvent.AI_ET_COMPANY_NEW <- AIEvent.ET_COMPANY_NEW;
AIEvent.AI_ET_COMPANY_IN_TROUBLE <- AIEvent.ET_COMPANY_IN_TROUBLE;
AIEvent.AI_ET_COMPANY_ASK_MERGER <- AIEvent.ET_COMPANY_ASK_MERGER;
AIEvent.AI_ET_COMPANY_MERGER <- AIEvent.ET_COMPANY_MERGER;
AIEvent.AI_ET_COMPANY_BANKRUPT <- AIEvent.ET_COMPANY_BANKRUPT;
AIEvent.AI_ET_VEHICLE_CRASHED <- AIEvent.ET_VEHICLE_CRASHED;
AIEvent.AI_ET_VEHICLE_LOST <- AIEvent.ET_VEHICLE_LOST;
AIEvent.AI_ET_VEHICLE_WAITING_IN_DEPOT <- AIEvent.ET_VEHICLE_WAITING_IN_DEPOT;
AIEvent.AI_ET_VEHICLE_UNPROFITABLE <- AIEvent.ET_VEHICLE_UNPROFITABLE;
AIEvent.AI_ET_INDUSTRY_OPEN <- AIEvent.ET_INDUSTRY_OPEN;
AIEvent.AI_ET_INDUSTRY_CLOSE <- AIEvent.ET_INDUSTRY_CLOSE;
AIEvent.AI_ET_ENGINE_AVAILABLE <- AIEvent.ET_ENGINE_AVAILABLE;
AIEvent.AI_ET_STATION_FIRST_VEHICLE <- AIEvent.ET_STATION_FIRST_VEHICLE;
AIEvent.AI_ET_DISASTER_ZEPPELINER_CRASHED <- AIEvent.ET_DISASTER_ZEPPELINER_CRASHED;
AIEvent.AI_ET_DISASTER_ZEPPELINER_CLEARED <- AIEvent.ET_DISASTER_ZEPPELINER_CLEARED;
AIOrder.AIOF_NONE <- AIOrder.OF_NONE
AIOrder.AIOF_NON_STOP_INTERMEDIATE <- AIOrder.OF_NON_STOP_INTERMEDIATE
AIOrder.AIOF_NON_STOP_DESTINATION <- AIOrder.OF_NON_STOP_DESTINATION
AIOrder.AIOF_UNLOAD <- AIOrder.OF_UNLOAD
AIOrder.AIOF_TRANSFER <- AIOrder.OF_TRANSFER
AIOrder.AIOF_NO_UNLOAD <- AIOrder.OF_NO_UNLOAD
AIOrder.AIOF_FULL_LOAD <- AIOrder.OF_FULL_LOAD
AIOrder.AIOF_FULL_LOAD_ANY <- AIOrder.OF_FULL_LOAD_ANY
AIOrder.AIOF_NO_LOAD <- AIOrder.OF_NO_LOAD
AIOrder.AIOF_SERVICE_IF_NEEDED <- AIOrder.OF_SERVICE_IF_NEEDED
AIOrder.AIOF_STOP_IN_DEPOT <- AIOrder.OF_STOP_IN_DEPOT
AIOrder.AIOF_GOTO_NEAREST_DEPOT <- AIOrder.OF_GOTO_NEAREST_DEPOT
AIOrder.AIOF_NON_STOP_FLAGS <- AIOrder.OF_NON_STOP_FLAGS
AIOrder.AIOF_UNLOAD_FLAGS <- AIOrder.OF_UNLOAD_FLAGS
AIOrder.AIOF_LOAD_FLAGS <- AIOrder.OF_LOAD_FLAGS
AIOrder.AIOF_DEPOT_FLAGS <- AIOrder.OF_DEPOT_FLAGS
AIOrder.AIOF_INVALID <- AIOrder.OF_INVALID

/* 1.9 adds a vehicle type parameter. */
AIBridge._GetName <- AIBridge.GetName;
AIBridge.GetName <- function(bridge_id)
{
	return AIBridge._GetName(bridge_id, AIVehicle.VT_RAIL);
}

/* 1.9 adds parent_group_id to CreateGroup function */
AIGroup._CreateGroup <- AIGroup.CreateGroup;
AIGroup.CreateGroup <- function(vehicle_type)
{
	return AIGroup._CreateGroup(vehicle_type, AIGroup.GROUP_INVALID);
}

/* 13 really checks RoadType against RoadType */
AIRoad._HasRoadType <- AIRoad.HasRoadType;
AIRoad.HasRoadType <- function(tile, road_type)
{
	local list = AIRoadTypeList(AIRoad.GetRoadTramType(road_type));
	foreach (rt, _ in list) {
		if (AIRoad._HasRoadType(tile, rt)) {
			return true;
		}
	}
	return false;
}

/* 15 renames GetBridgeID */
AIBridge.GetBridgeID <- AIBridge.GetBridgeType;

AIList.Valuate <-
AIBridgeList.Valuate <-
AIBridgeList_Length.Valuate <-
AICargoList.Valuate <-
AICargoList_IndustryAccepting.Valuate <-
AICargoList_IndustryProducing.Valuate <-
AICargoList_StationAccepting.Valuate <-
AIDepotList.Valuate <-
AIEngineList.Valuate <-
AIGroupList.Valuate <-
AIIndustryList.Valuate <-
AIIndustryList_CargoAccepting.Valuate <-
AIIndustryList_CargoProducing.Valuate <-
AIIndustryTypeList.Valuate <-
AINewGRFList.Valuate <-
AIObjectTypeList.Valuate <-
AIRailTypeList.Valuate <-
AIRoadTypeList.Valuate <-
AISignList.Valuate <-
AIStationList.Valuate <-
AIStationList_Cargo.Valuate <-
AIStationList_CargoPlanned.Valuate <-
AIStationList_CargoWaiting.Valuate <-
AIStationList_CargoWaitingByFrom.Valuate <-
AIStationList_CargoWaitingViaByFrom.Valuate <-
AIStationList_CargoWaitingByVia.Valuate <-
AIStationList_CargoWaitingFromByVia.Valuate <-
AIStationList_CargoPlannedByFrom.Valuate <-
AIStationList_CargoPlannedViaByFrom.Valuate <-
AIStationList_CargoPlannedByVia.Valuate <-
AIStationList_CargoPlannedFromByVia.Valuate <-
AIStationList_Vehicle.Valuate <-
AISubsidyList.Valuate <-
AITileList.Valuate <-
AITileList_IndustryAccepting.Valuate <-
AITileList_IndustryProducing.Valuate <-
AITileList_StationType.Valuate <-
AITileList_StationCoverage.Valuate <-
AITownList.Valuate <-
AITownEffectList.Valuate <-
AIVehicleList.Valuate <-
AIVehicleList_Station.Valuate <-
AIVehicleList_Depot.Valuate <-
AIVehicleList_SharedOrders.Valuate <-
AIVehicleList_Group.Valuate <-
AIVehicleList_DefaultGroup.Valuate <-
AIWaypointList.Valuate <-
AIWaypointList_Vehicle.Valuate <-
function(valuator_function, ...)
{
	local copy_list = AIList();
	copy_list.AddList(this);
	copy_list.Sort(AIList.SORT_BY_ITEM, AIList.SORT_ASCENDING);

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
			AILog.Error("too many arguments in valuator function");
			assert(false);
		}
	}

	this.Clear();
	this.AddList(copy_list);
}
