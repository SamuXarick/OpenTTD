/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_vehiclelist.hpp List all the vehicles (you own). */

#ifndef SCRIPT_VEHICLELIST_HPP
#define SCRIPT_VEHICLELIST_HPP

#include "script_list.hpp"
#include "script_vehicle.hpp"
#include "../../vehicle_base.h"

/**
 * Creates a list of vehicles of which you are the owner.
 * @api ai game
 * @ingroup ScriptList
 */
class ScriptVehicleList : public ScriptList {
public:
#ifdef DOXYGEN_API
	ScriptVehicleList();

	/**
	 * Apply a filter when building the list.
	 * @param filter_function The function which will be doing the filtering.
	 * @param ... The params to give to the filters (minus the first param,
	 *  which is always the index-value).
	 * @note You can write your own filters and use them. Just remember that
	 *  the first parameter should be the index-value, and it should return
	 *  a bool.
	 * @note Example:
	 * @code
	 *  local vehs_in_depot = ScriptVehicleList(ScriptVehicle.IsInDepot);
	 *
	 *  function IsType(vehicle_id, type)
	 *  {
	 *    return ScriptVehicle.GetVehicleType(vehicle_id) == type;
	 *  }
	 *  local road_vehs = ScriptVehicleList(IsType, ScriptVehicle.VT_ROAD);
	 * @endcode
	 */
	ScriptVehicleList(function filter_function, ...);
#else
	/**
	 * The constructor wrapper from Squirrel.
	 */
	ScriptVehicleList(HSQUIRRELVM vm);
#endif /* DOXYGEN_API */
protected:
	template <class CompanyPredicate, class VehicleFilter>
	static void FindVehiclesAndFreeWagons(ScriptVehicleList *list, CompanyPredicate company_pred, VehicleFilter veh_filter)
	{
		FindVehicles(company_pred,
			[&list, &veh_filter](const Vehicle *v) { if (veh_filter(v)) list->AddItem(v->index.base()); }
		);

		/* Find free wagons. */
		for (const Company *c : Company::Iterate()) {
			if (!company_pred(c)) continue;

			for (const Vehicle *v : c->free_wagons) {
				if (!veh_filter(v)) continue;

				list->AddItem(v->index.base());
			}
		}
	}

	template <class CompanyPredicate>
	static void FindVehiclesAndFreeWagons(ScriptVehicleList *list, CompanyPredicate company_pred)
	{
		ScriptVehicleList::FindVehiclesAndFreeWagons(list, company_pred, [](const Vehicle *) { return true; });
	}

	template <class CompanyPredicate>
	static void FindVehiclesAndFreeWagons(HSQUIRRELVM vm, ScriptVehicleList *list, CompanyPredicate company_pred)
	{
		int nparam = sq_gettop(vm) - 1;
		if (nparam >= 1) {
			/* Make sure the filter function is really a function, and not any
			 * other type. It's parameter 2 for us, but for the user it's the
			 * first parameter they give. */
			SQObjectType valuator_type = sq_gettype(vm, 2);
			if (valuator_type != OT_CLOSURE && valuator_type != OT_NATIVECLOSURE) {
				throw sq_throwerror(vm, "parameter 1 has an invalid type (expected function)");
			}

			/* Push the function to call */
			sq_push(vm, 2);
		}

		/* Don't allow docommand from a filter, as we can't resume in
		 * mid C++-code. */
		ScriptObject::DisableDoCommandScope disabler{};

		if (nparam < 1) {
			ScriptVehicleList::FindVehiclesAndFreeWagons(list, company_pred);
		} else {
			/* Limit the total number of ops that can be consumed by a filter operation, if a filter function is present */
			SQOpsLimiter limiter(vm, MAX_VALUATE_OPS, "list filter function");

			ScriptVehicleList::FindVehiclesAndFreeWagons(list, company_pred,
				[vm, nparam](const Vehicle *v) {
					/* Push the root table as instance object, this is what squirrel does for meta-functions. */
					sq_pushroottable(vm);
					/* Push all arguments for the valuator function. */
					sq_pushinteger(vm, GetRawIndex(v->index));
					for (int i = 0; i < nparam - 1; i++) {
						sq_push(vm, i + 3);
					}

					/* Call the function. Squirrel pops all parameters and pushes the return value. */
					if (SQ_FAILED(sq_call(vm, nparam + 1, SQTrue, SQFalse))) {
						throw static_cast<SQInteger>(SQ_ERROR);
					}

					SQBool add = SQFalse;

					/* Retrieve the return value */
					switch (sq_gettype(vm, -1)) {
						case OT_BOOL:
							sq_getbool(vm, -1, &add);
							break;

						default:
							throw sq_throwerror(vm, "return value of filter is not valid (not bool)");
					}

					/* Pop the return value. */
					sq_poptop(vm);

					return add;
				}
			);

			/* Pop the filter function */
			sq_poptop(vm);
		}
	}
};

