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

#include "GameTime.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "UpdateMask.h"
#include "WaypointMovementGenerator.h"
#include "WDataStore.h"
#include "User.h"

void User::HandleTaxiNodeStatusQueryOpcode(WDataStore& recvData)
{
    WOWGUID guid;

    recvData >> guid;
    SendTaxiStatus(guid);
}

void User::SendTaxiStatus(WOWGUID guid)
{
    Player* const player = GetPlayer();
    Creature* unit = ObjectAccessor::GetCreature(*player, guid);
    if (!unit || unit->IsHostileTo(player) || !unit->HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER))
    {
        LOG_DEBUG("network", "User::SendTaxiStatus - Unit ({}) not found.", guid.ToString());
        return;
    }

    // find taxi node
    uint32 nearest = sObjectMgr->GetNearestTaxiNode(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), unit->GetMapId(), player->GetTeamId());
    if (!nearest)
    {
        return;
    }

    WDataStore data(SMSG_TAXINODE_STATUS, 9);
    data << guid;
    data << uint8(player->m_taxi.IsTaximaskNodeKnown(nearest) ? 1 : 0);
    Send(&data);
    LOG_DEBUG("network", "WORLD: Sent SMSG_TAXINODE_STATUS");
}

void User::HandleTaxiQueryAvailableNodes(WDataStore& recvData)
{
    WOWGUID guid;
    recvData >> guid;

    // cheating checks
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!unit)
    {
        LOG_DEBUG("network", "WORLD: HandleTaxiQueryAvailableNodes - Unit ({}) not found or you can't interact with him.", guid.ToString());
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // unknown taxi node case
    if (SendLearnNewTaxiNode(unit))
        return;

    // known taxi node case
    SendTaxiMenu(unit);
}

void User::SendTaxiMenu(Creature* unit)
{
    // find current node
    uint32 curloc = sObjectMgr->GetNearestTaxiNode(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), unit->GetMapId(), GetPlayer()->GetTeamId());

    if (curloc == 0)
        return;

    bool lastTaxiCheaterState = GetPlayer()->isTaxiCheater();
    if (unit->GetEntry() == 29480) GetPlayer()->SetTaxiCheater(true); // Grimwing in Ebon Hold, special case. NOTE: Not perfect, Zul'Aman should not be included according to WoWhead, and I think taxicheat includes it.

    LOG_DEBUG("network", "WORLD: CMSG_TAXINODE_STATUS_QUERY {} ", curloc);

    WDataStore data(SMSG_SHOWTAXINODES, (4 + 8 + 4 + 8 * 4));
    data << uint32(1);
    data << unit->GetGUID();
    data << uint32(curloc);
    GetPlayer()->m_taxi.AppendTaximaskTo(data, GetPlayer()->isTaxiCheater());
    Send(&data);

    LOG_DEBUG("network", "WORLD: Sent SMSG_SHOWTAXINODES");

    GetPlayer()->SetTaxiCheater(lastTaxiCheaterState);
}

void User::SendDoFlight(uint32 mountDisplayId, uint32 path, uint32 pathNode)
{
    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    while (GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
        GetPlayer()->GetMotionMaster()->MovementExpired(false);

    if (mountDisplayId)
        GetPlayer()->Mount(mountDisplayId);

    if (Creature* critter = ObjectAccessor::GetCreature(*GetPlayer(), GetPlayer()->GetCritterGUID()))
        critter->DespawnOrUnsummon();

    GetPlayer()->GetMotionMaster()->MoveTaxiFlight(path, pathNode);
}

bool User::SendLearnNewTaxiNode(Creature* unit)
{
    // find current node
    uint32 curloc = sObjectMgr->GetNearestTaxiNode(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), unit->GetMapId(), GetPlayer()->GetTeamId());

    if (curloc == 0)
        return true;                                        // `true` send to avoid User::SendTaxiMenu call with one more curlock seartch with same false result.

    if (GetPlayer()->m_taxi.SetTaximaskNode(curloc))
    {
        WDataStore msg(SMSG_NEW_TAXI_PATH, 0);
        Send(&msg);

        WDataStore update(SMSG_TAXINODE_STATUS, 9);
        update << unit->GetGUID();
        update << uint8(1);
        Send(&update);

        return true;
    }
    else
        return false;
}

void User::SendDiscoverNewTaxiNode(uint32 nodeid)
{
    if (GetPlayer()->m_taxi.SetTaximaskNode(nodeid))
    {
        WDataStore msg(SMSG_NEW_TAXI_PATH, 0);
        Send(&msg);
    }
}

