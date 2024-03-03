/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_ship.cpp Implementation of YAPF for ships. */

#include "../../stdafx.h"
#include "../../ship.h"
#include "../../vehicle_func.h"

#include "yapf.hpp"
#include "yapf_node_ship.hpp"
#include "yapf_ship_regions.h"
#include "../water_regions.h"

#include "../../safeguards.h"

constexpr int NUMBER_OR_WATER_REGIONS_LOOKAHEAD = 4;
constexpr int MAX_SHIP_PF_NODES = (NUMBER_OR_WATER_REGIONS_LOOKAHEAD + 1) * WATER_REGION_NUMBER_OF_TILES * 4; // 4 possible exit dirs per tile.

constexpr int SHIP_LOST_PATH_LENGTH = 8; // The length of the (aimless) path assigned when a ship is lost.

template <class Types>
class CYapfDestinationTileWaterT
{
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class).
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type.
	typedef typename Node::Key Key; ///< key to hash tables.

protected:
	std::span<TileIndex> dest_tiles;
	bool dest_any_depot = false;
	StationID dest_station;

	bool has_intermediate_dest = false;
	TileIndex intermediate_dest_tile;
	WaterRegionPatchDesc intermediate_dest_region_patch;

public:
	void SetDestination(const Ship *v, const std::span<TileIndex> destination_tiles)
	{
		this->dest_tiles = destination_tiles;
		if (v->current_order.IsType(OT_GOTO_STATION)) {
			this->dest_station = v->current_order.GetDestination();
		} else {
			this->dest_station = INVALID_STATION;
		}
	}

	void SetAnyShipDepotDestination(const std::span<TileIndex> destination_tiles)
	{
		this->dest_tiles = destination_tiles;
		this->dest_any_depot = true;
		this->dest_station = INVALID_STATION;
	}

	void SetIntermediateDestination(const WaterRegionPatchDesc &water_region_patch)
	{
		this->has_intermediate_dest = true;
		this->intermediate_dest_tile = GetWaterRegionCenterTile(water_region_patch);
		this->intermediate_dest_region_patch = water_region_patch;
	}

protected:
	/** To access inherited path finder. */
	inline Tpf& Yapf()
	{
		return *static_cast<Tpf*>(this);
	}

public:
	/** Called by YAPF to detect if node ends in the desired destination. */
	inline bool PfDetectDestination(Node &n)
	{
		const TileIndex tile = n.segment_last_tile;

		if (this->has_intermediate_dest) {
			/* GetWaterRegionInfo is much faster than GetWaterRegionPatchInfo so we try that first. */
			if (GetWaterRegionInfo(tile) != this->intermediate_dest_region_patch) return false;
			return GetWaterRegionPatchInfo(tile) == this->intermediate_dest_region_patch;
		}

		if (this->dest_station != INVALID_STATION) return IsDockingTile(tile) && IsShipDestinationTile(tile, this->dest_station);

		if (!this->dest_any_depot) return tile == this->dest_tiles.front();

		return std::find(this->dest_tiles.begin(), this->dest_tiles.end(), tile) != this->dest_tiles.end();
	}

	inline TileIndex GetShipDepotDestination(const WaterRegionPatchDesc &water_region_patch)
	{
		IsShipDepotRegionCallBack HasShipDepot = [&](const TileIndex tile)
		{
			return std::find(this->dest_tiles.begin(), this->dest_tiles.end(), tile) != this->dest_tiles.end();
		};
		return GetShipDepotInWaterRegionPatch(water_region_patch, HasShipDepot);
	}

	static inline int CalcEstimate(Node &n, TileIndex destination_tile)
	{
		static const int dg_dir_to_x_offs[] = { -1, 0, 1, 0 };
		static const int dg_dir_to_y_offs[] = { 0, 1, 0, -1 };

		TileIndex tile = n.segment_last_tile;
		DiagDirection exitdir = TrackdirToExitdir(n.segment_last_td);
		int x1 = 2 * TileX(tile) + dg_dir_to_x_offs[(int)exitdir];
		int y1 = 2 * TileY(tile) + dg_dir_to_y_offs[(int)exitdir];
		int x2 = 2 * TileX(destination_tile);
		int y2 = 2 * TileY(destination_tile);
		int dx = abs(x1 - x2);
		int dy = abs(y1 - y2);
		int dmin = std::min(dx, dy);
		int dxy = abs(dx - dy);
		int d = dmin * YAPF_TILE_CORNER_LENGTH + (dxy - 1) * (YAPF_TILE_LENGTH / 2);
		int estimate = n.cost + d;
		assert(estimate >= n.parent->estimate);
		return estimate;
	}

	/**
	 * Called by YAPF to calculate cost estimate. Calculates distance to the destination
	 * adds it to the actual cost from origin and stores the sum to the Node::estimate.
	 */
	inline bool PfCalcEstimate(Node &n)
	{
		if (this->PfDetectDestination(n)) {
			n.estimate = n.cost;
			return true;
		}

		int shortest_estimate = std::numeric_limits<int>::max();
		if (this->has_intermediate_dest) {
			shortest_estimate = this->CalcEstimate(n, this->intermediate_dest_tile);
		} else {
			for (const TileIndex &destination_tile : this->dest_tiles) {
				int estimate = this->CalcEstimate(n, destination_tile);
				if (estimate < shortest_estimate) shortest_estimate = estimate;
			}
		}

		n.estimate = shortest_estimate;
		return true;
	}
};

