/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

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
GSVehicleList_Waypoint.Valuate <-
GSVehicleList_Depot.Valuate <-
GSVehicleList_SharedOrders.Valuate <-
GSVehicleList_Group.Valuate <-
GSVehicleList_DefaultGroup.Valuate <-
GSWaypointList.Valuate <-
GSWaypointList_Vehicle.Valuate <-
function(valuator_function, ...)
{
	local list = GSList();

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

GSTownList.constructorCompat15 <- GSTownList.constructor
GSVehicleList.constructorCompat15 <- GSVehicleList.constructor
GSSubsidyList.constructorCompat15 <- GSSubsidyList.constructor
GSSignList.constructorCompat15 <- GSSignList.constructor
GSIndustryList.constructorCompat15 <- GSIndustryList.constructor
GSGroupList.constructorCompat15 <- GSGroupList.constructor

GSTownList.constructor <-
GSVehicleList.constructor <-
GSSubsidyList.constructor <-
GSSignList.constructor <-
GSIndustryList.constructor <-
GSGroupList.constructor <-
function(...)
{
	this.constructorCompat15();
	if (!vargc) return;
	switch (vargc) {
		case 1: foreach (item, _ in this) if (!vargv[0](item)) this[item] = null; return;
		case 2: foreach (item, _ in this) if (!vargv[0](item, vargv[1])) this[item] = null; return;
		case 3: foreach (item, _ in this) if (!vargv[0](item, vargv[1], vargv[2])) this[item] = null; return;
		case 4: foreach (item, _ in this) if (!vargv[0](item, vargv[1], vargv[2], vargv[3])) this[item] = null; return;
		case 5: foreach (item, _ in this) if (!vargv[0](item, vargv[1], vargv[2], vargv[3], vargv[4])) this[item] = null; return;
		case 6: foreach (item, _ in this) if (!vargv[0](item, vargv[1], vargv[2], vargv[3], vargv[4], vargv[5])) this[item] = null; return;
		case 7: foreach (item, _ in this) if (!vargv[0](item, vargv[1], vargv[2], vargv[3], vargv[4], vargv[5], vargv[6])) this[item] = null; return;
		case 8: foreach (item, _ in this) if (!vargv[0](item, vargv[1], vargv[2], vargv[3], vargv[4], vargv[5], vargv[6], vargv[7])) this[item] = null; return;
		case 9: foreach (item, _ in this) if (!vargv[0](item, vargv[1], vargv[2], vargv[3], vargv[4], vargv[5], vargv[6], vargv[7], vargv[8])) this[item] = null; return;
		case 10: foreach (item, _ in this) if (!vargv[0](item, vargv[1], vargv[2], vargv[3], vargv[4], vargv[5], vargv[6], vargv[7], vargv[8], vargv[9])) this[item] = null; return;
		case 11: foreach (item, _ in this) if (!vargv[0](item, vargv[1], vargv[2], vargv[3], vargv[4], vargv[5], vargv[6], vargv[7], vargv[8], vargv[9], vargv[10])) this[item] = null; return;
		default: throw "Too many arguments in filter function";
	}
}
