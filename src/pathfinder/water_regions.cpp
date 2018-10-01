/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file water_regions.cpp Handles dividing the water in the map into square regions to assist pathfinding. */

#include "stdafx.h"
#include "map_func.h"
#include "water_regions.h"
#include "map_func.h"
#include "tilearea_type.h"
#include "track_func.h"
#include "transport_type.h"
#include "landscape.h"
#include "tunnelbridge_map.h"
#include "follow_track.hpp"
#include "ship.h"
#include "debug.h"
#include "yapf/yapf.h"

using TWaterRegionTraversabilityBits = uint16_t;

static_assert(sizeof(TWaterRegionTraversabilityBits) * 8 == WATER_REGION_EDGE_LENGTH);
static_assert(sizeof(TWaterRegionPatchLabel) == sizeof(byte)); // Important for the hash calculation.

static inline TrackBits GetWaterTracks(TileIndex tile) { return TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_WATER, 0)); }
static inline bool IsAqueductTile(TileIndex tile) { return IsBridgeTile(tile) && GetTunnelBridgeTransportType(tile) == TRANSPORT_WATER; }

static inline int GetWaterRegionX(TileIndex tile) { return TileX(tile) / WATER_REGION_EDGE_LENGTH; }
static inline int GetWaterRegionY(TileIndex tile) { return TileY(tile) / WATER_REGION_EDGE_LENGTH; }

static inline int GetWaterRegionMapSizeX() { return Map::SizeX() / WATER_REGION_EDGE_LENGTH; }
static inline int GetWaterRegionMapSizeY() { return Map::SizeY() / WATER_REGION_EDGE_LENGTH; }

static inline TWaterRegionIndex GetWaterRegionIndex(int region_x, int region_y) { return GetWaterRegionMapSizeX() * region_y + region_x; }
static inline TWaterRegionIndex GetWaterRegionIndex(TileIndex tile) { return GetWaterRegionIndex(GetWaterRegionX(tile), GetWaterRegionY(tile)); }

/**
 * Represents a square section of the map of a fixed size. Within this square individual unconnected patches of water are
 * identified using a Connected Component Labeling (CCL) algorithm. Note that all information stored in this class applies
 * only to tiles within the square section, there is no knowledge about the rest of the map. This makes it easy to invalidate
 * and update a water region if any changes are made to it, such as construction or terraforming.
 */
class WaterRegion
{
private:
	std::array<TWaterRegionTraversabilityBits, DIAGDIR_END> edge_traversability_bits{};
	bool has_cross_region_aqueducts = false;
	TWaterRegionPatchLabel number_of_patches = 0; // 0 = no water, 1 = one single patch of water, etc...
	const OrthogonalTileArea tile_area;
	std::array<TWaterRegionPatchLabel, WATER_REGION_NUMBER_OF_TILES> tile_patch_labels{};
	bool initialized = false;

	/**
	 * Returns the local index of the tile within the region. The N corner represents 0,
	 * the x direction is positive in the SW direction, and Y is positive in the SE direction.
	 * @param tile Tile within the water region.
	 * @returns The local index.
	 */
	inline int GetLocalIndex(TileIndex tile) const
	{
		assert(this->tile_area.Contains(tile));
		return (TileX(tile) - TileX(this->tile_area.tile)) + WATER_REGION_EDGE_LENGTH * (TileY(tile) - TileY(this->tile_area.tile));
	}

public:
	WaterRegion(int region_x, int region_y)
		: tile_area(TileXY(region_x * WATER_REGION_EDGE_LENGTH, region_y * WATER_REGION_EDGE_LENGTH), WATER_REGION_EDGE_LENGTH, WATER_REGION_EDGE_LENGTH)
	{}

	OrthogonalTileIterator begin() const { return this->tile_area.begin(); }
	OrthogonalTileIterator end() const { return this->tile_area.end(); }

	bool IsInitialized() const { return this->initialized; }

	void Invalidate()
	{
		if (!IsInitialized()) Debug(map, 3, "Invalidated water region ({},{})", GetWaterRegionX(this->tile_area.tile), GetWaterRegionY(this->tile_area.tile));
		this->initialized = false;
	}

