/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file cachecheck.cpp Check caches. */

#include "stdafx.h"
#include "aircraft.h"
#include "company_base.h"
#include "debug.h"
#include "industry.h"
#include "roadstop_base.h"
#include "roadveh.h"
#include "ship.h"
#include "station_base.h"
#include "station_map.h"
#include "subsidy_func.h"
#include "town.h"
#include "train.h"
#include "vehicle_func.h"
#include "depot_base.h"

#include "safeguards.h"

extern void AfterLoadCompanyStats();
extern void RebuildTownCaches();

/**
 * Check the validity of some of the caches.
 * Especially in the sense of desyncs between
 * the cached value and what the value would
 * be when calculated from the 'base' data.
 */
void CheckCaches()
{
	/* Return here so it is easy to add checks that are run
	 * always to aid testing of caches. */
	if (_debug_desync_level <= 1) return;

	/* Check the town caches. */
	std::vector<TownCache> old_town_caches;
	for (const Town *t : Town::Iterate()) {
		old_town_caches.push_back(t->cache);
	}

	RebuildTownCaches();
	RebuildSubsidisedSourceAndDestinationCache();

	uint i = 0;
	for (Town *t : Town::Iterate()) {
		if (old_town_caches[i] != t->cache) {
			Debug(desync, 2, "warning: town cache mismatch: town {}", t->index);
		}
		i++;
	}

	/* Check company infrastructure cache. */
	std::vector<CompanyInfrastructure> old_infrastructure;
	for (const Company *c : Company::Iterate()) old_infrastructure.push_back(c->infrastructure);

	AfterLoadCompanyStats();

	i = 0;
	for (const Company *c : Company::Iterate()) {
		if (old_infrastructure[i] != c->infrastructure) {
			Debug(desync, 2, "warning: infrastructure cache mismatch: company {}", c->index);
		}
		i++;
	}

	/* Strict checking of the road stop cache entries */
	for (const RoadStop *rs : RoadStop::Iterate()) {
		if (IsBayRoadStopTile(rs->xy)) continue;

		rs->GetEntry(DIAGDIR_NE).CheckIntegrity(rs);
		rs->GetEntry(DIAGDIR_NW).CheckIntegrity(rs);
	}

	std::vector<NewGRFCache> grf_cache;
	std::vector<VehicleCache> veh_cache;
	std::vector<GroundVehicleCache> gro_cache;
	std::vector<TrainCache> tra_cache;

	for (Vehicle *v : Vehicle::Iterate()) {
		if (v != v->First() || v->vehstatus.Test(VehState::Crashed) || !v->IsPrimaryVehicle()) continue;

		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			FillNewGRFVehicleCache(u);
			grf_cache.emplace_back(u->grf_cache);
			veh_cache.emplace_back(u->vcache);
			switch (u->type) {
				case VEH_TRAIN:
					gro_cache.emplace_back(Train::From(u)->gcache);
					tra_cache.emplace_back(Train::From(u)->tcache);
					break;
				case VEH_ROAD:
					gro_cache.emplace_back(RoadVehicle::From(u)->gcache);
					break;
				default:
					break;
			}
		}

		switch (v->type) {
			case VEH_TRAIN:    Train::From(v)->ConsistChanged(CCF_TRACK); break;
			case VEH_ROAD:     RoadVehUpdateCache(RoadVehicle::From(v)); break;
			case VEH_AIRCRAFT: UpdateAircraftCache(Aircraft::From(v));   break;
			case VEH_SHIP:     Ship::From(v)->UpdateCache();             break;
			default: break;
		}

		uint length = 0;
		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			FillNewGRFVehicleCache(u);
			if (grf_cache[length] != u->grf_cache) {
				Debug(desync, 2, "warning: newgrf cache mismatch: type {}, vehicle {}, company {}, unit number {}, wagon {}", v->type, v->index, v->owner, v->unitnumber, length);
			}
			if (veh_cache[length] != u->vcache) {
				Debug(desync, 2, "warning: vehicle cache mismatch: type {}, vehicle {}, company {}, unit number {}, wagon {}", v->type, v->index, v->owner, v->unitnumber, length);
			}
			switch (u->type) {
				case VEH_TRAIN:
					if (gro_cache[length] != Train::From(u)->gcache) {
						Debug(desync, 2, "warning: train ground vehicle cache mismatch: vehicle {}, company {}, unit number {}, wagon {}", v->index, v->owner, v->unitnumber, length);
					}
					if (tra_cache[length] != Train::From(u)->tcache) {
						Debug(desync, 2, "warning: train cache mismatch: vehicle {}, company {}, unit number {}, wagon {}", v->index, v->owner, v->unitnumber, length);
					}
					break;
				case VEH_ROAD:
					if (gro_cache[length] != RoadVehicle::From(u)->gcache) {
						Debug(desync, 2, "warning: road vehicle ground vehicle cache mismatch: vehicle {}, company {}, unit number {}, wagon {}", v->index, v->owner, v->unitnumber, length);
					}
					break;
				default:
					break;
			}
			length++;
		}

		grf_cache.clear();
		veh_cache.clear();
		gro_cache.clear();
		tra_cache.clear();
	}

	/* Check whether the caches are still valid */
	for (Vehicle *v : Vehicle::Iterate()) {
		[[maybe_unused]] const auto a = v->cargo.PeriodsInTransit();
		[[maybe_unused]] const auto b = v->cargo.TotalCount();
		[[maybe_unused]] const auto c = v->cargo.GetFeederShare();
		v->cargo.InvalidateCache();
		assert(a == v->cargo.PeriodsInTransit());
		assert(b == v->cargo.TotalCount());
		assert(c == v->cargo.GetFeederShare());
	}

	/* Backup stations_near */
	std::vector<StationList> old_town_stations_near;
	for (Town *t : Town::Iterate()) old_town_stations_near.push_back(t->stations_near);

	std::vector<StationList> old_industry_stations_near;
	for (Industry *ind : Industry::Iterate()) old_industry_stations_near.push_back(ind->stations_near);

	std::vector<IndustryList> old_station_industries_near;
	for (Station *st : Station::Iterate()) old_station_industries_near.push_back(st->industries_near);

	for (Station *st : Station::Iterate()) {
		for (GoodsEntry &ge : st->goods) {
			if (!ge.HasData()) continue;

			StationCargoList &cargo_list = ge.GetData().cargo;
			[[maybe_unused]] const auto a = cargo_list.PeriodsInTransit();
			[[maybe_unused]] const auto b = cargo_list.TotalCount();
			cargo_list.InvalidateCache();
			assert(a == cargo_list.PeriodsInTransit());
			assert(b == cargo_list.TotalCount());
		}

		/* Check docking tiles */
		TileArea ta;
		std::map<TileIndex, bool> docking_tiles;
		for (TileIndex tile : st->docking_station) {
			ta.Add(tile);
			docking_tiles[tile] = IsDockingTile(tile);
		}
		UpdateStationDockingTiles(st);
		if (ta.tile != st->docking_station.tile || ta.w != st->docking_station.w || ta.h != st->docking_station.h) {
			Debug(desync, 2, "warning: station docking mismatch: station {}, company {}", st->index, st->owner);
		}
		for (TileIndex tile : ta) {
			if (docking_tiles[tile] != IsDockingTile(tile)) {
				Debug(desync, 2, "warning: docking tile mismatch: tile {}", tile);
			}
		}
	}

	Station::RecomputeCatchmentForAll();

	/* Check industries_near */
	i = 0;
	for (Station *st : Station::Iterate()) {
		if (st->industries_near != old_station_industries_near[i]) {
			Debug(desync, 2, "warning: station industries near mismatch: station {}", st->index);
		}
		i++;
	}

	/* Check stations_near */
	i = 0;
	for (Town *t : Town::Iterate()) {
		if (t->stations_near != old_town_stations_near[i]) {
			Debug(desync, 2, "warning: town stations near mismatch: town {}", t->index);
		}
		i++;
	}
	i = 0;
	for (Industry *ind : Industry::Iterate()) {
		if (ind->stations_near != old_industry_stations_near[i]) {
			Debug(desync, 2, "warning: industry stations near mismatch: industry {}", ind->index);
		}
		i++;
	}

	/* Check group vehicle_list */
	for (const Company *c : Company::Iterate()) {
		for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
			for (const Vehicle *v : Vehicle::Iterate()) {
				if (v->type == type && v->owner == c->index && v->IsPrimaryVehicle()) {
					if (std::ranges::find(c->group_all[type].vehicle_list, v) == std::end(c->group_all[type].vehicle_list)) {
						Debug(desync, 2, "warning: group_all vehicle list mismatch, vehicle_id {} missing in group_all of company {} of type {}", v->index, c->index, type);
					}
					if (v->group_id == DEFAULT_GROUP) {
						if (std::ranges::find(c->group_default[type].vehicle_list, v) == std::end(c->group_default[type].vehicle_list)) {
							Debug(desync, 2, "warning: group_default vehicle list mismatch, vehicle_id {} missing in group_default of company {} of type {}", v->index, c->index, type);
						}
					} else if (std::ranges::find(Group::Get(v->group_id)->statistics.vehicle_list, v) == std::end(Group::Get(v->group_id)->statistics.vehicle_list)) {
						Debug(desync, 2, "warning: group vehicle list mismatch: vehicle_id {} missing in group {}", v->index, v->group_id);
					}
				}
			}
			for (const Vehicle *v : c->group_all[type].vehicle_list) {
				if (v == nullptr) {
					Debug(desync, 2, "warning: vehicle in group_all vehicle list mismatch: group_all of company {} of type {} has vehicle_id {} which does not exist", c->index, type, v->index);
					continue;
				}
				for (const Company *c2 : Company::Iterate()) {
					for (VehicleType type2 = VEH_BEGIN; type2 < VEH_COMPANY_END; type2++) {
						if (c2->index == c->index && type2 == type) continue;
						if (std::ranges::find(c2->group_all[type2].vehicle_list, v) != std::end(c2->group_all[type2].vehicle_list)) {
							Debug(desync, 2, "warning: vehicle in group_all vehicle list mismatch: group_all of company {} of type {} has vehicle_id {}, but vehicle is also in group_all of company {} of type {}", c->index, type, v->index, c2->index, type2);
						}
					}
				}
			}
			for (const Vehicle *v : c->group_default[type].vehicle_list) {
				if (v == nullptr) {
					Debug(desync, 2, "warning: vehicle in group_default vehicle list mismatch: group_default of type {} has vehicle_id {} which does not exist", type, v->index);
					continue;
				}
				for (const Company *c2 : Company::Iterate()) {
					for (VehicleType type2 = VEH_BEGIN; type2 < VEH_COMPANY_END; type2++) {
						if (c2->index == c->index && type2 == type) continue;
						if (std::ranges::find(c2->group_default[type2].vehicle_list, v) != std::end(c2->group_default[type2].vehicle_list)) {
							Debug(desync, 2, "warning: vehicle in group_default vehicle list mismatch: group_default of company {} of type {} has vehicle_id {}, but vehicle is also in group_default of company {} of type {}", c->index, type, v->index, c2->index, type2);
						}
					}
				}
				for (const Group *g : Group::Iterate()) {
					if (std::ranges::find(g->statistics.vehicle_list, v) != std::end(g->statistics.vehicle_list)) {
						Debug(desync, 2, "warning: vehicle in group_default vehicle list mismatch: group_default of company {} of type {} has vehicle_id {}, but vehicle is also in group {}", c->index, type, v->index, g->index);
					}
				}
			}
		}
	}
	for (const Group *g : Group::Iterate()) {
		for (const Vehicle *v : g->statistics.vehicle_list) {
			if (v == nullptr) {
				Debug(desync, 2, "warning: vehicle in group vehicle list mismatch: group {} has vehicle_id {} which does not exist", g->index, v->index);
				continue;
			}
			if (v->group_id != g->index) {
				Debug(desync, 2, "warning: vehicle in group vehicle list mismatch: group {} has vehicle_id {}, but vehicle has group {}", g->index, v->index, v->group_id);
			}
			for (const Group *g2 : Group::Iterate()) {
				if (g2->index == g->index) continue;
				if (std::ranges::find(g2->statistics.vehicle_list, v) != std::end(g2->statistics.vehicle_list)) {
					Debug(desync, 2, "warning: vehicle in group vehicle list mismatch: group {} has vehicle_id {}, but vehicle is also in group {}", g->index, v->index, g2->index);
				}
			}
			for (const Company *c : Company::Iterate()) {
				for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
					if (std::ranges::find(c->group_all[type].vehicle_list, v) != std::end(c->group_all[type].vehicle_list)) {
						if (v->type != type || v->owner != c->index) {
							Debug(desync, 2, "warning: vehicle in group vehicle list mismatch: group {} has vehicle_id {} of company {} of type {}, but vehicle is also in group_all of company {} of type {}", g->index, v->index, v->owner, v->type, c->index, type);
						}
					} else if (v->type == type && v->owner == c->index) {
						Debug(desync, 2, "warning: vehicle in group vehicle list mismatch: group {} has vehicle_id {} of company {} of type {}, but vehicle is missing in group_all of company {} of type {}", g->index, v->index, v->owner, v->type, c->index, type);
					}
					if (std::ranges::find(c->group_default[type].vehicle_list, v) != std::end(c->group_default[type].vehicle_list)) {
						Debug(desync, 2, "warning: vehicle in group vehicle list mismatch: group {} has vehicle_id {} of company {} of type {}, but vehicle is also in group_default of company {} of type {}", g->index, v->index, v->owner, v->type, c->index, type);
					}
				}
			}
		}
	}

	/* Check free_wagons caches. */
	for (const Company *c : Company::Iterate()) {
		for (const Depot *d : Depot::Iterate()) {
			if (!IsTileType(d->xy, MP_RAILWAY)) continue;
			for (const Vehicle *v : VehiclesOnTile(d->xy)) {
				if (v->type != VEH_TRAIN || !v->IsInDepot()) continue;
				const Train *t = Train::From(v);
				if (t->IsArticulatedPart() || t->IsRearDualheaded() || !t->IsFreeWagon()) continue;
				if (v->owner == c->index) {
					if (std::ranges::find(c->free_wagons, v) == std::end(c->free_wagons)) {
						Debug(desync, 2, "warning: free wagons list mismatch, vehicle_id {} missing in free_wagons of company {}", v->index, c->index);
					}
				}
			}
			for (const Vehicle *v : c->free_wagons) {
				if (v == nullptr) {
					Debug(desync, 2, "warning: vehicle in free wagons list mismatch: free_wagons of company {} has vehicle_id {} which does not exist", c->index, v->index);
					continue;
				}
				for (const Company *c2 : Company::Iterate()) {
					if (c2->index == c->index) continue;
					if (std::ranges::find(c2->free_wagons, v) != std::end(c2->free_wagons)) {
						Debug(desync, 2, "warning: vehicle in free wagons list mismatch: free_wagons of company {} has vehicle_id {}, but vehicle is also in free_wagons of company {}", c->index, v->index, c2->index);
					}
				}
			}
		}
	}
	for (const Company *c : Company::Iterate()) {
		for (const Vehicle *v : c->free_wagons) {
			if (v == nullptr) {
				Debug(desync, 2, "warning: vehicle in company free wagons list mismatch: company {} has vehicle_id {} which does not exist", c->index, v->index);
				continue;
			}
			for (const Company *c2 : Company::Iterate()) {
				if (c2->index == c->index) continue;
				if (std::ranges::find(c2->free_wagons, v) != std::end(c2->free_wagons)) {
					Debug(desync, 2, "warning: vehicle in company free wagons list mismatch: company {} has vehicle_id {}, but vehicle is also in company {}", c->index, v->index, c2->index);
				}
			}
		}
	}
}
