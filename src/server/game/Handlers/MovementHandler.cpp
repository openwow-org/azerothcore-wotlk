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

#include "ArenaSpectator.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Corpse.h"
#include "GameGraveyard.h"
#include "GameTime.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "MapMgr.h"
#include "MathUtil.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "Transport.h"
#include "Vehicle.h"
#include "WaypointMovementGenerator.h"
#include "WDataStore.h"
#include "User.h"

#define MOVEMENT_PACKET_TIME_DELAY 0

void User::HandleMoveWorldportAckOpcode(WDataStore& /*recvData*/)
{
    LOG_DEBUG("network", "WORLD: got MSG_MOVE_WORLDPORT_ACK.");
    HandleMoveWorldportAck();
}

void User::HandleMoveWorldportAck()
{
    // ignore unexpected far teleports
    if (!GetPlayer()->IsBeingTeleportedFar())
        return;

    GetPlayer()->SetSemaphoreTeleportFar(0);

    // get the teleport destination
    WorldLocation const& loc = GetPlayer()->GetTeleportDest();

    // possible errors in the coordinate validity check
    if (!MapMgr::IsValidMapCoord(loc))
    {
        KickPlayer("!MapMgr::IsValidMapCoord(loc)");
        return;
    }

    // get the destination map entry, not the current one, this will fix homebind and reset greeting
    MapEntry const* mEntry = sMapStore.LookupEntry(loc.GetMapId());
    InstanceTemplate const* mInstance = sObjectMgr->GetInstanceTemplate(loc.GetMapId());

    Map* oldMap = GetPlayer()->GetMap();
    if (GetPlayer()->IsInWorld())
    {
        LOG_ERROR("network.opcode", "Player (Name {}) is still in world when teleported from map {} to new map {}", GetPlayer()->GetName(), oldMap->GetId(), loc.GetMapId());
        oldMap->RemovePlayerFromMap(GetPlayer(), false);
    }

    // reset instance validity, except if going to an instance inside an instance
    if (!GetPlayer()->m_InstanceValid && !mInstance)
    {
        GetPlayer()->m_InstanceValid = true;
        // pussywizard: m_InstanceValid can be false only by leaving a group in an instance => so remove temp binds that could not be removed because player was still on the map!
        if (!sInstanceSaveMgr->PlayerIsPermBoundToInstance(GetPlayer()->GetGUID(), oldMap->GetId(), oldMap->GetDifficulty()))
            sInstanceSaveMgr->PlayerUnbindInstance(GetPlayer()->GetGUID(), oldMap->GetId(), oldMap->GetDifficulty(), true);
    }

    // relocate the player to the teleport destination
    Map* newMap = sMapMgr->CreateMap(loc.GetMapId(), GetPlayer());
    // the CanEnter checks are done in TeleporTo but conditions may change
    // while the player is in transit, for example the map may get full
    if (!newMap || newMap->CannotEnter(GetPlayer(), false))
    {
        LOG_ERROR("network.opcode", "Map {} could not be created for player {}, porting player to homebind", loc.GetMapId(), GetPlayer()->GetGUID().ToString());
        GetPlayer()->Teleport(GetPlayer()->m_homebindMapId, GetPlayer()->m_homebindX, GetPlayer()->m_homebindY, GetPlayer()->m_homebindZ, GetPlayer()->GetOrientation());
        return;
    }

    float z = loc.GetPositionZ() + GetPlayer()->GetHoverHeight();
    GetPlayer()->Relocate(loc.GetPositionX(), loc.GetPositionY(), z, loc.GetOrientation());

    GetPlayer()->ResetMap();
    GetPlayer()->SetMap(newMap);

    GetPlayer()->UpdatePositionData();

    GetPlayer()->SendInitialPacketsBeforeAddToMap();
    if (!GetPlayer()->GetMap()->AddPlayerToMap(GetPlayer()))
    {
        LOG_ERROR("network.opcode", "WORLD: failed to teleport player {} ({}) to map {} because of unknown reason!",
            GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString(), loc.GetMapId());
        GetPlayer()->ResetMap();
        GetPlayer()->SetMap(oldMap);
        GetPlayer()->Teleport(GetPlayer()->m_homebindMapId, GetPlayer()->m_homebindX, GetPlayer()->m_homebindY, GetPlayer()->m_homebindZ, GetPlayer()->GetOrientation());
        return;
    }

    oldMap->AfterPlayerUnlinkFromMap();

    // pussywizard: transport teleport couldn't teleport us to the same map (some other teleport pending, reqs not met, etc.), but we still have transport set until player moves! clear it if map differs (crashfix)
    if (Transport* t = m_player->GetTransport())
        if (!t->IsInMap(m_player))
        {
            t->RemovePassenger(m_player);
            m_player->m_transport = nullptr;
            m_player->m_movement.transport.Reset();
            m_player->m_movement.m_moveFlags &= ~MOVEFLAG_IMMOBILIZED;
        }

    if (!m_player->getHostileRefMgr().IsEmpty())
        m_player->getHostileRefMgr().deleteReferences(true); // pussywizard: multithreading crashfix

    CellCoord pair(Acore::ComputeCellCoord(GetPlayer()->GetPositionX(), GetPlayer()->GetPositionY()));
    Cell cell(pair);
    if (!GridCoord(cell.GridX(), cell.GridY()).IsCoordValid())
    {
        KickPlayer("!GridCoord(cell.GridX(), cell.GridY()).IsCoordValid()");
        return;
    }
    newMap->LoadGrid(GetPlayer()->GetPositionX(), GetPlayer()->GetPositionY());

    // pussywizard: player supposed to enter bg map
    if (m_player->InBattleground())
    {
        // but landed on another map, cleanup data
        if (!mEntry->IsBattlegroundOrArena())
            m_player->SetBattlegroundId(0, BATTLEGROUND_TYPE_NONE, PLAYER_MAX_BATTLEGROUND_QUEUES, false, false, TEAM_NEUTRAL);
        // everything ok
        else if (Battleground* bg = m_player->GetBattleground())
        {
            if (m_player->IsInvitedForBattlegroundInstance()) // GMs are not invited, so they are not added to participants
                bg->AddPlayer(m_player);
        }
    }

    // pussywizard: arena spectator stuff
    {
        if (newMap->IsBattleArena() && ((BattlegroundMap*)newMap)->GetBG() && m_player->HasPendingSpectatorForBG(((BattlegroundMap*)newMap)->GetInstanceId()))
        {
            m_player->ClearReceivedSpectatorResetFor();
            m_player->SetIsSpectator(true);
            ArenaSpectator::SendCommand(m_player, "%sENABLE", SPECTATOR_ADDON_PREFIX);
            ((BattlegroundMap*)newMap)->GetBG()->AddSpectator(m_player);
            ArenaSpectator::HandleResetCommand(m_player);
        }
        else
            m_player->SetIsSpectator(false);

        GetPlayer()->SetPendingSpectatorForBG(0);

        if (uint32 inviteInstanceId = m_player->GetPendingSpectatorInviteInstanceId())
        {
            if (Battleground* tbg = sBattlegroundMgr->GetBattleground(inviteInstanceId, BATTLEGROUND_TYPE_NONE))
                tbg->RemoveToBeTeleported(m_player->GetGUID());
            m_player->SetPendingSpectatorInviteInstanceId(0);
        }
    }

    // xinef: do this again, player can be teleported inside bg->AddPlayer(m_player)!!!!
    CellCoord pair2(Acore::ComputeCellCoord(GetPlayer()->GetPositionX(), GetPlayer()->GetPositionY()));
    Cell cell2(pair2);
    if (!GridCoord(cell2.GridX(), cell2.GridY()).IsCoordValid())
    {
        KickPlayer("!GridCoord(cell2.GridX(), cell2.GridY()).IsCoordValid()");
        return;
    }
    newMap->LoadGrid(GetPlayer()->GetPositionX(), GetPlayer()->GetPositionY());

    GetPlayer()->SendInitialPacketsAfterAddToMap();

    // flight fast teleport case
    if (GetPlayer()->IsOnTaxi())
    {
        if (!GetPlayer()->InBattleground())
        {
            // short preparations to continue flight
            MovementGenerator* movementGenerator = GetPlayer()->GetMotionMaster()->top();
            movementGenerator->Initialize(GetPlayer());
            return;
        }

        // battleground state prepare, stop flight
        GetPlayer()->GetMotionMaster()->MovementExpired();
        GetPlayer()->CleanupAfterTaxiFlight();
    }

    // resurrect character at enter into instance where his corpse exist after add to map
    Corpse* corpse = GetPlayer()->GetMap()->GetCorpseByPlayer(GetPlayer()->GetGUID());
    if (corpse && corpse->GetType() != CORPSE_BONES)
    {
        if (mEntry->IsDungeon())
        {
            GetPlayer()->Resurrect(0.5f);
            GetPlayer()->SpawnCorpseBones();
        }
    }

    if (!corpse && mEntry->IsDungeon())
    {
        // resurrect character upon entering instance when the corpse is not available anymore
        if (GetPlayer()->GetCorpseLocation().GetMapId() == mEntry->MapID)
        {
            GetPlayer()->Resurrect(0.5f);
            GetPlayer()->RemoveCorpse();
        }
    }

    bool allowMount = !mEntry->IsDungeon() || mEntry->IsBattlegroundOrArena();
    if (mInstance)
    {
        Difficulty diff = GetPlayer()->GetDifficulty(mEntry->IsRaid());
        if (MapDifficulty const* mapDiff = GetMapDifficultyData(mEntry->MapID, diff))
            if (mapDiff->resetTime)
                if (time_t timeReset = sInstanceSaveMgr->GetResetTimeFor(mEntry->MapID, diff))
                {
                    uint32 timeleft = uint32(timeReset - GameTime::GetGameTime().count());
                    GetPlayer()->SendInstanceResetWarning(mEntry->MapID, diff, timeleft, true);
                }
        allowMount = mInstance->AllowMount;
    }

    // mount allow check
    if (!allowMount)
        m_player->RemoveAurasByType(SPELL_AURA_MOUNTED);

    // update zone immediately, otherwise leave channel will cause crash in mtmap
    uint32 newzone, newarea;
    GetPlayer()->GetZoneAndAreaId(newzone, newarea);
    GetPlayer()->UpdateZone(newzone, newarea);

    // honorless target
    if (GetPlayer()->pvpInfo.IsHostile)
        GetPlayer()->CastSpell(GetPlayer(), 2479, true);

    // in friendly area
    else if (GetPlayer()->IsPvP() && !GetPlayer()->HasPlayerFlag(PLAYER_FLAGS_IN_PVP))
        GetPlayer()->UpdatePvP(false, false);

    // resummon pet
    GetPlayer()->ResummonPetTemporaryUnSummonedIfAny();

    //lets process all delayed operations on successful teleport
    GetPlayer()->ProcessDelayedOperations();
}

