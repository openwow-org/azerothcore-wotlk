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

#include "ChatPackets.h"

WDataStore const* WorldPackets::Chat::Emote::Write()
{
    _worldPacket << EmoteID;
    _worldPacket << Guid;

    return &_worldPacket;
}

void WorldPackets::Chat::EmoteClient::Read()
{
    _worldPacket >> EmoteID;
}

WDataStore const* WorldPackets::Chat::ChatServerMessage::Write()
{
    _worldPacket << int32(MessageID);
    _worldPacket << StringParam;

    return &_worldPacket;
}
