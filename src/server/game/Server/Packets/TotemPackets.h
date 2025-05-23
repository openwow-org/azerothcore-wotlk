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

#ifndef TotemPackets_h__
#define TotemPackets_h__

#include "GUID.h"
#include "Packet.h"

namespace WorldPackets
{
    namespace Totem
    {
        class TotemDestroyed final : public ClientPacket
        {
        public:
            TotemDestroyed(WDataStore&& packet) : ClientPacket(CMSG_TOTEM_DESTROYED, std::move(packet)) { }

            void Read() override;

            uint8 Slot = 0;
        };

        class TotemCreated final : public ServerPacket
        {
        public:
            TotemCreated() : ServerPacket(SMSG_TOTEM_CREATED, 1 + 8 + 4 + 4) { }

            WDataStore const* Write() override;

            uint8 Slot = 0;
            WOWGUID Totem;
            uint32 Duration = 0;
            uint32 SpellID = 0;

        };
    }
}

#endif // TotemPackets_h__