	/**
	 * Returns a set of bits indicating whether an edge tile on a particular side is traversable or not. These
	 * values can be used to determine whether a ship can enter/leave the region through a particular edge tile.
	 * @see GetLocalIndex() for a description of the coordinate system used.
	 * @param side Which side of the region we want to know the edge traversability of.
	 * @returns A value holding the edge traversability bits.
	 */
	TWaterRegionTraversabilityBits GetEdgeTraversabilityBits(DiagDirection side) const { return edge_traversability_bits[side]; }

	/**
	 * @returns The amount of individual water patches present within the water region. A value of
	 * 0 means there is no water present in the water region at all.
	 */
	int NumberOfPatches() const { return this->number_of_patches; }

	/**
	 * @returns Whether the water region contains aqueducts that cross the region boundaries.
	 */
	bool HasCrossRegionAqueducts() const { return this->has_cross_region_aqueducts; }

	/**
	 * Returns the patch label that was assigned to the tile.
	 * @param tile The tile of which we want to retrieve the label.
	 * @returns The label assigned to the tile.
	 */
	TWaterRegionPatchLabel GetLabel(TileIndex tile) const
	{
		assert(this->tile_area.Contains(tile));
		return this->tile_patch_labels[GetLocalIndex(tile)];
	}

	/**
	 * Performs the connected component labeling and other data gathering.
	 * @see WaterRegion
	 */
	void ForceUpdate()
	{
		Debug(map, 3, "Updating water region ({},{})", GetWaterRegionX(this->tile_area.tile), GetWaterRegionY(this->tile_area.tile));
		this->has_cross_region_aqueducts = false;

		this->tile_patch_labels.fill(WATER_REGION_PATCH_LABEL_NONE);
		this->edge_traversability_bits.fill(0);

		TWaterRegionPatchLabel current_label = WATER_REGION_PATCH_LABEL_FIRST;
		TWaterRegionPatchLabel highest_assigned_label = WATER_REGION_PATCH_LABEL_NONE;

		/* Perform connected component labeling. This uses a flooding algorithm that expands until no
		 * additional tiles can be added. Only tiles inside the water region are considered. */
		for (const TileIndex start_tile : tile_area) {
			static std::vector<TileIndex> tiles_to_check;
			tiles_to_check.clear();
			tiles_to_check.push_back(start_tile);

			bool increase_label = false;
			while (!tiles_to_check.empty()) {
				const TileIndex tile = tiles_to_check.back();
				tiles_to_check.pop_back();

				const TrackdirBits valid_dirs = TrackBitsToTrackdirBits(GetWaterTracks(tile));
				if (valid_dirs == TRACKDIR_BIT_NONE) continue;

				if (this->tile_patch_labels[GetLocalIndex(tile)] != WATER_REGION_PATCH_LABEL_NONE) continue;

				this->tile_patch_labels[GetLocalIndex(tile)] = current_label;
				highest_assigned_label = current_label;
				increase_label = true;

				for (const Trackdir dir : SetTrackdirBitIterator(valid_dirs)) {
					/* By using a TrackFollower we "play by the same rules" as the actual ship pathfinder */
					CFollowTrackWater ft;
					if (ft.Follow(tile, dir)) {
						if (this->tile_area.Contains(ft.m_new_tile)) {
							tiles_to_check.push_back(ft.m_new_tile);
						} else if (!ft.m_is_bridge) {
							assert(DistanceManhattan(ft.m_new_tile, tile) == 1);
							const auto side = DiagdirBetweenTiles(tile, ft.m_new_tile);
							const int local_x_or_y = DiagDirToAxis(side) == AXIS_X ? TileY(tile) - TileY(this->tile_area.tile) : TileX(tile) - TileX(this->tile_area.tile);
							SetBit(this->edge_traversability_bits[side], local_x_or_y);
						} else {
							this->has_cross_region_aqueducts = true;
						}
					}
				}
			}

			if (increase_label) current_label++;
			assert(current_label != INVALID_WATER_REGION_PATCH_LABEL);
		}

		this->number_of_patches = highest_assigned_label;
		this->initialized = true;
	}

	/**
	 * Updates the patch labels and other data, but only if the region is not yet initialized.
	 */
	inline void UpdateIfNotInitialized()
	{
		if (!this->initialized) ForceUpdate();
	}