void User::HandleMoveTeleportAck(WDataStore& recvData)
{
    LOG_DEBUG("network", "MSG_MOVE_TELEPORT_ACK");

    WOWGUID guid;
    recvData >> guid.ReadAsPacked();

    uint32 flags, time;
    recvData >> flags >> time; // unused
    LOG_DEBUG("network.opcode", "Guid {}", guid.ToString());
    LOG_DEBUG("network.opcode", "Flags {}, time {}", flags, time / IN_MILLISECONDS);

    Player* plMover = m_player->m_mover->ToPlayer();

    if (!plMover || !plMover->IsBeingTeleportedNear())
        return;

    if (guid != plMover->GetGUID())
        return;

    plMover->SetSemaphoreTeleportNear(0);

    uint32 old_zone = plMover->GetZoneId();

    WorldLocation const& dest = plMover->GetTeleportDest();
    Position oldPos(*plMover);

    plMover->UpdatePosition(dest, true);

    // xinef: teleport pets if they are not unsummoned
    if (Pet* pet = plMover->GetPet())
    {
        if (!pet->IsWithinDist3d(plMover, plMover->GetMap()->GetVisibilityRange() - 5.0f))
            pet->NearTeleportTo(plMover->GetPositionX(), plMover->GetPositionY(), plMover->GetPositionZ(), pet->GetOrientation());
    }

    if (oldPos.GetExactDist2d(plMover) > 100.0f)
    {
        uint32 newzone, newarea;
        plMover->GetZoneAndAreaId(newzone, newarea);
        plMover->UpdateZone(newzone, newarea);

        // new zone
        if (old_zone != newzone)
        {
            // honorless target
            if (plMover->pvpInfo.IsHostile)
                plMover->CastSpell(plMover, 2479, true);

            // in friendly area
            else if (plMover->IsPvP() && !plMover->HasPlayerFlag(PLAYER_FLAGS_IN_PVP))
                plMover->UpdatePvP(false, false);
        }
    }

    // resummon pet
    GetPlayer()->ResummonPetTemporaryUnSummonedIfAny();

    //lets process all delayed operations on successful teleport
    GetPlayer()->ProcessDelayedOperations();

    plMover->GetMotionMaster()->ReinitializeMovement();

    // pussywizard: client forgets about losing control, resend it
    if (plMover->HasUnitState(UNIT_STATE_FLEEING | UNIT_STATE_CONFUSED) || plMover->IsCharmed()) // only in such cases SetClientControl(self, false) is sent
        plMover->SetClientControl(plMover, false, true);
}

