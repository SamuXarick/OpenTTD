/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file vehiclelist_func.h Functions and type for generating vehicle lists. */

#ifndef VEHICLELIST_FUNC_H
#define VEHICLELIST_FUNC_H

#include "order_base.h"
#include "vehicle_base.h"

/**
 * Find vehicles matching an order.
 * This can be used, e.g. to find all vehicles that stop at a particular station.
 * @param company_pred Company selection predicate.
 * @param veh_pred VehicleType selection predicate.
 * @param ord_pred Order selection predicate.
 * @param veh_func Called for each vehicle that matches company, vehicle type and order predicates.
 **/
template <class CompanyPredicate, class VehiclePredicate, class OrderPredicate, class VehicleFunc>
void FindVehiclesWithOrder(CompanyPredicate company_pred, VehiclePredicate veh_pred, OrderPredicate ord_pred, VehicleFunc veh_func)
{
	for (const Company *c : Company::Iterate()) {
		if (!company_pred(c)) continue;

		for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
			if (!veh_pred(type)) continue;

			/* We assume all vehicles in the order lists are the first in a shared chain. */
			for (const Vehicle *v : c->order_lists[type]) {

				/* Vehicle is a candidate, search for a matching order. */
				for (const Order &order : v->Orders()) {
					if (!ord_pred(&order)) continue;

					/* An order matches, we can add all shared vehicles to the list. */
					for (; v != nullptr; v = v->NextShared()) {
						veh_func(v);
					}
					break;
				}
			}
		}
	}
}

/**
 * Find vehicles matching an order.
 * This can be used, e.g. to find all vehicles that stop at a particular station.
 * @param company_pred Company selection predicate.
 * @param ord_pred Order selection predicate.
 * @param veh_func Called for each vehicle that matches company and order predicates.
 **/
template <class CompanyPredicate, class OrderPredicate, class VehicleFunc>
void FindVehiclesWithOrder(CompanyPredicate company_pred, OrderPredicate ord_pred, VehicleFunc veh_func)
{
	FindVehiclesWithOrder(company_pred, [](VehicleType) { return true; }, ord_pred, veh_func);
}

#endif /* VEHICLELIST_FUNC_H */