	void PrintDebugInfo()
	{
		Debug(map, 9, "Water region {},{} labels and edge traversability = ...", GetWaterRegionX(tile_area.tile), GetWaterRegionY(tile_area.tile));

		const size_t max_element_width = std::to_string(this->number_of_patches).size();

		std::array<int, 16> traversability_NW{0};
		for (auto bitIndex : SetBitIterator(edge_traversability_bits[DIAGDIR_NW])) *(traversability_NW.rbegin() + bitIndex) = 1;
		Debug(map, 9, "    {:{}}", fmt::join(traversability_NW, " "), max_element_width);
		Debug(map, 9, "  +{:->{}}+", "", WATER_REGION_EDGE_LENGTH * (max_element_width + 1) + 1);

		for (int y = 0; y < WATER_REGION_EDGE_LENGTH; ++y) {
			std::string line{};
			for (int x = 0; x < WATER_REGION_EDGE_LENGTH; ++x) {
				const auto label = this->tile_patch_labels[x + y * WATER_REGION_EDGE_LENGTH];
				const std::string label_str = label == WATER_REGION_PATCH_LABEL_NONE ? "." : std::to_string(label);
				line = fmt::format("{:{}}", label_str, max_element_width) + " " + line;
			}
			Debug(map, 9, "{} | {}| {}", GB(this->edge_traversability_bits[DIAGDIR_SW], y, 1), line, GB(this->edge_traversability_bits[DIAGDIR_NE], y, 1));
		}

		Debug(map, 9, "  +{:->{}}+", "", WATER_REGION_EDGE_LENGTH * (max_element_width + 1) + 1);
		std::array<int, 16> traversability_SE{0};
		for (auto bitIndex : SetBitIterator(edge_traversability_bits[DIAGDIR_SE])) *(traversability_SE.rbegin() + bitIndex) = 1;
		Debug(map, 9, "    {:{}}", fmt::join(traversability_SE, " "), max_element_width);
	}
};

std::vector<WaterRegion> _water_regions;

TileIndex GetTileIndexFromLocalCoordinate(int region_x, int region_y, int local_x, int local_y)
{
	assert(local_x >= 0 && local_x < WATER_REGION_EDGE_LENGTH);
	assert(local_y >= 0 && local_y < WATER_REGION_EDGE_LENGTH);
	return TileXY(WATER_REGION_EDGE_LENGTH * region_x + local_x, WATER_REGION_EDGE_LENGTH * region_y + local_y);
}

TileIndex GetEdgeTileCoordinate(int region_x, int region_y, DiagDirection side, int x_or_y)
{
	assert(x_or_y >= 0 && x_or_y < WATER_REGION_EDGE_LENGTH);
	switch (side) {
		case DIAGDIR_NE: return GetTileIndexFromLocalCoordinate(region_x, region_y, 0, x_or_y);
		case DIAGDIR_SW: return GetTileIndexFromLocalCoordinate(region_x, region_y, WATER_REGION_EDGE_LENGTH - 1, x_or_y);
		case DIAGDIR_NW: return GetTileIndexFromLocalCoordinate(region_x, region_y, x_or_y, 0);
		case DIAGDIR_SE: return GetTileIndexFromLocalCoordinate(region_x, region_y, x_or_y, WATER_REGION_EDGE_LENGTH - 1);
		default: NOT_REACHED();
	}
}

WaterRegion &GetUpdatedWaterRegion(uint16_t region_x, uint16_t region_y)
{
	WaterRegion &result = _water_regions[GetWaterRegionIndex(region_x, region_y)];
	result.UpdateIfNotInitialized();
	return result;
}

WaterRegion &GetUpdatedWaterRegion(TileIndex tile)
{
	WaterRegion &result = _water_regions[GetWaterRegionIndex(tile)];
	result.UpdateIfNotInitialized();
	return result;
}

/**
 * Returns the index of the water region.
 * @param water_region The water region to return the index for.
 */
TWaterRegionIndex GetWaterRegionIndex(const WaterRegionDesc &water_region)
{
	return GetWaterRegionIndex(water_region.x, water_region.y);
}

/**
 * Calculates a number that uniquely identifies the provided water region patch.
 * @param water_region_patch The Water region to calculate the hash for.
 */