void User::HandleMovementOpcodes(WDataStore& recvData)
{
    uint16 opcode = recvData.GetOpcode();

    Unit* mover = m_player->m_mover;

    ASSERT(mover);                      // there must always be a mover

    Player* plrMover = mover->ToPlayer();

    // ignore, waiting processing in User::HandleMoveWorldportAckOpcode and User::HandleMoveTeleportAck
    if (plrMover && plrMover->IsBeingTeleported())
    {
        recvData.rfinish();                     // prevent warnings spam
        return;
    }

    /* extract packet */
    WOWGUID guid;
    recvData >> guid.ReadAsPacked();

    // prevent tampered movement data
    if (!guid || guid != mover->GetGUID())
    {
        recvData.rfinish();                     // prevent warnings spam
        return;
    }

    // pussywizard: typical check for incomming movement packets | prevent tampered movement data
    if (!mover || !(mover->IsInWorld()) || mover->IsDuringRemoveFromWorld() || guid != mover->GetGUID())
    {
        recvData.rfinish();                     // prevent warnings spam
        return;
    }

    CMovement movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    // Stop emote on move
    if (Player* plrMover = mover->ToPlayer())
    {
        if (plrMover->GetUInt32Value(UNIT_NPC_EMOTESTATE) != EMOTE_ONESHOT_NONE && (movementInfo.m_moveFlags & MOVEFLAG_MOTION_MASK) != 0)
        {
            plrMover->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
        }
    }

    if (!movementInfo.pos.IsPositionValid())
    {
        if (plrMover)
        {
            sScriptMgr->AnticheatUpdateMovementInfo(plrMover, movementInfo);
        }

        recvData.rfinish();                     // prevent warnings spam
        return;
    }

    if (!mover->movespline->Finalized())
    {
        recvData.rfinish();                     // prevent warnings spam
        return;
    }

    // Xinef: do not allow to move with UNIT_FLAG_DISABLE_MOVE
    if (mover->HasUnitFlag(UNIT_FLAG_DISABLE_MOVE))
    {
        // Xinef: skip moving packets
        if ((movementInfo.m_moveFlags & MOVEFLAG_MOVE_MASK) != 0)
        {
            if (plrMover)
            {
                sScriptMgr->AnticheatUpdateMovementInfo(plrMover, movementInfo);
            }
            return;
        }
        movementInfo.pos.Relocate(mover->GetPositionX(), mover->GetPositionY(), mover->GetPositionZ());

        if (mover->GetTypeId() == TYPEID_UNIT)
        {
            movementInfo.transport.guid = mover->m_movement.transport.guid;
            movementInfo.transport.pos.Relocate(mover->m_movement.transport.pos.GetPositionX(), mover->m_movement.transport.pos.GetPositionY(), mover->m_movement.transport.pos.GetPositionZ());
            movementInfo.transport.seat = mover->m_movement.transport.seat;
        }
    }

    if ((movementInfo.m_moveFlags & MOVEFLAG_IMMOBILIZED) != 0)
    {
        // We were teleported, skip packets that were broadcast before teleport
        if (movementInfo.pos.GetExactDist2d(mover) > SIZE_OF_GRIDS)
        {
            if (plrMover)
            {
                sScriptMgr->AnticheatUpdateMovementInfo(plrMover, movementInfo);
                //LOG_INFO("anticheat", "MovementHandler:: 2 We were teleported, skip packets that were broadcast before teleport");
            }
            recvData.rfinish();                 // prevent warnings spam
            return;
        }

        if (!Acore::IsValidMapCoord(movementInfo.pos.GetPositionX() + movementInfo.transport.pos.GetPositionX(), movementInfo.pos.GetPositionY() + movementInfo.transport.pos.GetPositionY(),
                                    movementInfo.pos.GetPositionZ() + movementInfo.transport.pos.GetPositionZ(), movementInfo.pos.GetOrientation() + movementInfo.transport.pos.GetOrientation()))
        {
            if (plrMover)
            {
                sScriptMgr->AnticheatUpdateMovementInfo(plrMover, movementInfo);
            }

            recvData.rfinish();                   // prevent warnings spam
            return;
        }

        // if we boarded a transport, add us to it
        if (plrMover)
        {
            if (!plrMover->GetTransport())
            {
                if (Transport* transport = plrMover->GetMap()->GetTransport(movementInfo.transport.guid))
                {
                    plrMover->m_transport = transport;
                    transport->AddPassenger(plrMover);
                }
            }
            else if (plrMover->GetTransport()->GetGUID() != movementInfo.transport.guid)
            {
                bool foundNewTransport = false;
                plrMover->m_transport->RemovePassenger(plrMover);
                if (Transport* transport = plrMover->GetMap()->GetTransport(movementInfo.transport.guid))
                {
                    foundNewTransport = true;
                    plrMover->m_transport = transport;
                    transport->AddPassenger(plrMover);
                }

                if (!foundNewTransport)
                {
                    plrMover->m_transport = nullptr;
                    movementInfo.transport.Reset();
                }
            }
        }

        if (!mover->GetTransport() && !mover->GetVehicle())
        {
            GameObject* go = mover->GetMap()->GetGameObject(movementInfo.transport.guid);
            if (!go || go->GetGoType() != GAMEOBJECT_TYPE_TRANSPORT)
            {
                movementInfo.m_moveFlags &= ~MOVEFLAG_IMMOBILIZED;
            }
        }
    }
    else if (plrMover && plrMover->GetTransport()) // if we were on a transport, leave
    {
        sScriptMgr->AnticheatSetUnderACKmount(plrMover); // just for safe

        plrMover->m_transport->RemovePassenger(plrMover);
        plrMover->m_transport = nullptr;
        movementInfo.transport.Reset();
    }

    // fall damage generation (ignore in flight case that can be triggered also at lags in moment teleportation to another map).
    if (opcode == MSG_MOVE_FALL_LAND && plrMover && !plrMover->IsOnTaxi())
    {
        plrMover->HandleFall(movementInfo);

        sScriptMgr->AnticheatSetJumpingbyOpcode(plrMover, false);
    }

    // interrupt parachutes upon falling or landing in water
    if (opcode == MSG_MOVE_FALL_LAND || opcode == MSG_MOVE_START_SWIM)
    {
        mover->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_LANDING); // Parachutes

        if (plrMover)
        {
            sScriptMgr->AnticheatSetJumpingbyOpcode(plrMover, false);
        }
    }

    if (plrMover && ((movementInfo.m_moveFlags & MOVEFLAG_SWIMMING) != 0) != plrMover->IsInWater())
    {
        // now client not include swimming flag in case jumping under water
        plrMover->SetInWater(!plrMover->IsInWater() || plrMover->GetMap()->IsUnderWater(plrMover->GetPhaseMask(), movementInfo.pos.GetPositionX(),
            movementInfo.pos.GetPositionY(), movementInfo.pos.GetPositionZ(), plrMover->GetCollisionHeight()));
    }

    if (plrMover)//Hook for OnPlayerMove
    {
        sScriptMgr->OnPlayerMove(plrMover, movementInfo, opcode);
    }

    bool jumpopcode = false;
    if (opcode == MSG_MOVE_JUMP)
    {
        jumpopcode = true;
        if (plrMover && !sScriptMgr->AnticheatHandleDoubleJump(plrMover, mover))
        {
            plrMover->User()->KickPlayer();
            return;
        }
    }

    /* start some hack detection */
    if (plrMover && !sScriptMgr->AnticheatCheckMovementInfo(plrMover, movementInfo, mover, jumpopcode))
    {
        plrMover->User()->KickPlayer();
        return;
    }

    /* process position-change */
    WDataStore data(opcode, recvData.size());
    int64 movementTime = (int64)movementInfo.time + _timeSyncClockDelta;
    if (_timeSyncClockDelta == 0 || movementTime < 0 || movementTime > 0xFFFFFFFF)
    {
        LOG_INFO("misc", "The computed movement time using clockDelta is erronous. Using fallback instead");
        movementInfo.time = getMSTime();
    }
    else
    {
        movementInfo.time = (uint32) movementTime;
    }

    movementInfo.guid = mover->GetGUID();
    WriteMovementInfo(&data, &movementInfo);
    mover->SendMessageToSet(&data, m_player);

    mover->m_movement = movementInfo;

    // Some vehicles allow the passenger to turn by himself
    if (Vehicle* vehicle = mover->GetVehicle())
    {
        if (VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(mover))
        {
            if (seat->m_flags & VEHICLE_SEAT_FLAG_ALLOW_TURNING && movementInfo.pos.GetOrientation() != mover->GetOrientation())
            {
                mover->SetOrientation(movementInfo.pos.GetOrientation());
                mover->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TURNING);
            }
        }

        return;
    }

    mover->UpdatePosition(movementInfo.pos);

    if (plrMover)                                            // nothing is charmed, or player charmed
    {
        if (plrMover->IsSitting() && (movementInfo.m_moveFlags & (MOVEFLAG_MOVE_MASK | MOVEFLAG_TURN_MASK)))
            plrMover->SetStandState(UNIT_STANDING);

        plrMover->UpdateFallInformationIfNeed(movementInfo, opcode);

        if (movementInfo.pos.GetPositionZ() < plrMover->GetMap()->GetMinHeight(movementInfo.pos.GetPositionX(), movementInfo.pos.GetPositionY()))
            if (!plrMover->GetBattleground() || !plrMover->GetBattleground()->HandlePlayerUnderMap(m_player))
            {
                if (plrMover->IsAlive())
                {
                    plrMover->SetPlayerFlag(PLAYER_FLAGS_IS_OUT_OF_BOUNDS);
                    plrMover->EnvironmentalDamage(DAMAGE_FALL_TO_VOID, GetPlayer()->GetMaxHealth());
                    // player can be alive if GM
                    if (plrMover->IsAlive())
                        plrMover->KillPlayer();
                }
                else if (!plrMover->HasPlayerFlag(PLAYER_FLAGS_IS_OUT_OF_BOUNDS))
                {
                    GraveyardStruct const* grave = sGraveyard->GetClosestGraveyard(plrMover, plrMover->GetTeamId());
                    if (grave)
                    {
                        plrMover->Teleport(grave->Map, grave->x, grave->y, grave->z, plrMover->GetOrientation());
                        plrMover->Relocate(grave->x, grave->y, grave->z, plrMover->GetOrientation());
                    }
                }
            }
    }
}

