/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tile_map.cpp Global tile accessors. */

#include "stdafx.h"
#include "tile_map.h"

#include "safeguards.h"

static inline constexpr Slope INVALID_SLOPE = static_cast<Slope>(0xFF); ///< Marker for invalid slope.

/**
 * Slope lookup table, indexed by encoded slope key.
 * 
 * dn, dw, de, ds are the height differences of the north, west,
 * east, and south corners with the minimum height of the four corners,
 * respectively. Valid height differences are: 0, 1 and 2.
 *
 * The table contains 81 entries (3^4), with invalid combinations marked
 * as INVALID_SLOPE.
 */
static constexpr Slope _slope_height_diff_table[3][3][3][3] = {
	/* dn = 0 */
	{
		/* dw = 0 */
		{
			/* de = 0 */
			{ SLOPE_FLAT, SLOPE_S, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 1 */
			{ SLOPE_E, SLOPE_SE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 2 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE } // ds = 0, 1, 2
		},
		/* dw = 1 */
		{
			/* de = 0 */
			{ SLOPE_W, SLOPE_SW, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 1 */
			{ SLOPE_EW, SLOPE_WSE, SLOPE_STEEP_S }, // ds = 0, 1, 2
			/* de = 2 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE } // ds = 0, 1, 2
		},
		/* dw = 2 */
		{
			/* de = 0 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 1 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 2 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE } // ds = 0, 1, 2
		}
	},
	/* dn = 1 */
	{
		/* dw = 0 */
		{
			/* de = 0 */
			{ SLOPE_N, SLOPE_NS, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 1 */
			{ SLOPE_NE, SLOPE_SEN, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 2 */
			{ INVALID_SLOPE, SLOPE_STEEP_E, INVALID_SLOPE } // ds = 0, 1, 2
		},
		/* dw = 1 */
		{
			/* de = 0 */
			{ SLOPE_NW, SLOPE_NWS, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 1 */
			{ SLOPE_ENW, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 2 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE } // ds = 0, 1, 2
		},
		/* dw = 2 */
		{
			/* de = 0 */
			{ INVALID_SLOPE, SLOPE_STEEP_W, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 1 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 2 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE } // ds = 0, 1, 2
		}
	},
	/* dn = 2 */
	{
		/* dw = 0 */
		{
			/* de = 0 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 1 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 2 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE } // ds = 0, 1, 2
		},
		/* dw = 1 */
		{
			/* de = 0 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 1 */
			{ SLOPE_STEEP_N, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 2 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE } // ds = 0, 1, 2
		},
		/* dw = 2 */
		{
			/* de = 0 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 1 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE }, // ds = 0, 1, 2
			/* de = 2 */
			{ INVALID_SLOPE, INVALID_SLOPE, INVALID_SLOPE } // ds = 0, 1, 2
		}
	}
};

/**
 * Get a tile's slope given the height of its four corners.
 * @param hnorth The height at the northern corner in the same unit as TileHeight.
 * @param hwest  The height at the western corner in the same unit as TileHeight.
 * @param heast  The height at the eastern corner in the same unit as TileHeight.
 * @param hsouth The height at the southern corner in the same unit as TileHeight.
 * @return The slope and the lowest height of the four corners.
 */
static std::tuple<Slope, int> GetTileSlopeGivenHeight(int hnorth, int hwest, int heast, int hsouth)
{
	/* Due to the fact that tiles must connect with each other without leaving gaps, the
	 * biggest difference in height between any corner and 'min' is between 0, 1, or 2.
	 *
	 * Also, there is at most 1 corner with height difference of 2.
	 */
	int hminnw = std::min(hnorth, hwest);
	int hmines = std::min(heast, hsouth);
	int hmin = std::min(hminnw, hmines);

	/* Calculate height differences with the minimum height. */
	int dn = hnorth - hmin;
	int dw = hwest - hmin;
	int de = heast - hmin;
	int ds = hsouth - hmin;

	/* Lookup the slope from the table. */
	Slope r = _slope_height_diff_table[dn][dw][de][ds];
	assert(r != INVALID_SLOPE); // Should never happen.

	return {r, hmin};
}

/**
 * Return the slope of a given tile inside the map.
 * @param tile Tile to compute slope of
 * @return Slope of the tile, except for the HALFTILE part, and the z height
 */
std::tuple<Slope, int> GetTileSlopeZ(TileIndex tile)
{
	uint x1 = TileX(tile);
	uint y1 = TileY(tile);
	uint x2 = std::min(x1 + 1, Map::MaxX());
	uint y2 = std::min(y1 + 1, Map::MaxY());

	int hnorth = TileHeight(tile);           // Height of the North corner.
	int hwest  = TileHeight(TileXY(x2, y1)); // Height of the West corner.
	int heast  = TileHeight(TileXY(x1, y2)); // Height of the East corner.
	int hsouth = TileHeight(TileXY(x2, y2)); // Height of the South corner.

	return GetTileSlopeGivenHeight(hnorth, hwest, heast, hsouth);
}

/**
 * Return the slope of a given tile, also for tiles outside the map (virtual "black" tiles).
 *
 * @param x X coordinate of the tile to compute slope of, may be outside the map.
 * @param y Y coordinate of the tile to compute slope of, may be outside the map.
 * @param h If not \c nullptr, pointer to storage of z height.
 * @return Slope of the tile, except for the HALFTILE part, and the z height of the tile.
 */
std::tuple<Slope, int> GetTilePixelSlopeOutsideMap(int x, int y)
{
	int hnorth = TileHeightOutsideMap(x,     y);     // N corner.
	int hwest  = TileHeightOutsideMap(x + 1, y);     // W corner.
	int heast  = TileHeightOutsideMap(x,     y + 1); // E corner.
	int hsouth = TileHeightOutsideMap(x + 1, y + 1); // S corner.

	auto [slope, h] = GetTileSlopeGivenHeight(hnorth, hwest, heast, hsouth);
	return {slope, h * TILE_HEIGHT};
}

/**
 * Check if a given tile is flat
 * @param tile Tile to check
 * @param h If not \c nullptr, pointer to storage of z height (only if tile is flat)
 * @return Whether the tile is flat
 */
bool IsTileFlat(TileIndex tile, int *h)
{
	uint x1 = TileX(tile);
	uint y1 = TileY(tile);
	uint x2 = std::min(x1 + 1, Map::MaxX());
	uint y2 = std::min(y1 + 1, Map::MaxY());

	uint z = TileHeight(tile);
	if (TileHeight(TileXY(x2, y1)) != z) return false;
	if (TileHeight(TileXY(x1, y2)) != z) return false;
	if (TileHeight(TileXY(x2, y2)) != z) return false;

	if (h != nullptr) *h = z;
	return true;
}

/**
 * Get bottom height of the tile
 * @param tile Tile to compute height of
 * @return Minimum height of the tile
 */
int GetTileZ(TileIndex tile)
{
	uint x1 = TileX(tile);
	uint y1 = TileY(tile);
	uint x2 = std::min(x1 + 1, Map::MaxX());
	uint y2 = std::min(y1 + 1, Map::MaxY());

	return std::min({
		TileHeight(tile),           // N corner
		TileHeight(TileXY(x2, y1)), // W corner
		TileHeight(TileXY(x1, y2)), // E corner
		TileHeight(TileXY(x2, y2)), // S corner
	});
}

/**
 * Get top height of the tile inside the map.
 * @param t Tile to compute height of
 * @return Maximum height of the tile
 */
int GetTileMaxZ(TileIndex t)
{
	uint x1 = TileX(t);
	uint y1 = TileY(t);
	uint x2 = std::min(x1 + 1, Map::MaxX());
	uint y2 = std::min(y1 + 1, Map::MaxY());

	return std::max({
		TileHeight(t),              // N corner
		TileHeight(TileXY(x2, y1)), // W corner
		TileHeight(TileXY(x1, y2)), // E corner
		TileHeight(TileXY(x2, y2)), // S corner
	});
}
