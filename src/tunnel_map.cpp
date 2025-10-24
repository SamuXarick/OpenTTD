/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tunnel_map.cpp Map accessors for tunnels. */

#include "stdafx.h"
#include "tunnelbridge_map.h"

#include "safeguards.h"


/**
 * Gets the other end of the tunnel. Where a vehicle would reappear when it
 * enters at the given tile.
 * @param tile the tile to search from.
 * @return the tile of the other end of the tunnel.
 */
TileIndex GetOtherTunnelEnd(TileIndex tile)
{
	DiagDirection dir = GetTunnelBridgeDirection(tile);
	TileIndexDiff delta = TileOffsByDiagDir(dir);
	int z = GetTileZ(tile);

	dir = ReverseDiagDir(dir);
	do {
		tile += delta;
	} while (
		!IsTunnelTile(tile) ||
		GetTunnelBridgeDirection(tile) != dir ||
		GetTileZ(tile) != z
	);

	return tile;
}


/**
 * Is there a tunnel in the way in the given direction?
 * @param tile the tile to search from.
 * @param z    the 'z' to search on.
 * @param dir  the direction to start searching to.
 * @return true if and only if there is a tunnel.
 */
bool IsTunnelInWayDir(TileIndex tile, int z, DiagDirection dir)
{
	if (!IsAnyTunnelBelow(tile)) return false;

	TileIndexDiff delta = TileOffsByDiagDir(dir);
	int height;

	do {
		tile -= delta;
		if (!IsValidTile(tile)) return false;
		height = GetTileZ(tile);
	} while (z < height);

	return z == height && IsTunnelTile(tile) && GetTunnelBridgeDirection(tile) == dir;
}

/**
 * Is there a tunnel in the way in any direction?
 * @param tile the tile to search from.
 * @param z the 'z' to search on.
 * @return true if and only if there is a tunnel.
 */
bool IsTunnelInWay(TileIndex tile, int z)
{
	if (!IsAnyTunnelBelow(tile)) return false;

	return IsTunnelInWayDir(tile, z, (TileX(tile) > (Map::MaxX() / 2)) ? DIAGDIR_NE : DIAGDIR_SW) ||
			IsTunnelInWayDir(tile, z, (TileY(tile) > (Map::MaxY() / 2)) ? DIAGDIR_NW : DIAGDIR_SE);
}

/**
 * Is there a tunnel at any height along an axis?
 * @param start the tile to search from.
 * @param axis the axis to search on.
 * @retunr true if and only if there is a tunnel.
 */
static bool IsAnyTunnelAlongAxis(TileIndex start, Axis axis)
{
	DiagDirection search_dir;
	if (axis == AXIS_X) {
		search_dir = TileX(start) > (Map::MaxX() / 2) ? DIAGDIR_NE : DIAGDIR_SW;
	} else {
		search_dir = TileY(start) > (Map::MaxY() / 2) ? DIAGDIR_NW : DIAGDIR_SE;
	}

	TileIndexDiff delta = TileOffsByDiagDir(search_dir);
	int last_height = MAX_TILE_HEIGHT;

	for (TileIndex t = start - delta; IsValidTile(t); t -= delta) {
		if (last_height == 0) return false;
		last_height = GetTileZ(t);

		if (!IsTunnelTile(t)) continue;
		if (GetTunnelBridgeDirection(t) != search_dir) continue;

		TileIndex end = GetOtherTunnelEnd(t);
		if (end == start) continue;

		auto dist_end = Delta(t, end);
		auto dist_start = Delta(t, start);
		if (dist_end <= dist_start) continue;

		return true;
	}

	return false;
}

/**
 * Scans a diagonal tile segment to update underground tunnel markers after a tunnel is removed.
 *
 * The 'start' and 'end' tiles define a diagonal segment that was previously marked as having a
 * tunnel below. Since the tunnel is now gone, check each tile in that segment to determine whether
 * any other tunnels still exist below. If there's no more tunnels, the tile marker is cleared.
 *
 * @param start The first tile of the diagonal segment previously marked for tunnel presence.
 * @param end   The last tile of the diagonal segment previously marked for tunnel presence.
 * @param dir   The diagonal direction used to traverse the segment and determine axis alignment.
 */
void UpdateTunnelBelowMarkers(TileIndex start, TileIndex end, DiagDirection dir)
{
	TileIndexDiff delta = TileOffsByDiagDir(dir);
	Axis axis = DiagDirToAxis(dir);

	for (TileIndex t = start + delta; t != end; t += delta) {
		assert(IsAnyTunnelBelow(t));

		if (IsAnyTunnelAlongAxis(t, axis)) continue;
		if (IsAnyTunnelAlongAxis(t, OtherAxis(axis))) continue;

		ClearTunnelMiddle(t);
	}
}