void User::HandleForceSpeedChangeAck(WDataStore& recvData)
{
    uint32 opcode = recvData.GetOpcode();
    LOG_DEBUG("network", "WORLD: Recvd {} ({}, 0x{:X}) opcode", GetOpcodeNameForLogging(static_cast<OpcodeClient>(opcode)), opcode, opcode);

    /* extract packet */
    WOWGUID guid;
    uint32 unk1;
    float  newspeed;

    recvData >> guid.ReadAsPacked();

    // pussywizard: special check, only player mover allowed here
    if (guid != m_player->m_mover->GetGUID() || guid != m_player->GetGUID())
    {
        recvData.rfinish(); // prevent warnings spam
        return;
    }

    // continue parse packet
    recvData >> unk1;                                      // counter or moveEvent

    CMovement movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData >> newspeed;

    // client ACK send one packet for mounted/run case and need skip all except last from its
    // in other cases anti-cheat check can be fail in false case
    UnitMoveType move_type;
    UnitMoveType force_move_type;

    static char const* move_type_name[MAX_MOVE_TYPE] = {  "Walk", "Run", "RunBack", "Swim", "SwimBack", "TurnRate", "Flight", "FlightBack", "PitchRate" };

    switch (opcode)
    {
        case CMSG_FORCE_WALK_SPEED_CHANGE_ACK:
            move_type = MOVE_WALK;
            force_move_type = MOVE_WALK;
            break;
        case CMSG_FORCE_RUN_SPEED_CHANGE_ACK:
            move_type = MOVE_RUN;
            force_move_type = MOVE_RUN;
            break;
        case CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK:
            move_type = MOVE_RUN_BACK;
            force_move_type = MOVE_RUN_BACK;
            break;
        case CMSG_FORCE_SWIM_SPEED_CHANGE_ACK:
            move_type = MOVE_SWIM;
            force_move_type = MOVE_SWIM;
            break;
        case CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK:
            move_type = MOVE_SWIM_BACK;
            force_move_type = MOVE_SWIM_BACK;
            break;
        case CMSG_FORCE_TURN_RATE_CHANGE_ACK:
            move_type = MOVE_TURN_RATE;
            force_move_type = MOVE_TURN_RATE;
            break;
        case CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK:
            move_type = MOVE_FLIGHT;
            force_move_type = MOVE_FLIGHT;
            break;
        case CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK:
            move_type = MOVE_FLIGHT_BACK;
            force_move_type = MOVE_FLIGHT_BACK;
            break;
        case CMSG_FORCE_PITCH_RATE_CHANGE_ACK:
            move_type = MOVE_PITCH_RATE;
            force_move_type = MOVE_PITCH_RATE;
            break;
        default:
            LOG_ERROR("network.opcode", "User::HandleForceSpeedChangeAck: Unknown move type opcode: {}", opcode);
            return;
    }

    sScriptMgr->AnticheatSetUnderACKmount(m_player);

    // skip all forced speed changes except last and unexpected
    // in run/mounted case used one ACK and it must be skipped.m_forced_speed_changes[MOVE_RUN} store both.
    if (m_player->m_forced_speed_changes[force_move_type] > 0)
    {
        --m_player->m_forced_speed_changes[force_move_type];
        if (m_player->m_forced_speed_changes[force_move_type] > 0)
            return;
    }

    if (!m_player->GetTransport() && std::fabs(m_player->GetSpeed(move_type) - newspeed) > 0.01f)
    {
        if (m_player->GetSpeed(move_type) > newspeed)         // must be greater - just correct
        {
            LOG_ERROR("network.opcode", "{}SpeedChange player {} is NOT correct (must be {} instead {}), force set to correct value",
                           move_type_name[move_type], m_player->GetName(), m_player->GetSpeed(move_type), newspeed);
            m_player->SetSpeed(move_type, m_player->GetSpeedRate(move_type), true);
        }
        else                                                // must be lesser - cheating
        {
            LOG_INFO("network.opcode", "Player {} from account id {} kicked for incorrect speed (must be {} instead {})",
                           m_player->GetName(), GetAccountId(), m_player->GetSpeed(move_type), newspeed);
            KickPlayer("Incorrect speed");
        }
    }
}