/**
 * Creates a list of vehicles that have orders to a given station.
 * @api ai game
 * @ingroup ScriptList
 */
class ScriptVehicleList_Station : public ScriptList {
public:
#ifdef DOXYGEN_API
	/**
	 * @param station_id The station to get the list of vehicles from, which have orders to it.
	 * @pre ScriptBaseStation::IsValidBaseStation(station_id)
	 */
	ScriptVehicleList_Station(StationID station_id);

	/**
	 * @param station_id The station to get the list of vehicles from, which have orders to it.
	 * @param vehicle_type The VehicleType to get the list of vehicles for.
	 * @pre ScriptBaseStation::IsValidBaseStation(station_id)
	 */
	ScriptVehicleList_Station(StationID station_id, ScriptVehicle::VehicleType vehicle_type);
#else
	/**
	 * The constructor wrapper from Squirrel.
	 */
	ScriptVehicleList_Station(HSQUIRRELVM vm);
#endif /* DOXYGEN_API */
};

/**
 * Creates a list of vehicles that have orders to a given waypoint.
 * @api ai game
 * @ingroup ScriptList
 */
class ScriptVehicleList_Waypoint : public ScriptList {
public:
	/**
	 * @param waypoint_id The waypoint to get the list of vehicles from, which have orders to it.
	 * @pre ScriptWaypoint::IsValidWaypoint(waypoint_id)
	 */
	ScriptVehicleList_Waypoint(StationID waypoint_id);
};

/**
 * Creates a list of vehicles that have orders to a given depot.
 * The list is created with a tile. If the tile is part of an airport all
 * aircraft having a depot order on a hangar of that airport will be
 * returned. For all other vehicle types the tile has to be a depot or
 * an empty list will be returned.
 * @api ai game
 * @ingroup ScriptList
 */
class ScriptVehicleList_Depot : public ScriptList {
public:
	/**
	 * @param tile The tile of the depot to get the list of vehicles from, which have orders to it.
	 */
	ScriptVehicleList_Depot(TileIndex tile);
};

/**
 * Creates a list of vehicles that share orders.
 * @api ai game
 * @ingroup ScriptList
 */
class ScriptVehicleList_SharedOrders : public ScriptList {
public:
	/**
	 * @param vehicle_id The vehicle that the rest shared orders with.
	 */
	ScriptVehicleList_SharedOrders(VehicleID vehicle_id);
};

/**
 * Creates a list of vehicles that are in a group.
 * @api ai game
 * @ingroup ScriptList
 */
class ScriptVehicleList_Group : public ScriptList {
public:
	/**
	 * @param group_id The ID of the group the vehicles are in.
	 * @game @pre ScriptCompanyMode::IsValid().
	 */
	ScriptVehicleList_Group(GroupID group_id);
};

/**
 * Creates a list of vehicles that are in the default group.
 * @api ai game
 * @ingroup ScriptList
 */
class ScriptVehicleList_DefaultGroup : public ScriptList {
public:
	/**
	 * @param vehicle_type The VehicleType to get the list of vehicles for.
	 * @game @pre ScriptCompanyMode::IsValid().
	 */
	ScriptVehicleList_DefaultGroup(ScriptVehicle::VehicleType vehicle_type);
};

#endif /* SCRIPT_VEHICLELIST_HPP */
