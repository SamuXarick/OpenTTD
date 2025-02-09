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
