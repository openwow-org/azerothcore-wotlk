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

#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "GameTime.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "WDataStore.h"
#include "User.h"

//This send to player windows for invite player to join the war
//Param1:(BattleId) the BattleId of Bf
//Param2:(ZoneId) the zone where the battle is (4197 for wg)
//Param3:(time) Time in second that the player have for accept
void User::SendBfInvitePlayerToWar(uint32 BattleId, uint32 ZoneId, uint32 p_time)
{
    //Send packet
    WDataStore data(SMSG_BATTLEFIELD_MGR_ENTRY_INVITE, 12);
    data << uint32(BattleId);
    data << uint32(ZoneId);
    data << uint32((GameTime::GetGameTime().count() + p_time));

    //Sending the packet to player
    Send(&data);
}

//This send invitation to player to join the queue
//Param1:(BattleId) the BattleId of Bf
void User::SendBfInvitePlayerToQueue(uint32 BattleId)
{
    WDataStore data(SMSG_BATTLEFIELD_MGR_QUEUE_INVITE, 5);

    data << uint32(BattleId);
    data << uint8(1);                                       //warmup ? used ?

    //Sending packet to player
    Send(&data);
}

//This send packet for inform player that he join queue
//Param1:(BattleId) the BattleId of Bf
//Param2:(ZoneId) the zone where the battle is (4197 for wg)
//Param3:(CanQueue) if able to queue
//Param4:(Full) on log in is full
void User::SendBfQueueInviteResponse(uint32 BattleId, uint32 ZoneId, bool CanQueue, bool Full)
{
    WDataStore data(SMSG_BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE, 11);
    data << uint32(BattleId);
    data << uint32(ZoneId);
    data << uint8((CanQueue ? 1 : 0));  //Accepted          //0 you cannot queue wg     //1 you are queued
    data << uint8((Full ? 0 : 1));      //Logging In        //0 wg full                 //1 queue for upcoming
    data << uint8(1); //Warmup
    Send(&data);
}

//This is call when player accept to join war
//Param1:(BattleId) the BattleId of Bf
void User::SendBfEntered(uint32 BattleId)
{
    //    m_PlayerInWar[player->GetTeamId()].insert(player->GetGUID());
    WDataStore data(SMSG_BATTLEFIELD_MGR_ENTERED, 7);
    data << uint32(BattleId);
    data << uint8(1);                                       //unk
    data << uint8(1);                                       //unk
    data << uint8(m_player->isAFK() ? 1 : 0);                //Clear AFK
    Send(&data);
}

void User::SendBfLeaveMessage(uint32 BattleId, BFLeaveReason reason)
{
    WDataStore data(SMSG_BATTLEFIELD_MGR_EJECTED, 7);
    data << uint32(BattleId);
    data << uint8(reason);//byte Reason
    data << uint8(2);//byte BattleStatus
    data << uint8(0);//bool Relocated
    Send(&data);
}

//Send by client when he click on accept for queue
void User::HandleBfQueueInviteResponse(WDataStore& recvData)
{
    uint32 BattleId;
    uint8 Accepted;

    recvData >> BattleId >> Accepted;

    Battlefield* Bf = sBattlefieldMgr->GetBattlefieldByBattleId(BattleId);
    if (!Bf)
        return;

    if (Accepted)
    {
        Bf->PlayerAcceptInviteToQueue(m_player);
    }
}

//Send by client on clicking in accept or refuse of invitation windows for join game
void User::HandleBfEntryInviteResponse(WDataStore& recvData)
{
    uint32 BattleId;
    uint8 Accepted;

    recvData >> BattleId >> Accepted;

    Battlefield* Bf = sBattlefieldMgr->GetBattlefieldByBattleId(BattleId);
    if (!Bf)
        return;

    //If player accept invitation
    if (Accepted)
    {
        Bf->PlayerAcceptInviteToWar(m_player);
    }
    else
    {
        if (m_player->GetZoneId() == Bf->GetZoneId())
            Bf->KickPlayerFromBattlefield(m_player->GetGUID());
    }
}

void User::HandleBfExitRequest(WDataStore& recvData)
{
    uint32 BattleId;

    recvData >> BattleId;

    Battlefield* Bf = sBattlefieldMgr->GetBattlefieldByBattleId(BattleId);
    if (!Bf)
        return;

    Bf->AskToLeaveQueue(m_player);
}
