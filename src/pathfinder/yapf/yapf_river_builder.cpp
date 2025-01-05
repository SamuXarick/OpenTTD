/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_river_builder.cpp Pathfinder for river building. */

#include "../../stdafx.h"

#include "yapf.hpp"

#include "../../safeguards.h"

/* River builder pathfinder node. */
struct YapfRiverBuilderNode : CYapfNodeT<CYapfNodeKeyTrackDir, YapfRiverBuilderNode> {};

/* River builder pathfinder node list. */
using RiverBuilderNodeList = NodeList<YapfRiverBuilderNode, 8, 10>;

/* We don't need a follower but YAPF requires one. */
struct RiverBuilderFollower {};

/* We don't need a vehicle but YAPF requires one. */
struct DummyVehicle : Vehicle {};

class YapfRiverBuilder;

/* Types struct required for YAPF components. */
struct RiverBuilderTypes {
	using Tpf = YapfRiverBuilder;
	using TrackFollower = RiverBuilderFollower;
	using NodeList = RiverBuilderNodeList;
	using VehicleType = DummyVehicle;
};

/* River builder pathfinder implementation. */
class YapfRiverBuilder
	: public CYapfBaseT<RiverBuilderTypes>
	, public CYapfSegmentCostCacheNoneT<RiverBuilderTypes>
{
public:
	using Node = RiverBuilderTypes::NodeList::Item;
	using Key = Node::Key;

protected:
	Node *last_visited = nullptr; ///< Last node processed
	int height_begin; ///< Height of the river start tile

	inline YapfRiverBuilder &Yapf()
	{
		return *static_cast<YapfRiverBuilder *>(this);
	}

public:
	YapfRiverBuilder(TileIndex start_tile, int height_begin)
	{
		this->height_begin = height_begin;

		Node &node = Yapf().CreateNewNode();
		node.Set(nullptr, start_tile, INVALID_TRACKDIR, false);
		Yapf().AddStartupNode(node);
	}

	inline bool PfDetectDestination(Node &n)
	{
		this->last_visited = &n;

		TileIndex end = n.GetTile();
		int height_end;
		return IsTileFlat(end, &height_end) && (height_end < this->height_begin || (height_end == this->height_begin && IsWaterTile(end)));
	}

	inline bool PfCalcCost(Node &n, const RiverBuilderFollower *)
	{
		n.cost = n.parent->cost + 1 + RandomRange(_settings_game.game_creation.river_route_random);
		return true;
	}

	inline bool PfCalcEstimate(Node &n)
	{
		n.estimate = n.cost;
		assert(n.estimate >= n.parent->estimate);
		return true;
	}

	/**
	 * Check whether a river at begin could (logically) flow down to end.
	 * @param begin The origin of the flow.
	 * @param end The destination of the flow.
	 * @return True iff the water can be flowing down.
	 */
	inline bool RiverFlowsDown(TileIndex begin, TileIndex end) const
	{
		assert(DistanceManhattan(begin, end) == 1);

		auto [slope_end, height_end] = GetTileSlopeZ(end);

		/* Slope either is inclined or flat; rivers don't support other slopes. */
		if (slope_end != SLOPE_FLAT && !IsInclinedSlope(slope_end)) return false;

		auto [slope_begin, height_begin] = GetTileSlopeZ(begin);

		/* It can't flow uphill. */
		if (height_end > height_begin) return false;

		/* Slope continues, then it must be lower... */
		if (slope_end == slope_begin && height_end < height_begin) return true;

		/* ... or either end must be flat. */
		return slope_end == SLOPE_FLAT || slope_begin == SLOPE_FLAT;
	}

	inline void PfFollowNode(Node &old_node)
	{
		const TileIndex old_tile = old_node.GetTile();
		for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; ++d) {
			const TileIndex t = old_tile + TileOffsByDiagDir(d);
			if (IsValidTile(t) && this->RiverFlowsDown(old_tile, t)) {
				Node &node = Yapf().CreateNewNode();
				node.Set(&old_node, t, INVALID_TRACKDIR, true);
				Yapf().AddNewNode(node, RiverBuilderFollower{});
			}
		}
	}

	inline char TransportTypeChar() const
	{
		return '~';
	}

	static bool FlowRiver(TileIndex start_tile, int height_begin, std::vector<TileIndex> &river_tiles)
	{
		YapfRiverBuilder pf(start_tile, height_begin);
		bool found = pf.FindPath(nullptr);

		Node *path_root = nullptr;

		if (found) {
			/* Path found. Reconstruct the path from the best node. */
			path_root = pf.GetBestNode();
		} else if (pf.max_search_nodes != 0 && pf.nodes.ClosedCount() >= pf.max_search_nodes) {
			/* If we hit the max search nodes, reconstruct the path from the last visited node. */
			path_root = pf.last_visited;
		}

		for (Node *node = path_root; node != nullptr; node = node->parent) {
			river_tiles.push_back(node->GetTile());
		}
		std::ranges::reverse(river_tiles);

		return found;
	}
};

/**
 * Generates a river path, flowing from the start tile.
 * @param start_tile Start tile of the river.
 * @param height_begin Height of the river start tile.
 * @param river_tiles [out] Vector of tiles in the river path.
 *   When no path is found, this may be empty, or contain a path to the last visited tile if the max_search_nodes limit was reached.
 * @return True if a path is found, otherwise false.
 */
bool YapfFlowRiver(TileIndex start_tile, int height_begin, std::vector<TileIndex> &river_tiles)
{
	return YapfRiverBuilder::FlowRiver(start_tile, height_begin, river_tiles);
}
