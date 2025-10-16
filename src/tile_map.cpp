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

/**
 * Get bottom height of a tuple of heights of a tile inside the map.
 * @param heights Tuple of heights to compute height of.
 * @return Minimum height of the tuple of heights.
 */
static int GetTileMinHeight(auto heights)
{
	auto [hnorth, hwest, heast, hsouth] = heights;

	return std::min({ hnorth, hwest, heast, hsouth });
}

/**
 * Get top height of a tuple of heights of a tile inside the map.
 * @param heights Tuple of heights to compute height of.
 * @return Maximum height of the tuple of heights
 */
static int GetTileMaxHeight(auto heights)
{
	auto [hnorth, hwest, heast, hsouth] = heights;

	return std::max({ hnorth, hwest, heast, hsouth });
}

/**
 * Get a tile's slope given the height of its four corners.
 * @param heights The tuple containing the heights at the northern, western, eastern and southern corners in the same unit as TileHeight.
 * @return The slope and the lowest height of the four corners.
 */
static std::tuple<Slope, int> GetTileSlopeGivenHeight(auto heights)
{
	/* Due to the fact that tiles must connect with each other without leaving gaps, the
	 * biggest difference in height between any corner and 'min' is between 0, 1, or 2.
	 *
	 * Also, there is at most 1 corner with height difference of 2.
	 */
	int hmin = GetTileMinHeight(heights);

	Slope r = SLOPE_FLAT;

	auto [hnorth, hwest, heast, hsouth] = heights;

	if (hnorth != hmin) r |= SLOPE_N;
	if (hwest  != hmin) r |= SLOPE_W;
	if (heast  != hmin) r |= SLOPE_E;
	if (hsouth != hmin) r |= SLOPE_S;

	if (GetTileMaxHeight(heights) - hmin == 2) r |= SLOPE_STEEP;

	return {r, hmin};
}

/**
 * Get the bounding corner coordinates of a tile on the map grid.
 *
 * Extracts its top corner (x1, y1) coordinates and calculates the
 * bottom corner (x2, y2) coordinates by incrementing each axis,
 * ensuring they do not exceed the map boundaries.
 *
 * @param tile The tile for which to compute corner coordinates.
 * @return A tuple containing (x1, y1, x2, y2), representing the top and bottom corners.
 */
static std::tuple<uint, uint, uint, uint> GetTileCornersCoordinates(TileIndex tile)
{
	uint x1 = TileX(tile);
	uint y1 = TileY(tile);
	uint x2 = std::min(x1 + 1, Map::MaxX());
	uint y2 = std::min(y1 + 1, Map::MaxY());

	return {x1, y1, x2, y2};
}

/**
 * Get the heights of the four corners of a tile.
 * @param tile Tile to get the heights of.
 * @return A tuple containing the heights at the northern, western, eastern and southern corners in the same unit as TileHeight.
 */
static std::tuple<int, int, int, int> GetTileHeights(TileIndex tile)
{
	auto [x1, y1, x2, y2] = GetTileCornersCoordinates(tile);

	int hnorth = TileHeight(tile);           // Height of the North corner.
	int hwest  = TileHeight(TileXY(x2, y1)); // Height of the West corner.
	int heast  = TileHeight(TileXY(x1, y2)); // Height of the East corner.
	int hsouth = TileHeight(TileXY(x2, y2)); // Height of the South corner.

	return {hnorth, hwest, heast, hsouth};
}

/**
 * Return the slope of a given tile inside the map.
 * @param tile Tile to compute slope of
 * @return Slope of the tile, except for the HALFTILE part, and the z height
 */
std::tuple<Slope, int> GetTileSlopeZ(TileIndex tile)
{
	return GetTileSlopeGivenHeight(GetTileHeights(tile));
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

	auto [slope, h] = GetTileSlopeGivenHeight(std::make_tuple(hnorth, hwest, heast, hsouth));
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
	auto [x1, y1, x2, y2] = GetTileCornersCoordinates(tile);

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
	return GetTileMinHeight(GetTileHeights(tile));
}

/**
 * Get top height of the tile inside the map.
 * @param t Tile to compute height of
 * @return Maximum height of the tile
 */
int GetTileMaxZ(TileIndex t)
{
	return GetTileMaxHeight(GetTileHeights(t));
}