int CalculateWaterRegionPatchHash(const WaterRegionPatchDesc &water_region_patch)
{
	return water_region_patch.label | GetWaterRegionIndex(water_region_patch) << 8;
}

/**
 * Returns the center tile of a particular water region.
 * @param water_region The water region to find the center tile for.
 * @returns The center tile of the water region.
 */
TileIndex GetWaterRegionCenterTile(const WaterRegionDesc &water_region)
{
	return TileXY(water_region.x * WATER_REGION_EDGE_LENGTH + (WATER_REGION_EDGE_LENGTH / 2), water_region.y * WATER_REGION_EDGE_LENGTH + (WATER_REGION_EDGE_LENGTH / 2));
}

/**
 * Returns basic water region information for the provided tile.
 * @param tile The tile for which the information will be calculated.
 */
WaterRegionDesc GetWaterRegionInfo(TileIndex tile)
{
	return WaterRegionDesc{ GetWaterRegionX(tile), GetWaterRegionY(tile) };
}

/**
 * Returns basic water region patch information for the provided tile.
 * @param tile The tile for which the information will be calculated.
 */
WaterRegionPatchDesc GetWaterRegionPatchInfo(TileIndex tile)
{
	WaterRegion &region = GetUpdatedWaterRegion(tile);
	return WaterRegionPatchDesc{ GetWaterRegionX(tile), GetWaterRegionY(tile), region.GetLabel(tile)};
}

/**
 * Marks the water region that tile is part of as invalid.
 * @param tile Tile within the water region that we wish to invalidate.
 */
void InvalidateWaterRegion(TileIndex tile)
{
	if (!IsValidTile(tile)) return;
	const int water_region_index = GetWaterRegionIndex(tile);
	_water_regions[water_region_index].Invalidate();

	/* When updating the water region we look into the first tile of adjacent water regions to determine edge
	 * traversability. This means that if we invalidate any region edge tiles we might also change the traversability
	 * of the adjacent region. This code ensures the adjacent regions also get invalidated in such a case. */
	for (DiagDirection side = DIAGDIR_BEGIN; side < DIAGDIR_END; side++) {
		const int adjacent_region_index = GetWaterRegionIndex(TileAddByDiagDir(tile, side));
		if (adjacent_region_index != water_region_index) _water_regions[adjacent_region_index].Invalidate();
	}
}

/**
 * Calls the provided callback function for all water region patches
 * accessible from one particular side of the starting patch.
 * @param water_region_patch Water patch within the water region to start searching from
 * @param side Side of the water region to look for neigboring patches of water
 * @param callback The function that will be called for each neighbor that is found
 */
static inline void VisitAdjacentWaterRegionPatchNeighbors(const WaterRegionPatchDesc &water_region_patch, DiagDirection side, TVisitWaterRegionPatchCallBack &func)
{
	if (water_region_patch.label == INVALID_WATER_REGION_PATCH) return;

	const WaterRegion &current_region = GetUpdatedWaterRegion(water_region_patch.x, water_region_patch.y);

	const TileIndexDiffC offset = TileIndexDiffCByDiagDir(side);
	const int nx = water_region_patch.x + offset.x;
	const int ny = water_region_patch.y + offset.y;

	if (nx < 0 || ny < 0 || nx >= GetWaterRegionMapSizeX() || ny >= GetWaterRegionMapSizeY()) return;

	const WaterRegion &neighboring_region = GetUpdatedWaterRegion(nx, ny);
	const DiagDirection opposite_side = ReverseDiagDir(side);

	/* Indicates via which local x or y coordinates (depends on the "side" parameter) we can cross over into the adjacent region. */
	const TWaterRegionTraversabilityBits traversability_bits = current_region.GetEdgeTraversabilityBits(side)
		& neighboring_region.GetEdgeTraversabilityBits(opposite_side);
	if (traversability_bits == 0) return;

	if (current_region.NumberOfPatches() == 1 && neighboring_region.NumberOfPatches() == 1) {
		func(WaterRegionPatchDesc{ nx, ny, WATER_REGION_PATCH_LABEL_FIRST }); // No further checks needed because we know there is just one patch for both adjacent regions
		return;
	}

	/* Multiple water patches can be reached from the current patch. Check each edge tile individually. */
	static std::vector<TWaterRegionPatchLabel> unique_labels; // static and vector-instead-of-map for performance reasons
	unique_labels.clear();
	for (int x_or_y = 0; x_or_y < WATER_REGION_EDGE_LENGTH; ++x_or_y) {
		if (!HasBit(traversability_bits, x_or_y)) continue;

		const TileIndex current_edge_tile = GetEdgeTileCoordinate(water_region_patch.x, water_region_patch.y, side, x_or_y);
		const TWaterRegionPatchLabel current_label = current_region.GetLabel(current_edge_tile);
		if (current_label != water_region_patch.label) continue;

		const TileIndex neighbor_edge_tile = GetEdgeTileCoordinate(nx, ny, opposite_side, x_or_y);
		const TWaterRegionPatchLabel neighbor_label = neighboring_region.GetLabel(neighbor_edge_tile);
		assert(neighbor_label != INVALID_WATER_REGION_PATCH);
		if (std::find(unique_labels.begin(), unique_labels.end(), neighbor_label) == unique_labels.end()) unique_labels.push_back(neighbor_label);
	}
	for (TWaterRegionPatchLabel unique_label : unique_labels) func(WaterRegionPatchDesc{ nx, ny, unique_label });
}

