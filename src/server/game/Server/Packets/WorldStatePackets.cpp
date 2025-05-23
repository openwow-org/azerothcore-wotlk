/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "WorldStatePackets.h"

WorldPackets::WorldState::InitWorldStates::InitWorldStates() : ServerPacket(SMSG_INIT_WORLD_STATES, 4 + 4 + 4 + 2) { }

WDataStore const* WorldPackets::WorldState::InitWorldStates::Write()
{
    _worldPacket.reserve(4 + 4 + 4 + 2 + Worldstates.size() * 8);

    _worldPacket << int32(MapID);
    _worldPacket << int32(ZoneID);
    _worldPacket << int32(AreaID);

    _worldPacket << uint16(Worldstates.size());
    for (WorldStateInfo const& wsi : Worldstates)
    {
        _worldPacket << int32(wsi.VariableID);
        _worldPacket << int32(wsi.Value);
    }

    return &_worldPacket;
}

WDataStore const* WorldPackets::WorldState::UpdateWorldState::Write()
{
    _worldPacket << int32(VariableID);
    _worldPacket << int32(Value);

    return &_worldPacket;
}
