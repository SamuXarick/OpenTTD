/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

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
AIVehicleList_Waypoint.Valuate <-
AIVehicleList_Depot.Valuate <-
AIVehicleList_SharedOrders.Valuate <-
AIVehicleList_Group.Valuate <-
AIVehicleList_DefaultGroup.Valuate <-
AIWaypointList.Valuate <-
AIWaypointList_Vehicle.Valuate <-
function(valuator_function, ...)
{
	local list = AIList();

	switch (vargc) {
		case 0: foreach (item, _ in this) list[item] = valuator_function(item).tointeger(); this.Clear(); this.AddList(list); return;
		case 1: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0]).tointeger(); this.Clear(); this.AddList(list); return;
		case 2: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0], vargv[1]).tointeger(); this.Clear(); this.AddList(list); return;
		case 3: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0], vargv[1], vargv[2]).tointeger(); this.Clear(); this.AddList(list); return;
		case 4: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0], vargv[1], vargv[2], vargv[3]).tointeger(); this.Clear(); this.AddList(list); return;
		case 5: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0], vargv[1], vargv[2], vargv[3], vargv[4]).tointeger(); this.Clear(); this.AddList(list); return;
		case 6: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0], vargv[1], vargv[2], vargv[3], vargv[4], vargv[5]).tointeger(); this.Clear(); this.AddList(list); return;
		case 7: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0], vargv[1], vargv[2], vargv[3], vargv[4], vargv[5], vargv[6]).tointeger(); this.Clear(); this.AddList(list); return;
		case 8: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0], vargv[1], vargv[2], vargv[3], vargv[4], vargv[5], vargv[6], vargv[7]).tointeger(); this.Clear(); this.AddList(list); return;
		case 9: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0], vargv[1], vargv[2], vargv[3], vargv[4], vargv[5], vargv[6], vargv[7], vargv[8]).tointeger(); this.Clear(); this.AddList(list); return;
		case 10: foreach (item, _ in this) list[item] = valuator_function(item, vargv[0], vargv[1], vargv[2], vargv[3], vargv[4], vargv[5], vargv[6], vargv[7], vargv[8], vargv[9]).tointeger(); this.Clear(); this.AddList(list); return;
		default: throw "Too many arguments in valuator function";
	}
}