/**
 * Calls the provided callback function on all accessible water region patches in
 * each cardinal direction, plus any others that are reachable via aqueducts.
 * @param water_region_patch Water patch within the water region to start searching from
 * @param callback The function that will be called for each accessible water patch that is found
 */
void VisitWaterRegionPatchNeighbors(const WaterRegionPatchDesc &water_region_patch, TVisitWaterRegionPatchCallBack &callback)
{
	if (water_region_patch.label == INVALID_WATER_REGION_PATCH) return;

	const WaterRegion &current_region = GetUpdatedWaterRegion(water_region_patch.x, water_region_patch.y);

	/* Visit adjacent water region patches in each cardinal direction */
	for (DiagDirection side = DIAGDIR_BEGIN; side < DIAGDIR_END; side++) VisitAdjacentWaterRegionPatchNeighbors(water_region_patch, side, callback);

	/* Visit neigboring water patches accessible via cross-region aqueducts */
	if (current_region.HasCrossRegionAqueducts()) {
		for (const TileIndex tile : current_region) {
			if (GetWaterRegionPatchInfo(tile) == water_region_patch && IsAqueductTile(tile)) {
				const TileIndex other_end_tile = GetOtherBridgeEnd(tile);
				if (GetWaterRegionIndex(tile) != GetWaterRegionIndex(other_end_tile)) callback(GetWaterRegionPatchInfo(other_end_tile));
			}
		}
	}
}

/**
 * Allocates the appropriate amount of water regions for the current map size
 */
void AllocateWaterRegions()
{
	_water_regions.clear();
	_water_regions.reserve(static_cast<size_t>(GetWaterRegionMapSizeX()) * GetWaterRegionMapSizeY());

	Debug(map, 2, "Allocating {} x {} water regions", GetWaterRegionMapSizeX(), GetWaterRegionMapSizeY());

	for (int region_y = 0; region_y < GetWaterRegionMapSizeY(); region_y++) {
		for (int region_x = 0; region_x < GetWaterRegionMapSizeX(); region_x++) {
			_water_regions.emplace_back(region_x, region_y);
		}
	}
}

void PrintWaterRegionDebugInfo(TileIndex tile)
{
	GetUpdatedWaterRegion(tile).PrintDebugInfo();
}

DiagDirection DiagDirBetweenRegions(const WaterRegionPatchDesc &water_region_patch_from, const WaterRegionPatchDesc &water_region_patch_to, bool *is_adjacent_neighbor)
{
	if (water_region_patch_from == INVALID_WATER_REGION_PATCH_DESC) return INVALID_DIAGDIR;
	const int dx = water_region_patch_from.x - water_region_patch_to.x;
	const int dy = water_region_patch_from.y - water_region_patch_to.y;
	*is_adjacent_neighbor = (std::abs(dx) == 1 && dy == 0) || (std::abs(dy) == 1 && dx == 0);
	if (dx > 0 && dy == 0) return DIAGDIR_SW;
	if (dx < 0 && dy == 0) return DIAGDIR_NE;
	if (dx == 0 && dy > 0) return DIAGDIR_SE;
	if (dx == 0 && dy < 0) return DIAGDIR_NW;
	return INVALID_DIAGDIR;
}

