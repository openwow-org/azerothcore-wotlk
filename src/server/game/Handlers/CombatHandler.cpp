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

#include "CombatPackets.h"
#include "CreatureAI.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "Vehicle.h"
#include "WDataStore.h"
#include "User.h"

void User::HandleAttackSwingOpcode(WDataStore& recvData)
{
    WOWGUID guid;
    recvData >> guid;

    LOG_DEBUG("network", "WORLD: Recvd CMSG_ATTACKSWING: {}", guid.ToString());

    Unit* pEnemy = ObjectAccessor::GetUnit(*m_player, guid);

    if (!pEnemy)
    {
        // stop attack state at client
        SendAttackStop(nullptr);
        return;
    }

    if (!m_player->IsValidAttackTarget(pEnemy))
    {
        // stop attack state at client
        SendAttackStop(pEnemy);
        return;
    }

    //! Client explicitly checks the following before sending CMSG_ATTACKSWING packet,
    //! so we'll place the same check here. Note that it might be possible to reuse this snippet
    //! in other places as well.
    if (Vehicle* vehicle = m_player->GetVehicle())
    {
        VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(m_player);
        ASSERT(seat);
        if (!(seat->m_flags & VEHICLE_SEAT_FLAG_CAN_ATTACK))
        {
            SendAttackStop(pEnemy);
            return;
        }
    }

    m_player->Attack(pEnemy, true);
}

void User::HandleAttackStopOpcode(WDataStore& /*recvData*/)
{
    GetPlayer()->AttackStop();
}

void User::HandleSetSheathedOpcode(WorldPackets::Combat::SetSheathed& packet)
{
    if (packet.CurrentSheathState >= MAX_SHEATH_STATE)
    {
        LOG_ERROR("network.opcode", "Unknown sheath state {} ??", packet.CurrentSheathState);
        return;
    }

    m_player->SetSheath(SheathState(packet.CurrentSheathState));
}

void User::SendAttackStop(Unit const* enemy)
{
    WDataStore data(SMSG_ATTACKSTOP, (8 + 8 + 4)); // we guess size
    data << GetPlayer()->GetPackGUID();

    if (enemy)
    {
        data << enemy->GetPackGUID();               // must be packed guid
        data << (uint32)enemy->isDead();
    }
    Send(&data);
}