/** Node Follower module of YAPF for ships */
template <class Types>
class CYapfFollowShipT
{
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class).
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type.
	typedef typename Node::Key Key; ///< key to hash tables.

protected:
	/** to access inherited path finder */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf*>(this);
	}

	std::vector<WaterRegionDesc> water_region_corridor;

public:
	/**
	 * Called by YAPF to move from the given node to the next tile. For each
	 *  reachable trackdir on the new tile creates new node, initializes it
	 *  and adds it to the open list by calling Yapf().AddNewNode(n)
	 */
	inline void PfFollowNode(Node &old_node)
	{
		TrackFollower F(Yapf().GetVehicle());
		if (F.Follow(old_node.key.tile, old_node.key.td)) {
			if (this->water_region_corridor.empty()
					|| std::ranges::find(this->water_region_corridor, GetWaterRegionInfo(F.new_tile)) != this->water_region_corridor.end()) {
				Yapf().AddMultipleNodes(&old_node, F);
			}
		}
	}

	/** Restricts the search by creating corridor or water regions through which the ship is allowed to travel. */
	inline void RestrictSearch(const std::span<WaterRegionPatchDesc> &path)
	{
		this->water_region_corridor.clear();
		for (const WaterRegionPatchDesc &path_entry : path) this->water_region_corridor.push_back(path_entry);
	}

	/** Return debug report character to identify the transportation type. */
	inline char TransportTypeChar() const
	{
		return 'w';
	}

	/** Returns a random trackdir out of a set of trackdirs. */
	static Trackdir GetRandomTrackdir(TrackdirBits trackdirs)
	{
		const int strip_amount = RandomRange(CountBits(trackdirs));
		for (int s = 0; s < strip_amount; ++s) RemoveFirstTrackdir(&trackdirs);
		return FindFirstTrackdir(trackdirs);
	}

	/** Returns a random tile/trackdir that can be reached from the current tile/trackdir, or tile/INVALID_TRACK if none is available. */
	static std::pair<TileIndex, Trackdir> GetRandomFollowUpTileTrackdir(const Ship *v, TileIndex tile, Trackdir dir)
	{
		TrackFollower follower(v);
		if (follower.Follow(tile, dir)) {
			TrackdirBits dirs = follower.new_td_bits;
			const TrackdirBits dirs_without_90_degree = dirs & ~TrackdirCrossesTrackdirs(dir);
			if (dirs_without_90_degree != TRACKDIR_BIT_NONE) dirs = dirs_without_90_degree;
			return { follower.new_tile, GetRandomTrackdir(dirs) };
		}
		return { follower.new_tile, INVALID_TRACKDIR };
	}

	/** Creates a random path, avoids 90 degree turns. */
	static Trackdir CreateRandomPath(const Ship *v, ShipPathCache &path_cache, int path_length)
	{
		std::pair<TileIndex, Trackdir> tile_dir = { v->tile, v->GetVehicleTrackdir()};
		for (int i = 0; i < path_length; ++i) {
			tile_dir = GetRandomFollowUpTileTrackdir(v, tile_dir.first, tile_dir.second);
			if (tile_dir.second == INVALID_TRACKDIR) break;
			path_cache.push_back(tile_dir.second);
		}

		if (path_cache.empty()) return INVALID_TRACKDIR;

		/* Reverse the path so we can take from the end. */
		std::reverse(std::begin(path_cache), std::end(path_cache));

		const Trackdir result = path_cache.back().trackdir;
		path_cache.pop_back();
		return result;
	}

	static Trackdir ChooseShipTrack(const Ship *v, TileIndex &tile, TrackdirBits forward_dirs, TrackdirBits reverse_dirs, const std::span<TileIndex> dest_tiles, int max_penalty,
		bool &path_found, ShipPathCache &path_cache, Trackdir &best_origin_dir)
	{
		std::vector<WaterRegionPatchDesc> high_level_path = YapfShipFindWaterRegionPath(v, tile, NUMBER_OR_WATER_REGIONS_LOOKAHEAD + 1, dest_tiles);
		if (high_level_path.empty()) {
			path_found = false;
			/* Make the ship move around aimlessly. This prevents repeated pathfinder calls and clearly indicates that the ship is lost. */
			return CreateRandomPath(v, path_cache, SHIP_LOST_PATH_LENGTH);
		}

		const bool find_closest_depot = tile == INVALID_TILE;
		if (find_closest_depot) tile = v->tile;
		const bool automatic_servicing = find_closest_depot && max_penalty != 0;

		/* Try one time without restricting the search area, which generally results in better and more natural looking paths.
		 * However the pathfinder can hit the node limit in certain situations such as long aqueducts or maze-like terrain.
		 * If that happens we run the pathfinder again, but restricted only to the regions provided by the region pathfinder. */
		for (int attempt = 0; attempt < 2; ++attempt) {
			Tpf pf(MAX_SHIP_PF_NODES);

			/* Set origin and destination nodes */
			pf.SetOrigin(v->tile, forward_dirs | reverse_dirs);
			if (find_closest_depot) {
				pf.SetAnyShipDepotDestination(dest_tiles);
			} else {
				pf.SetDestination(v, dest_tiles);
			}
			pf.SetMaxCost(max_penalty);
			const std::span<WaterRegionPatchDesc> high_level_path_span(high_level_path.data(), std::min<size_t>(high_level_path.size(), NUMBER_OR_WATER_REGIONS_LOOKAHEAD + 1));
			const bool is_intermediate_destination = static_cast<int>(high_level_path_span.size()) >= NUMBER_OR_WATER_REGIONS_LOOKAHEAD + 1;
			if (is_intermediate_destination) {
				if (automatic_servicing) {
					/* Automatic servicing requires a valid path cost from start to end.
					 * However, when an intermediate destination is set, the resulting cost
					 * cannot be used to determine if it falls within the maximum allowed penalty. */
					return INVALID_TRACKDIR;
				}
				pf.SetIntermediateDestination(high_level_path_span.back());
			}

			/* Restrict the search area to prevent the low level pathfinder from expanding too many nodes. This can happen
			 * when the terrain is very "maze-like" or when the high level path "teleports" via a very long aqueduct. */
			if (attempt > 0) pf.RestrictSearch(high_level_path_span);

			/* Find best path. */
			path_found = pf.FindPath(v);
			Node *node = pf.GetBestNode();
			if (attempt == 0 && !path_found) continue; // Try again with restricted search area.

			/* Make the ship move around aimlessly. This prevents repeated pathfinder calls and clearly indicates that the ship is lost. */
			if (!path_found) return CreateRandomPath(v, path_cache, SHIP_LOST_PATH_LENGTH);

			/* Return early when only searching for the closest depot tile. */
			if (find_closest_depot) {
				tile = is_intermediate_destination ? pf.GetShipDepotDestination(high_level_path.back()) : node->GetTile();
				return INVALID_TRACKDIR;
			}

			/* Return only the path within the current water region if an intermediate destination was returned. If not, cache the entire path
			 * to the final destination tile. The low-level pathfinder might actually prefer a different docking tile in a nearby region. Without
			 * caching the full path the ship can get stuck in a loop. */
			const WaterRegionPatchDesc end_water_patch = GetWaterRegionPatchInfo(node->GetTile());
			assert(GetWaterRegionPatchInfo(tile) == high_level_path.front());
			const WaterRegionPatchDesc start_water_patch = high_level_path.front();
			while (node->parent) {
				const WaterRegionPatchDesc node_water_patch = GetWaterRegionPatchInfo(node->GetTile());

				const bool node_water_patch_on_high_level_path = std::ranges::find(high_level_path_span, node_water_patch) != high_level_path_span.end();
				const bool add_full_path = !is_intermediate_destination && node_water_patch != end_water_patch;

				/* The cached path must always lead to a region patch that's on the high level path.
				 * This is what can happen when that's not the case https://github.com/OpenTTD/OpenTTD/issues/12176. */
				if (add_full_path || !node_water_patch_on_high_level_path || node_water_patch == start_water_patch) {
					path_cache.push_back(node->GetTrackdir());
				} else {
					path_cache.clear();
				}
				node = node->parent;
			}
			assert(node->GetTile() == v->tile);

			/* Return INVALID_TRACKDIR to trigger a ship reversal if that is the best option. */
			best_origin_dir = node->GetTrackdir();
			if ((TrackdirToTrackdirBits(best_origin_dir) & forward_dirs) == TRACKDIR_BIT_NONE) {
				path_cache.clear();
				return INVALID_TRACKDIR;
			}

			/* A empty path means we are already at the destination. The pathfinder shouldn't have been called at all.
			 * Return a random reachable trackdir to hopefully nudge the ship out of this strange situation. */
			if (path_cache.empty()) return CreateRandomPath(v, path_cache, 1);

			/* Take out the last trackdir as the result. */
			const Trackdir result = path_cache.back().trackdir;
			path_cache.pop_back();

			/* Clear path cache when in final water region patch. This is to allow ships to spread over different docking tiles dynamically. */
			if (start_water_patch == end_water_patch) path_cache.clear();

			return result;
		}

		return INVALID_TRACKDIR;
	}

	/**
	 * Check whether a ship should reverse to reach its destination.
	 * Called when leaving depot.
	 * @param v Ship.
	 * @param trackdir [out] the best of all possible reversed trackdirs.
	 * @param dest_tiles list of destination tiles.
	 * @return true if the reverse direction is better.
	 */
	static bool CheckShipReverse(const Ship *v, Trackdir *trackdir, const std::span<TileIndex> dest_tiles)
	{
		bool path_found = false;
		ShipPathCache dummy_cache;
		TileIndex tile = v->tile;
		Trackdir best_origin_dir = INVALID_TRACKDIR;

		if (trackdir == nullptr) {
			/* The normal case, typically called when ships leave a dock. */
			const Trackdir reverse_dir = ReverseTrackdir(v->GetVehicleTrackdir());
			const TrackdirBits forward_dirs = TrackdirToTrackdirBits(v->GetVehicleTrackdir());
			const TrackdirBits reverse_dirs = TrackdirToTrackdirBits(reverse_dir);
			(void)ChooseShipTrack(v, tile, forward_dirs, reverse_dirs, dest_tiles, 0, path_found, dummy_cache, best_origin_dir);
			return path_found && best_origin_dir == reverse_dir;
		} else {
			/* This gets called when a ship suddenly can't move forward, e.g. due to terraforming. */
			const DiagDirection entry = ReverseDiagDir(VehicleExitDir(v->direction, v->state));
			const TrackdirBits reverse_dirs = DiagdirReachesTrackdirs(entry) & TrackStatusToTrackdirBits(GetTileTrackStatus(v->tile, TRANSPORT_WATER, 0, entry));
			(void)ChooseShipTrack(v, tile, TRACKDIR_BIT_NONE, reverse_dirs, dest_tiles, 0, path_found, dummy_cache, best_origin_dir);
			*trackdir = path_found && best_origin_dir != INVALID_TRACKDIR ? best_origin_dir : GetRandomTrackdir(reverse_dirs);
			return true;
		}
	}

	/**
	 * Find the best depot for a ship.
	 * @param v Ship
	 * @param max_penalty maximum pathfinder cost.
	 * @param depot_tiles list of all possible depot destinations.
	 * @return FindDepotData with the best depot tile, cost and whether to reverse.
	 */
	static inline FindDepotData FindNearestDepot(const Ship *v, int max_penalty, const std::span<TileIndex> depot_tiles)
	{
		FindDepotData depot;
		if (depot_tiles.empty()) return depot;

		bool path_found = false;
		ShipPathCache dummy_cache;
		TileIndex tile = INVALID_TILE;
		Trackdir best_origin_dir = INVALID_TRACKDIR;

		const bool search_both_ways = max_penalty == 0;
		const Trackdir forward_dir = v->GetVehicleTrackdir();
		const Trackdir reverse_dir = ReverseTrackdir(forward_dir);
		const TrackdirBits forward_dirs = TrackdirToTrackdirBits(forward_dir);
		const TrackdirBits reverse_dirs = search_both_ways ? TrackdirToTrackdirBits(reverse_dir) : TRACKDIR_BIT_NONE;
		(void)ChooseShipTrack(v, tile, forward_dirs, reverse_dirs, depot_tiles, max_penalty, path_found, dummy_cache, best_origin_dir);

		if (path_found) {
			assert(tile != INVALID_TILE);
			depot.tile = tile;
		}

		return depot;
	}
};