/**
 * Find the tile of a cross-region aqueduct based on the given parameters.
 * @param region_x The x-coordinate of the water region.
 * @param region_y The y-coordinate of the water region.
 * @param side The side the aqueduct crosses (DIAGDIR_NE, DIAGDIR_NW, DIAGDIR_SE, DIAGDIR_SW).
 * @param x_or_y The x or y coordinate within the water region.
 * @param dist[out] The distance manhattan from the aqueduct to the edge.
 * @pre The x_or_y parameter must be less than WATER_REGION_EDGE_LENGTH.
 * @return The tile index of the cross-region aqueduct or INVALID_TILE if not found.
 */
TileIndex FindCrossRegionAqueductTileCoordinate(int region_x, int region_y, DiagDirection side, int x_or_y, int *dist)
{
	assert(x_or_y >= 0 && x_or_y < WATER_REGION_EDGE_LENGTH);

	int start = (side == DIAGDIR_NE || side == DIAGDIR_NW) ? 0 : WATER_REGION_EDGE_LENGTH - 1;
	int end = (side == DIAGDIR_NE || side == DIAGDIR_NW) ? WATER_REGION_EDGE_LENGTH : -1;
	int step = (side == DIAGDIR_NE || side == DIAGDIR_NW) ? 1 : -1;

	int distance = 0;
	for (int i = start; i != end; i += step) {
		TileIndex tile = (side == DIAGDIR_NE || side == DIAGDIR_SW) ? GetTileIndexFromLocalCoordinate(region_x, region_y, i, x_or_y) : GetTileIndexFromLocalCoordinate(region_x, region_y, x_or_y, i);
		if (IsAqueductTile(tile) && GetWaterRegionIndex(region_x, region_y) != GetWaterRegionIndex(GetOtherBridgeEnd(tile)) && GetTunnelBridgeDirection(tile) == side) {
			*dist = distance;
			return tile;
		}
		distance++;
	}

	return INVALID_TILE;
}

/**
 * Tests the provided callback function on all tiles of the current region, and determines which tile
 * reached from the neighboring water region patch has the closest ship depot by pathfinding standards.
 * @param callback The test function that will be called for each neighboring water patch that is found.
 * @param parent_water_region_patch Water patch from which the current region patch is entered from.
 * @param current_water_region_patch Water patch within the water region to test the callback.
 * @param v Ship, used by the pathfinder to extract the pathfinder distances and tiles.
 * @return closest tile which passed the callback test, or INVALID_TILE if the callback failed.
 */