void User::HandleActivateTaxiExpressOpcode(WDataStore& recvData)
{
    WOWGUID guid;
    uint32 node_count;

    recvData >> guid >> node_count;

    Creature* npc = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!npc)
    {
        LOG_DEBUG("network", "WORLD: HandleActivateTaxiExpressOpcode - Unit ({}) not found or you can't interact with it.", guid.ToString());
        SendActivateTaxiReply(ERR_TAXITOOFARAWAY);
        return;
    }
    std::vector<uint32> nodes;

    for (uint32 i = 0; i < node_count; ++i)
    {
        uint32 node;
        recvData >> node;

        if (!GetPlayer()->m_taxi.IsTaximaskNodeKnown(node) && !GetPlayer()->isTaxiCheater())
        {
            SendActivateTaxiReply(ERR_TAXINOTVISITED);
            recvData.rfinish();
            return;
        }

        nodes.push_back(node);
    }

    if (nodes.empty())
        return;

    LOG_DEBUG("network", "WORLD: Received CMSG_ACTIVATETAXIEXPRESS from {} to {}", nodes.front(), nodes.back());

    GetPlayer()->ActivateTaxiPathTo(nodes, npc, 0);
}

void User::HandleMoveSplineDoneOpcode(WDataStore& recvData)
{
    WOWGUID guid; // used only for proper packet read
    recvData >> guid.ReadAsPacked();

    CMovement movementInfo;                              // used only for proper packet read
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                          // spline id

    // in taxi flight packet received in 2 case:
    // 1) end taxi path in far (multi-node) flight
    // 2) switch from one map to other in case multim-map taxi path
    // we need process only (1)

    uint32 curDest = GetPlayer()->m_taxi.GetTaxiDestination();
    if (curDest)
    {
        TaxiNodesEntry const* curDestNode = sTaxiNodesStore.LookupEntry(curDest);

        // far teleport case
        if (curDestNode && curDestNode->map_id != GetPlayer()->GetMapId() && GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
        {
            if (FlightPathMovementGenerator* flight = dynamic_cast<FlightPathMovementGenerator*>(GetPlayer()->GetMotionMaster()->top()))
            {
                // short preparations to continue flight
                flight->SetCurrentNodeAfterTeleport();
                TaxiPathNodeEntry const* node = flight->GetPath()[flight->GetCurrentNode()];
                flight->SkipCurrentNode();

                GetPlayer()->Teleport(curDestNode->map_id, node->x, node->y, node->z, GetPlayer()->GetFacing(), TELE_TO_NOT_LEAVE_TAXI);
            }
        }

        return;
    }

    // at this point only 1 node is expected (final destination)
    if (GetPlayer()->m_taxi.GetPath().size() != 1)
    {
        return;
    }

    GetPlayer()->CleanupAfterTaxiFlight();
    GetPlayer()->SetFallInformation(GameTime::GetGameTime().count(), GetPlayer()->GetPositionZ());
    if (GetPlayer()->pvpInfo.IsHostile)
    {
        GetPlayer()->CastSpell(GetPlayer(), 2479, true);
    }
}

void User::HandleActivateTaxiOpcode(WDataStore& recvData)
{
    WOWGUID guid;
    std::vector<uint32> nodes;
    nodes.resize(2);
    GetPlayer()->SetCanTeleport(true);
    recvData >> guid >> nodes[0] >> nodes[1];
    LOG_DEBUG("network", "WORLD: Received CMSG_ACTIVATETAXI from {} to {}", nodes[0], nodes[1]);
    Creature* npc = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!npc)
    {
        LOG_DEBUG("network", "WORLD: HandleActivateTaxiOpcode - Unit ({}) not found or you can't interact with it.", guid.ToString());
        SendActivateTaxiReply(ERR_TAXITOOFARAWAY);
        return;
    }

    if (!GetPlayer()->isTaxiCheater())
    {
        if (!GetPlayer()->m_taxi.IsTaximaskNodeKnown(nodes[0]) || !GetPlayer()->m_taxi.IsTaximaskNodeKnown(nodes[1]))
        {
            SendActivateTaxiReply(ERR_TAXINOTVISITED);
            return;
        }
    }

    GetPlayer()->ActivateTaxiPathTo(nodes, npc, 0);
}

void User::SendActivateTaxiReply(ActivateTaxiReply reply)
{
    GetPlayer()->SetCanTeleport(true);
    WDataStore data(SMSG_ACTIVATETAXIREPLY, 4);
    data << uint32(reply);
    Send(&data);

    LOG_DEBUG("network", "WORLD: Sent SMSG_ACTIVATETAXIREPLY");
}