/** Cost Provider module of YAPF for ships. */
template <class Types>
class CYapfCostShipT
{
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class).
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type.
	typedef typename Node::Key Key; ///< key to hash tables.

protected:
	int max_cost;

	CYapfCostShipT() : max_cost(0) {}

	/** to access inherited path finder */
	Tpf &Yapf()
	{
		return *static_cast<Tpf*>(this);
	}

public:
	inline void SetMaxCost(int maximum_cost)
	{
		this->max_cost = maximum_cost;
	}

	inline int CurveCost(Trackdir td1, Trackdir td2)
	{
		assert(IsValidTrackdir(td1));
		assert(IsValidTrackdir(td2));

		if (HasTrackdir(TrackdirCrossesTrackdirs(td1), td2)) {
			/* 90-deg curve penalty. */
			return Yapf().PfGetSettings().ship_curve90_penalty;
		} else if (td2 != NextTrackdir(td1)) {
			/* 45-deg curve penalty. */
			return Yapf().PfGetSettings().ship_curve45_penalty;
		}
		return 0;
	}

	static Vehicle *CountShipProc(Vehicle *v, void *data)
	{
		uint *count = (uint*)data;
		/* Ignore other vehicles (aircraft) and ships inside depot. */
		if (v->type == VEH_SHIP && (v->vehstatus & VS_HIDDEN) == 0) (*count)++;

		return nullptr;
	}

	/**
	 * Called by YAPF to calculate the cost from the origin to the given node.
	 * Calculates only the cost of given node, adds it to the parent node cost
	 * and stores the result into Node::m_cost member.
	 */
	inline bool PfCalcCost(Node &n, const TrackFollower *tf)
	{
		/* Base tile cost depending on distance. */
		int c = IsDiagonalTrackdir(n.GetTrackdir()) ? YAPF_TILE_LENGTH : YAPF_TILE_CORNER_LENGTH;
		/* Additional penalty for curves. */
		c += this->CurveCost(n.parent->GetTrackdir(), n.GetTrackdir());

		if (IsDockingTile(n.GetTile())) {
			/* Check docking tile for occupancy. */
			uint count = 0;
			HasVehicleOnPos(n.GetTile(), &count, &CountShipProc);
			c += count * 3 * YAPF_TILE_LENGTH;
		}

		/* Skipped tile cost for aqueducts. */
		c += YAPF_TILE_LENGTH * tf->tiles_skipped;

		/* Ocean/canal speed penalty. */
		const ShipVehicleInfo *svi = ShipVehInfo(Yapf().GetVehicle()->engine_type);
		uint8_t speed_frac = (GetEffectiveWaterClass(n.GetTile()) == WATER_CLASS_SEA) ? svi->ocean_speed_frac : svi->canal_speed_frac;
		if (speed_frac > 0) c += YAPF_TILE_LENGTH * (1 + tf->tiles_skipped) * speed_frac / (256 - speed_frac);

		/* Finish if we already exceeded the maximum path cost (i.e. when
		 * searching for the nearest depot). */
		if (this->max_cost > 0 && (n.parent->cost + c) > this->max_cost) return false;

		/* Apply it. */
		n.cost = n.parent->cost + c;
		return true;
	}
};

