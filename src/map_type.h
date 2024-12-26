/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file map_type.h Types related to maps. */

#ifndef MAP_TYPE_H
#define MAP_TYPE_H

struct TileOffset;

/**
 * A pair-construct of a TileOffset.
 *
 * This can be used to save the difference between to
 * tiles as a pair of x and y value.
 */
struct TileOffsetC {
	int16_t x;        ///< The x value of the coordinate
	int16_t y;        ///< The y value of the coordinate

	inline constexpr TileOffsetC operator*(const int16_t &amount) const { return TileOffsetC(this->x * amount, this->y * amount); }
	inline constexpr TileOffsetC &operator+=(const TileOffsetC &other) { this->x += other.x; this->y += other.y; return *this; }
};

/** Minimal and maximal map width and height */
static const uint MIN_MAP_SIZE_BITS = 6;                       ///< Minimal size of map is equal to 2 ^ MIN_MAP_SIZE_BITS
static const uint MAX_MAP_SIZE_BITS = 12;                      ///< Maximal size of map is equal to 2 ^ MAX_MAP_SIZE_BITS
static const uint MIN_MAP_SIZE      = 1U << MIN_MAP_SIZE_BITS; ///< Minimal map size = 64
static const uint MAX_MAP_SIZE      = 1U << MAX_MAP_SIZE_BITS; ///< Maximal map size = 4096

/** Argument for CmdLevelLand describing what to do. */
enum LevelMode : uint8_t {
	LM_LEVEL, ///< Level the land.
	LM_LOWER, ///< Lower the land.
	LM_RAISE, ///< Raise the land.
};

#endif /* MAP_TYPE_H */