void User::HandleSetActiveMoverOpcode(WDataStore& recvData)
{
    LOG_DEBUG("network", "WORLD: Recvd CMSG_SET_ACTIVE_MOVER");

    WOWGUID guid;
    recvData >> guid;

    if (GetPlayer()->IsInWorld() && m_player->m_mover && m_player->m_mover->IsInWorld())
    {
        if (m_player->m_mover->GetGUID() != guid)
            LOG_ERROR("network.opcode", "HandleSetActiveMoverOpcode: incorrect mover guid: mover is {} and should be {}",
                guid.ToString(), m_player->m_mover->GetGUID().ToString());
    }
}

void User::HandleMoveNotActiveMover(WDataStore& recvData)
{
    LOG_DEBUG("network", "WORLD: Recvd CMSG_MOVE_NOT_ACTIVE_MOVER");

    WOWGUID old_mover_guid;
    recvData >> old_mover_guid.ReadAsPacked();

    // pussywizard: typical check for incomming movement packets
    if (!m_player->m_mover || !m_player->m_mover->IsInWorld() || m_player->m_mover->IsDuringRemoveFromWorld() || old_mover_guid != m_player->m_mover->GetGUID())
    {
        recvData.rfinish(); // prevent warnings spam
        return;
    }

    CMovement mi;
    mi.guid = old_mover_guid;
    ReadMovementInfo(recvData, &mi);

    m_player->m_mover->m_movement = mi;
}

