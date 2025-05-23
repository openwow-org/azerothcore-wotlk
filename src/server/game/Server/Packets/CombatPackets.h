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

#ifndef CombatPackets_h__
#define CombatPackets_h__

#include "GUID.h"
#include "Packet.h"

namespace WorldPackets
{
    namespace Combat
    {
        class SetSheathed final : public ClientPacket
        {
        public:
            SetSheathed(WDataStore&& packet) : ClientPacket(CMSG_SET_SHEATHED, std::move(packet)) { }

            void Read() override;

            uint32 CurrentSheathState = 0;
        };
    }
}

#endif // CombatPackets_h__