TileIndex GetShipDepotInWaterRegionPatch(IsShipDepotRegionCallBack &callback, const WaterRegionPatchDesc &parent_water_region_patch, const WaterRegionPatchDesc &current_water_region_patch, const Ship *v)
{
	const WaterRegion &region = GetUpdatedWaterRegion(current_water_region_patch.x, current_water_region_patch.y);
	bool is_adjacent_neighbor = false;
	const DiagDirection enter_side = DiagDirBetweenRegions(parent_water_region_patch, current_water_region_patch, &is_adjacent_neighbor);

	/* Check if the current region has a tile which passes the callback test. */
	bool has_valid_tile = false;
	for (const TileIndex tile : region) {
		if (region.GetLabel(tile) != current_water_region_patch.label || !callback(tile)) continue;

		/* If we don't know from which side we entered this region, it's likely this is the origin node.
		 * In this situation, return whichever tile passed the callback. */
		if (!IsValidDiagDirection(enter_side)) return tile;
		has_valid_tile = true;
		break;
	}

	TileIndex best_tile = INVALID_TILE;
	if (!has_valid_tile) return best_tile;

	/* Collect all entry points, look for the one closest to where a depot is found. */
	std::vector<std::pair<const TileIndex, int>> tile_dists;

	const WaterRegion &neighboring_region = GetUpdatedWaterRegion(parent_water_region_patch.x, parent_water_region_patch.y);
	const DiagDirection exit_side = ReverseDiagDir(enter_side);

	/* Visit adjacent water region patch. */
	const TWaterRegionTraversabilityBits edge_traversability_bits = region.GetEdgeTraversabilityBits(enter_side);
	if (is_adjacent_neighbor && edge_traversability_bits != 0) {
		for (int x_or_y = 0; x_or_y < WATER_REGION_EDGE_LENGTH; ++x_or_y) {
			if (!HasBit(edge_traversability_bits, x_or_y)) continue;

			const TileIndex current_edge_tile = GetEdgeTileCoordinate(current_water_region_patch.x, current_water_region_patch.y, enter_side, x_or_y);
			const TWaterRegionPatchLabel current_label = region.GetLabel(current_edge_tile);
			if (current_label != current_water_region_patch.label) continue;

			const TileIndex neighbor_edge_tile = GetEdgeTileCoordinate(parent_water_region_patch.x, parent_water_region_patch.y, exit_side, x_or_y);
			const TWaterRegionPatchLabel neighbor_label = neighboring_region.GetLabel(neighbor_edge_tile);
			if (neighbor_label != parent_water_region_patch.label) continue;

			auto tile_dist = std::pair<const TileIndex, int>(current_edge_tile, 0);
			if (std::find(tile_dists.begin(), tile_dists.end(), tile_dist) == tile_dists.end()) {
				tile_dists.push_back(tile_dist);
			}
		}
	}

	/* Visit neigboring water patch accessible via cross-region aqueducts. */
	if (region.HasCrossRegionAqueducts()) {
		for (int x_or_y = 0; x_or_y < WATER_REGION_EDGE_LENGTH; ++x_or_y) {

			int dist;
			const TileIndex current_aqueduct_tile = FindCrossRegionAqueductTileCoordinate(current_water_region_patch.x, current_water_region_patch.y, enter_side, x_or_y, &dist);
			if (!IsValidTile(current_aqueduct_tile)) continue;

			const TWaterRegionPatchLabel current_label = region.GetLabel(current_aqueduct_tile);
			if (current_label != current_water_region_patch.label) continue;

			const TileIndex neighbor_aqueduct_tile = GetOtherBridgeEnd(current_aqueduct_tile);
			const WaterRegionPatchDesc &neighbor_patch = GetWaterRegionPatchInfo(neighbor_aqueduct_tile);
			if (neighbor_patch != parent_water_region_patch) continue;

			const TWaterRegionPatchLabel neighbor_label = neighboring_region.GetLabel(neighbor_aqueduct_tile);
			if (neighbor_label != parent_water_region_patch.label) continue;

			auto tile_dist = std::pair<const TileIndex, int>(current_aqueduct_tile, dist);
			if (std::find(tile_dists.begin(), tile_dists.end(), tile_dist) == tile_dists.end()) {
				tile_dists.push_back(tile_dist);
			}
		}
	}

	assert(!tile_dists.empty());

	/* Compare actual pathfinder distance costs for each of them, then return one with the smallest cost. */
	int best_cost = INT_MAX;
	for (const auto &tile_dist : tile_dists) {

		/* Convert tracks to trackdirs */
		TrackdirBits trackdirs = TrackBitsToTrackdirBits(GetWaterTracks(tile_dist.first));
		/* Limit to trackdirs reachable from the parent region. */
		trackdirs &= DiagdirReachesTrackdirs(exit_side);

		const FindDepotData depot = YapfShipFindNearestDepot(v, 0, tile_dist.first, trackdirs);
		assert(depot.best_length != UINT_MAX);
		int cost = depot.best_length;
		if (tile_dist.second != 0) {
			/* Skipped tile cost for aqueducts. */
			cost += YAPF_TILE_LENGTH * tile_dist.second;

			/* Aqueduct speed penalty. */
			const ShipVehicleInfo *svi = ShipVehInfo(v->engine_type);
			byte speed_frac = svi->canal_speed_frac;
			if (speed_frac > 0) cost += YAPF_TILE_LENGTH * (1 + tile_dist.second) * speed_frac / (256 - speed_frac);
		}
		if (cost >= best_cost) continue;

		best_cost = cost;
		best_tile = depot.tile;
	}

	return best_tile;
}