void User::HandleMountSpecialAnimOpcode(WDataStore& /*recvData*/)
{
    WDataStore data(SMSG_MOUNTSPECIAL_ANIM, 8);
    data << GetPlayer()->GetGUID();

    GetPlayer()->SendMessageToSet(&data, false);
}

void User::HandleMoveKnockBackAck(WDataStore& recvData)
{
    LOG_DEBUG("network", "CMSG_MOVE_KNOCK_BACK_ACK");

    WOWGUID guid;
    recvData >> guid.ReadAsPacked();

    // pussywizard: typical check for incomming movement packets
    if (!m_player->m_mover || !m_player->m_mover->IsInWorld() || m_player->m_mover->IsDuringRemoveFromWorld() || guid != m_player->m_mover->GetGUID())
    {
        recvData.rfinish(); // prevent warnings spam
        return;
    }

    recvData.read_skip<uint32>();                          // unk

    CMovement movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    m_player->m_mover->m_movement = movementInfo;

    WDataStore data(MSG_MOVE_KNOCK_BACK, 66);
    data << guid.WriteAsPacked();
    m_player->m_mover->BuildMovementPacket(&data);
    m_player->SetCanTeleport(true);
    // knockback specific info
    data << movementInfo.jump.sinAngle;
    data << movementInfo.jump.cosAngle;
    data << movementInfo.jump.xyspeed;
    data << movementInfo.jump.zspeed;

    m_player->SendMessageToSet(&data, false);
}