/**
 * Config struct of YAPF for ships.
 * Defines all 6 base YAPF modules as classes providing services for CYapfBaseT.
 */
template <class Tpf_, class Ttrack_follower, class Tnode_list>
struct CYapfShip_TypesT
{
	typedef CYapfShip_TypesT<Tpf_, Ttrack_follower, Tnode_list>  Types;         ///< Shortcut for this struct type.
	typedef Tpf_                                                 Tpf;           ///< Pathfinder type.
	typedef Ttrack_follower                                      TrackFollower; ///< Track follower helper class.
	typedef Tnode_list                                           NodeList;
	typedef Ship                                                 VehicleType;

	/** Pathfinder components (modules). */
	typedef CYapfBaseT<Types>                 PfBase;        ///< Base pathfinder class.
	typedef CYapfFollowShipT<Types>           PfFollow;      ///< Node follower.
	typedef CYapfOriginTileT<Types>           PfOrigin;      ///< Origin provider.
	typedef CYapfDestinationTileWaterT<Types> PfDestination; ///< Destination/distance provider.
	typedef CYapfSegmentCostCacheNoneT<Types> PfCache;       ///< Segment cost cache provider.
	typedef CYapfCostShipT<Types>             PfCost;        ///< Cost provider.
};

struct CYapfShip : CYapfT<CYapfShip_TypesT<CYapfShip, CFollowTrackWater, CShipNodeListExitDir > >
{
	explicit CYapfShip(int max_nodes) { this->max_search_nodes = max_nodes; }
};

