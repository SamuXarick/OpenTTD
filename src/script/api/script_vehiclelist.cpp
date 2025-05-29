/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_vehiclelist.cpp Implementation of ScriptVehicleList and friends. */

#include "../../stdafx.h"
#include "script_vehiclelist.hpp"
#include "script_group.hpp"
#include "script_map.hpp"
#include "script_station.hpp"
#include "script_waypoint.hpp"
#include "../../depot_map.h"
#include "../../vehicle_base.h"
#include "../../train.h"

#include "../../safeguards.h"

ScriptVehicleList::ScriptVehicleList(HSQUIRRELVM vm)
{
	EnforceDeityOrCompanyModeValid_Void();

	bool is_deity = ScriptCompanyMode::IsDeity();
	::CompanyID owner = ScriptObject::GetCompany();

	ScriptList::FillList<Vehicle>(vm, this,
		[is_deity, owner](const Vehicle *v) {
			return (is_deity || v->owner == owner) && (v->IsPrimaryVehicle() || (v->type == VEH_TRAIN && ::Train::From(v)->IsFreeWagon()));
		}
	);
}

ScriptVehicleList_Station::ScriptVehicleList_Station(HSQUIRRELVM vm)
{
	EnforceDeityOrCompanyModeValid_Void();

	int nparam = sq_gettop(vm) - 1;

	if (nparam < 1 || nparam > 2) throw sq_throwerror(vm, "wrong number of parameters");

	SQInteger sqstationid;
	if (SQ_FAILED(sq_getinteger(vm, 2, &sqstationid))) {
		throw sq_throwerror(vm, "parameter 1 must be an integer");
	}
	StationID station_id = static_cast<StationID>(sqstationid);
	if (!ScriptBaseStation::IsValidBaseStation(station_id)) return;

	bool is_deity = ScriptCompanyMode::IsDeity();
	::CompanyID owner = ScriptObject::GetCompany();
	::VehicleType type = VEH_INVALID;

	if (nparam == 2) {
		SQInteger sqtype;
		if (SQ_FAILED(sq_getinteger(vm, 3, &sqtype))) {
			throw sq_throwerror(vm, "parameter 2 must be an integer");
		}
		if (sqtype < ScriptVehicle::VT_RAIL || sqtype > ScriptVehicle::VT_AIR) return;
		type = static_cast<::VehicleType>(sqtype);
	}

	for (const Company *c : Company::Iterate()) {
		if (c->index != owner && !is_deity) continue;
		for (VehicleType vtype = VEH_BEGIN; vtype < VEH_COMPANY_END; vtype++) {
			if (type != VEH_INVALID && vtype != type) continue;
			for (const Vehicle *v : c->group_all[vtype].vehicle_list) {
				for (const Order &order : v->Orders()) {
					if ((order.IsType(OT_GOTO_STATION) || order.IsType(OT_GOTO_WAYPOINT)) && order.GetDestination() == station_id) {
						this->AddItem(v->index.base());
						break;
					}
				}
			}
		}
	}
}

ScriptVehicleList_Waypoint::ScriptVehicleList_Waypoint(StationID waypoint_id)
{
	EnforceDeityOrCompanyModeValid_Void();
	if (!ScriptWaypoint::IsValidWaypoint(waypoint_id)) return;

	bool is_deity = ScriptCompanyMode::IsDeity();
	::CompanyID owner = ScriptObject::GetCompany();

	for (const Company *c : Company::Iterate()) {
		if (c->index != owner && !is_deity) continue;
		for (VehicleType vtype = VEH_BEGIN; vtype < VEH_AIRCRAFT; vtype++) { // There are no aircraft waypoints in the game
			for (const Vehicle *v : c->group_all[vtype].vehicle_list) {
				for (const Order &order : v->Orders()) {
					if (order.IsType(OT_GOTO_WAYPOINT) && order.GetDestination() == waypoint_id) {
						this->AddItem(v->index.base());
						break;
					}
				}
			}
		}
	}
}

ScriptVehicleList_Depot::ScriptVehicleList_Depot(TileIndex tile)
{
	EnforceDeityOrCompanyModeValid_Void();
	if (!ScriptMap::IsValidTile(tile)) return;

	DestinationID dest;
	VehicleType type;

	switch (GetTileType(tile)) {
		case MP_STATION: // Aircraft
			if (!IsAirport(tile)) return;
			type = VEH_AIRCRAFT;
			dest = GetStationIndex(tile);
			break;

		case MP_RAILWAY:
			if (!IsRailDepot(tile)) return;
			type = VEH_TRAIN;
			dest = GetDepotIndex(tile);
			break;

		case MP_ROAD:
			if (!IsRoadDepot(tile)) return;
			type = VEH_ROAD;
			dest = GetDepotIndex(tile);
			break;

		case MP_WATER:
			if (!IsShipDepot(tile)) return;
			type = VEH_SHIP;
			dest = GetDepotIndex(tile);
			break;

		default: // No depot
			return;
	}

	bool is_deity = ScriptCompanyMode::IsDeity();
	::CompanyID owner = ScriptObject::GetCompany();

	for (const Company *c : Company::Iterate()) {
		if (c->index != owner && !is_deity) continue;
		for (const Vehicle *v : c->group_all[type].vehicle_list) {
			for (const Order &order : v->Orders()) {
				if (order.IsType(OT_GOTO_DEPOT) && order.GetDestination() == dest) {
					this->AddItem(v->index.base());
					break;
				}
			}
		}
	}
}

ScriptVehicleList_SharedOrders::ScriptVehicleList_SharedOrders(VehicleID vehicle_id)
{
	if (!ScriptVehicle::IsPrimaryVehicle(vehicle_id)) return;

	for (const Vehicle *v = Vehicle::Get(vehicle_id)->FirstShared(); v != nullptr; v = v->NextShared()) {
		this->AddItem(v->index.base());
	}
}

ScriptVehicleList_Group::ScriptVehicleList_Group(GroupID group_id)
{
	EnforceCompanyModeValid_Void();
	if (!ScriptGroup::IsValidGroup(group_id)) return;

	for (const Vehicle *v : ::Group::Get(group_id)->statistics.vehicle_list) {
		this->AddItem(v->index.base());
	}
}

ScriptVehicleList_DefaultGroup::ScriptVehicleList_DefaultGroup(ScriptVehicle::VehicleType vehicle_type)
{
	EnforceCompanyModeValid_Void();
	if (vehicle_type < ScriptVehicle::VT_RAIL || vehicle_type > ScriptVehicle::VT_AIR) return;

	::CompanyID owner = ScriptObject::GetCompany();

	for (const Vehicle *v : Company::Get(owner)->group_default[(::VehicleType)vehicle_type].vehicle_list) {
		this->AddItem(v->index.base());
	}
}