void User::HandleMoveHoverAck(WDataStore& recvData)
{
    LOG_DEBUG("network", "CMSG_MOVE_HOVER_ACK");

    WOWGUID guid;
    recvData >> guid.ReadAsPacked();

    recvData.read_skip<uint32>();                          // unk

    CMovement movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                          // unk2
}

void User::HandleMoveWaterWalkAck(WDataStore& recvData)
{
    LOG_DEBUG("network", "CMSG_MOVE_WATER_WALK_ACK");

    WOWGUID guid;
    recvData >> guid.ReadAsPacked();

    recvData.read_skip<uint32>();                          // unk

    CMovement movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                          // unk2
}

void User::HandleSummonResponseOpcode(WDataStore& recvData)
{
    if (!m_player->IsAlive() || m_player->IsInCombat())
        return;

    WOWGUID summoner_guid;
    bool agree;
    recvData >> summoner_guid;
    recvData >> agree;

    if (agree && m_player->IsSummonAsSpectator())
    {
        ChatHandler chc(this);
        if (Player* summoner = ObjectAccessor::FindPlayer(summoner_guid))
            ArenaSpectator::HandleSpectatorSpectateCommand(&chc, summoner->GetName().c_str());
        else
            chc.PSendSysMessage("Requested player not found.");

        agree = false;
    }
    m_player->SetSummonAsSpectator(false);
    m_player->SummonIfPossible(agree, summoner_guid);
}

void User::HandleMoveTimeSkippedOpcode(WDataStore& recvData)
{
    LOG_DEBUG("network", "WORLD: Recvd CMSG_MOVE_TIME_SKIPPED");

    WOWGUID guid;
    uint32 timeSkipped;
    recvData >> guid.ReadAsPacked();
    recvData >> timeSkipped;

    Unit* mover = GetPlayer()->m_mover;

    if (!mover)
    {
        LOG_ERROR("network.opcode", "User::HandleMoveTimeSkippedOpcode wrong mover state from the unit moved by the player [{}]", GetPlayer()->GetGUID().ToString());
        return;
    }

    // prevent tampered movement data
    if (guid != mover->GetGUID())
    {
        LOG_ERROR("network.opcode", "User::HandleMoveTimeSkippedOpcode wrong guid from the unit moved by the player [{}]", GetPlayer()->GetGUID().ToString());
        return;
    }

    mover->m_movement.time += timeSkipped;

    WDataStore data(MSG_MOVE_TIME_SKIPPED, recvData.size());
    data << guid.WriteAsPacked();
    data << timeSkipped;
    GetPlayer()->SendMessageToSet(&data, false);
}