/** Ship controller helper - path finder invoker. */
Track YapfShipChooseTrack(const Ship *v, TileIndex tile, bool &path_found, ShipPathCache &path_cache)
{
	std::vector<TileIndex> dest_tiles = GetShipDestinationTiles(v);
	Trackdir best_origin_dir = INVALID_TRACKDIR;
	const TrackdirBits origin_dirs = TrackdirToTrackdirBits(v->GetVehicleTrackdir());
	const Trackdir td_ret = CYapfShip::ChooseShipTrack(v, tile, origin_dirs, TRACKDIR_BIT_NONE, dest_tiles, 0, path_found, path_cache, best_origin_dir);
	return (td_ret != INVALID_TRACKDIR) ? TrackdirToTrack(td_ret) : INVALID_TRACK;
}

bool YapfShipCheckReverse(const Ship *v, Trackdir *trackdir)
{
	std::vector<TileIndex> dest_tiles = GetShipDestinationTiles(v);
	return CYapfShip::CheckShipReverse(v, trackdir, dest_tiles);
}

FindDepotData YapfShipFindNearestDepot(const Ship *v, int max_penalty)
{
	std::vector<TileIndex> depot_tiles = GetShipDepotTiles(v, max_penalty / YAPF_TILE_LENGTH);
	return CYapfShip::FindNearestDepot(v, max_penalty, depot_tiles);
}