void User::HandleTimeSyncResp(WDataStore& recvData)
{
    LOG_DEBUG("network", "CMSG_TIME_SYNC_RESP");

    uint32 counter, clientTimestamp;
    recvData >> counter >> clientTimestamp;

    if (_pendingTimeSyncRequests.count(counter) == 0)
        return;

    uint32 serverTimeAtSent = _pendingTimeSyncRequests.at(counter);
    _pendingTimeSyncRequests.erase(counter);

    // time it took for the request to travel to the client, for the client to process it and reply and for response to travel back to the server.
    // we are going to make 2 assumptions:
    // 1) we assume that the request processing time equals 0.
    // 2) we assume that the packet took as much time to travel from server to client than it took to travel from client to server.
    uint32 roundTripDuration = getMSTimeDiff(serverTimeAtSent, recvData.GetReceivedTime());
    uint32 lagDelay = roundTripDuration / 2;

    // clockDelta = serverTime - clientTime
    // where
    // serverTime: time that was displayed on the clock of the SERVER at the moment when the client processed the SMSG_TIME_SYNC_REQUEST packet.
    // clientTime:  time that was displayed on the clock of the CLIENT at the moment when the client processed the SMSG_TIME_SYNC_REQUEST packet.

    // Once clockDelta has been computed, we can compute the time of an event on server clock when we know the time of that same event on the client clock,
    // using the following relation:
    // serverTime = clockDelta + clientTime

    int64 clockDelta = (int64)serverTimeAtSent + (int64)lagDelay - (int64)clientTimestamp;
    _timeSyncClockDeltaQueue.put(std::pair<int64, uint32>(clockDelta, roundTripDuration));
    ComputeNewClockDelta();
}

void User::ComputeNewClockDelta()
{
    // implementation of the technique described here: https://web.archive.org/web/20180430214420/http://www.mine-control.com/zack/timesync/timesync.html
    // to reduce the skew induced by dropped TCP packets that get resent.

    std::vector<uint32> latencies;
    std::vector<int64> clockDeltasAfterFiltering;

    for (auto& pair : _timeSyncClockDeltaQueue.content())
        latencies.push_back(pair.second);

    uint32 latencyMedian = median(latencies);
    uint32 latencyStandardDeviation = standard_deviation(latencies);

    uint32 sampleSizeAfterFiltering = 0;
    for (auto& pair : _timeSyncClockDeltaQueue.content())
    {
        if (pair.second <= latencyMedian + latencyStandardDeviation) {
            clockDeltasAfterFiltering.push_back(pair.first);
            sampleSizeAfterFiltering++;
        }
    }

    if (sampleSizeAfterFiltering != 0)
    {
        int64 meanClockDelta = static_cast<int64>(mean(clockDeltasAfterFiltering));
        if (std::abs(meanClockDelta - _timeSyncClockDelta) > 25)
            _timeSyncClockDelta = meanClockDelta;
    }
    else if (_timeSyncClockDelta == 0)
    {
        std::pair<int64, uint32> back = _timeSyncClockDeltaQueue.peak_back();
        _timeSyncClockDelta = back.first;
    }
}

void User::HandleMoveRootAck(WDataStore& recvData)
{
    WOWGUID guid;
    recvData >> guid.ReadAsPacked();

    Unit* mover = m_player->m_mover;
    if (!mover || guid != mover->GetGUID())
    {
        recvData.rfinish(); // prevent warnings spam
        return;
    }

    uint32 movementCounter;
    recvData >> movementCounter;

    CMovement movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

     /* process position-change */
    int64 movementTime = (int64) movementInfo.time + _timeSyncClockDelta;
    if (_timeSyncClockDelta == 0 || movementTime < 0 || movementTime > 0xFFFFFFFF)
    {
        LOG_INFO("misc", "The computed movement time using clockDelta is erronous. Using fallback instead");
        movementInfo.time = getMSTime();
    }
    else
    {
        movementInfo.time = (uint32)movementTime;
    }

    movementInfo.guid = mover->GetGUID();
    mover->m_movement = movementInfo;
    mover->UpdatePosition(movementInfo.pos);

    WDataStore data(MSG_MOVE_ROOT, 64);
    WriteMovementInfo(&data, &movementInfo);
    mover->SendMessageToSet(&data, m_player);
}

void User::HandleMoveUnRootAck(WDataStore& recvData)
{
    WOWGUID guid;
    recvData >> guid.ReadAsPacked();

    Unit* mover = m_player->m_mover;
    if (!mover || guid != mover->GetGUID())
    {
        recvData.rfinish(); // prevent warnings spam
        return;
    }

    uint32 movementCounter;
    recvData >> movementCounter;

    CMovement movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

     /* process position-change */
    int64 movementTime = (int64) movementInfo.time + _timeSyncClockDelta;
    if (_timeSyncClockDelta == 0 || movementTime < 0 || movementTime > 0xFFFFFFFF)
    {
        LOG_INFO("misc", "The computed movement time using clockDelta is erronous. Using fallback instead");
        movementInfo.time = getMSTime();
    }
    else
    {
        movementInfo.time = (uint32)movementTime;
    }

    if (G3D::fuzzyEq(movementInfo.fallTime, 0.f))
    {
        movementInfo.m_moveFlags &= ~MOVEFLAG_FALLING;
    }

    movementInfo.guid = mover->GetGUID();
    mover->m_movement = movementInfo;
    mover->UpdatePosition(movementInfo.pos);

    WDataStore data(MSG_MOVE_UNROOT, 64);
    WriteMovementInfo(&data, &movementInfo);
    mover->SendMessageToSet(&data, m_player);
}
