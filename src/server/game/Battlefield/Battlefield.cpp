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
#include "CellImpl.h"
#include "CreatureTextMgr.h"
#include "GameGraveyard.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Map.h"
#include "MapMgr.h"
#include "MiscPackets.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Transport.h"
#include "WDataStore.h"

/// @todo: this import is not necessary for compilation and marked as unused by the IDE
//  however, for some reasons removing it would cause a damn linking issue
//  there is probably some underlying problem with imports which should properly addressed
//  see: https://github.com/azerothcore/azerothcore-wotlk/issues/9766
#include "GridNotifiersImpl.h"

Battlefield::Battlefield()
{
    m_Timer = 0;
    m_IsEnabled = true;
    m_isActive = false;
    m_DefenderTeam = TEAM_NEUTRAL;

    m_TypeId = 0;
    m_BattleId = 0;
    m_ZoneId = 0;
    m_Map = nullptr;
    m_MapId = 0;
    m_MaxPlayer = 0;
    m_MinPlayer = 0;
    m_MinLevel = 0;
    m_BattleTime = 0;
    m_NoWarBattleTime = 0;
    m_RestartAfterCrash = 0;
    m_TimeForAcceptInvite = 20;
    m_uiKickDontAcceptTimer = 1000;

    m_uiKickAfkPlayersTimer = 1000;

    m_LastResurectTimer = RESURRECTION_INTERVAL;
    m_StartGroupingTimer = 0;
    m_StartGrouping = false;
}

Battlefield::~Battlefield()
{
    for (BfCapturePointVector::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        delete *itr;

    for (GraveyardVect::const_iterator itr = m_GraveyardList.begin(); itr != m_GraveyardList.end(); ++itr)
        delete *itr;

    m_capturePoints.clear();
}

// Called when a player enters the zone
void Battlefield::HandlePlayerEnterZone(Player* player, uint32 /*zone*/)
{
    // Xinef: do not invite players on taxi
    if (!player->IsOnTaxi())
    {
        // If battle is started,
        // If not full of players > invite player to join the war
        // If full of players > announce to player that BF is full and kick him after a few second if he desn't leave
        if (IsWarTime())
        {
            if (m_PlayersInWar[player->GetTeamId()].size() + m_InvitedPlayers[player->GetTeamId()].size() < m_MaxPlayer) // Vacant spaces
                InvitePlayerToWar(player);
            else // No more vacant places
            {
                /// @todo: Send a packet to announce it to player
                m_PlayersWillBeKick[player->GetTeamId()][player->GetGUID()] = GameTime::GetGameTime().count() + (player->IsGameMaster() ? 30 * MINUTE : 10);
                InvitePlayerToQueue(player);
            }
        }
        else
        {
            // If time left is < 15 minutes invite player to join queue
            if (m_Timer <= m_StartGroupingTimer)
                InvitePlayerToQueue(player);
        }
    }

    // Add player in the list of player in zone
    m_players[player->GetTeamId()].insert(player->GetGUID());
    OnPlayerEnterZone(player);
}

// Called when a player leave the zone
void Battlefield::HandlePlayerLeaveZone(Player* player, uint32 /*zone*/)
{
    if (IsWarTime())
    {
        // If the player is participating to the battle
        if (m_PlayersInWar[player->GetTeamId()].find(player->GetGUID()) != m_PlayersInWar[player->GetTeamId()].end())
        {
            m_PlayersInWar[player->GetTeamId()].erase(player->GetGUID());
            player->User()->SendBfLeaveMessage(m_BattleId);
            if (Group* group = player->GetGroup()) // Remove the player from the raid group
                if (group->isBFGroup())
                    group->RemoveMember(player->GetGUID());

            OnPlayerLeaveWar(player);
        }
    }

    for (BfCapturePointVector::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        (*itr)->HandlePlayerLeave(player);

    m_InvitedPlayers[player->GetTeamId()].erase(player->GetGUID());
    m_PlayersWillBeKick[player->GetTeamId()].erase(player->GetGUID());
    m_players[player->GetTeamId()].erase(player->GetGUID());
    SendRemoveWorldStates(player);
    RemovePlayerFromResurrectQueue(player->GetGUID());
    OnPlayerLeaveZone(player);
}

bool Battlefield::Update(uint32 diff)
{
    if (m_Timer <= diff)
    {
        if (!IsEnabled() || (!IsWarTime() && sWorld->GetActiveSessionCount() > 3500)) // if WG is disabled or there is more than 3500 connections, switch automaticly
        {
            m_isActive = true;
            EndBattle(false);
            return false;
        }
        // Battlefield ends on time
        if (IsWarTime())
            EndBattle(true);
        else // Time to start a new battle!
            StartBattle();
    }
    else
        m_Timer -= diff;

    if (!IsEnabled())
        return false;

    // Invite players a few minutes before the battle's beginning
    if (!IsWarTime() && !m_StartGrouping && m_Timer <= m_StartGroupingTimer)
    {
        m_StartGrouping = true;
        InvitePlayersInZoneToQueue();
        OnStartGrouping();
    }

    bool objective_changed = false;
    if (IsWarTime())
    {
        if (m_uiKickAfkPlayersTimer <= diff)
        {
            m_uiKickAfkPlayersTimer = 20000;
            KickAfkPlayers();
        }
        else
            m_uiKickAfkPlayersTimer -= diff;

        // Kick players who chose not to accept invitation to the battle
        if (m_uiKickDontAcceptTimer <= diff)
        {
            time_t now = GameTime::GetGameTime().count();
            for (int team = 0; team < 2; team++)
                for (PlayerTimerMap::iterator itr = m_InvitedPlayers[team].begin(); itr != m_InvitedPlayers[team].end(); ++itr)
                    if (itr->second <= now)
                        KickPlayerFromBattlefield(itr->first);

            InvitePlayersInZoneToWar();
            for (int team = 0; team < 2; team++)
                for (PlayerTimerMap::iterator itr = m_PlayersWillBeKick[team].begin(); itr != m_PlayersWillBeKick[team].end(); ++itr)
                    if (itr->second <= now)
                        KickPlayerFromBattlefield(itr->first);

            m_uiKickDontAcceptTimer = 5000;
        }
        else
            m_uiKickDontAcceptTimer -= diff;

        for (BfCapturePointVector::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
            if ((*itr)->Update(diff))
                objective_changed = true;
    }

    if (m_LastResurectTimer <= diff)
    {
        for (uint8 i = 0; i < m_GraveyardList.size(); i++)
            if (GetGraveyardById(i))
                m_GraveyardList[i]->Resurrect();
        m_LastResurectTimer = RESURRECTION_INTERVAL;
    }
    else
        m_LastResurectTimer -= diff;

    return objective_changed;
}

void Battlefield::InvitePlayersInZoneToQueue()
{
    for (uint8 team = 0; team < 2; ++team)
        for (GuidUnorderedSet::const_iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                InvitePlayerToQueue(player);
}

void Battlefield::InvitePlayerToQueue(Player* player)
{
    if (m_PlayersInQueue[player->GetTeamId()].count(player->GetGUID()))
        return;

    if (m_PlayersInQueue[player->GetTeamId()].size() <= m_MinPlayer || m_PlayersInQueue[GetOtherTeam(player->GetTeamId())].size() >= m_MinPlayer)
        player->User()->SendBfInvitePlayerToQueue(m_BattleId);
}

void Battlefield::InvitePlayersInQueueToWar()
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        GuidUnorderedSet copy(m_PlayersInQueue[team]);
        for (GuidUnorderedSet::const_iterator itr = copy.begin(); itr != copy.end(); ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                if (m_PlayersInWar[player->GetTeamId()].size() + m_InvitedPlayers[player->GetTeamId()].size() < m_MaxPlayer)
                    InvitePlayerToWar(player);
                else
                {
                    //Full
                }
            }
        }
        m_PlayersInQueue[team].clear();
    }
}

void Battlefield::InvitePlayersInZoneToWar()
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (GuidUnorderedSet::const_iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                if (m_PlayersInWar[player->GetTeamId()].count(player->GetGUID()) || m_InvitedPlayers[player->GetTeamId()].count(player->GetGUID()))
                    continue;
                if (m_PlayersInWar[player->GetTeamId()].size() + m_InvitedPlayers[player->GetTeamId()].size() < m_MaxPlayer)
                    InvitePlayerToWar(player);
                else if (m_PlayersWillBeKick[player->GetTeamId()].count(player->GetGUID()) == 0)// Battlefield is full of players
                    m_PlayersWillBeKick[player->GetTeamId()][player->GetGUID()] = GameTime::GetGameTime().count() + 10;
            }
        }
}

void Battlefield::InvitePlayerToWar(Player* player)
{
    if (!player)
        return;

    /// @todo : needed ?
    if (player->IsOnTaxi())
        return;

    if (player->InBattleground())
    {
        m_PlayersInQueue[player->GetTeamId()].erase(player->GetGUID());
        return;
    }

    // If the player does not match minimal level requirements for the battlefield, kick him
    if (player->GetLevel() < m_MinLevel)
    {
        if (m_PlayersWillBeKick[player->GetTeamId()].count(player->GetGUID()) == 0)
            m_PlayersWillBeKick[player->GetTeamId()][player->GetGUID()] = GameTime::GetGameTime().count() + 10;
        return;
    }

    // Check if player is not already in war
    if (m_PlayersInWar[player->GetTeamId()].count(player->GetGUID()) || m_InvitedPlayers[player->GetTeamId()].count(player->GetGUID()))
        return;

    m_PlayersWillBeKick[player->GetTeamId()].erase(player->GetGUID());
    m_InvitedPlayers[player->GetTeamId()][player->GetGUID()] = GameTime::GetGameTime().count() + m_TimeForAcceptInvite;
    player->User()->SendBfInvitePlayerToWar(m_BattleId, m_ZoneId, m_TimeForAcceptInvite);
}

void Battlefield::InitStalker(uint32 entry, float x, float y, float z, float o)
{
    if (Creature* creature = SpawnCreature(entry, x, y, z, o, TEAM_NEUTRAL))
        StalkerGuid = creature->GetGUID();
    else
        LOG_ERROR("bg.battlefield", "Battlefield::InitStalker: could not spawn Stalker (Creature entry {}), zone messeges will be un-available", entry);
}

void Battlefield::KickAfkPlayers()
{
    // xinef: optimization, dont lookup player twice
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (GuidUnorderedSet::const_iterator itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                if (player->isAFK() && player->GetZoneId() == GetZoneId() && !player->IsGameMaster())
                    player->Teleport(KickPosition);
}

void Battlefield::KickPlayerFromBattlefield(WOWGUID guid)
{
    if (Player* player = ObjectAccessor::FindPlayer(guid))
    {
        if (player->GetZoneId() == GetZoneId() && !player->IsGameMaster())
            player->Teleport(KickPosition);
    }
}

void Battlefield::StartBattle()
{
    if (m_isActive)
        return;

    for (int team = 0; team < PVP_TEAMS_COUNT; team++)
    {
        m_PlayersInWar[team].clear();
        m_Groups[team].clear();
    }

    m_Timer = m_BattleTime;
    m_isActive = true;

    InvitePlayersInZoneToWar();
    InvitePlayersInQueueToWar();

    DoPlaySoundToAll(BF_START);

    OnBattleStart();
}

void Battlefield::EndBattle(bool endByTimer)
{
    if (!m_isActive)
        return;

    m_isActive = false;

    m_StartGrouping = false;

    if (!endByTimer)
        SetDefenderTeam(GetAttackerTeam());

    if (GetDefenderTeam() == TEAM_ALLIANCE)
        DoPlaySoundToAll(BF_ALLIANCE_WINS);
    else
        DoPlaySoundToAll(BF_HORDE_WINS);

    OnBattleEnd(endByTimer);

    // Reset battlefield timer
    m_Timer = m_NoWarBattleTime;
    SendInitWorldStatesToAll();
}

void Battlefield::DoPlaySoundToAll(uint32 SoundID)
{
    BroadcastPacketToWar(WorldPackets::Misc::Playsound(SoundID).Write());
}

bool Battlefield::HasPlayer(Player* player) const
{
    return m_players[player->GetTeamId()].find(player->GetGUID()) != m_players[player->GetTeamId()].end();
}

// Called in User::HandleBfQueueInviteResponse
void Battlefield::PlayerAcceptInviteToQueue(Player* player)
{
    // Add player in queue
    m_PlayersInQueue[player->GetTeamId()].insert(player->GetGUID());
    // Send notification
    player->User()->SendBfQueueInviteResponse(m_BattleId, m_ZoneId);
}

// Called in User::HandleBfExitRequest
void Battlefield::AskToLeaveQueue(Player* player)
{
    // Remove player from queue
    m_PlayersInQueue[player->GetTeamId()].erase(player->GetGUID());
}

// Called in User::HandleHearthAndResurrect
void Battlefield::PlayerAskToLeave(Player* player)
{
    // Player leaving Wintergrasp, trigger Hearthstone spell.
    player->CastSpell(player, 8690, true);
}

// Called in User::HandleBfEntryInviteResponse
void Battlefield::PlayerAcceptInviteToWar(Player* player)
{
    if (!IsWarTime())
        return;

    if (AddOrSetPlayerToCorrectBfGroup(player))
    {
        player->User()->SendBfEntered(m_BattleId);
        m_PlayersInWar[player->GetTeamId()].insert(player->GetGUID());
        m_InvitedPlayers[player->GetTeamId()].erase(player->GetGUID());

        if (player->isAFK())
            player->ToggleAFK();

        OnPlayerJoinWar(player);                               //for scripting
    }
}

void Battlefield::TeamCastSpell(TeamId team, int32 spellId)
{
    if (spellId > 0)
    {
        for (GuidUnorderedSet::const_iterator itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->CastSpell(player, uint32(spellId), true);
    }
    else
    {
        for (GuidUnorderedSet::const_iterator itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->RemoveAuraFromStack(uint32(-spellId));
    }
}

void Battlefield::BroadcastPacketToZone(WDataStore const* data) const
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (GuidUnorderedSet::const_iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->User()->Send(data);
}

void Battlefield::BroadcastPacketToQueue(WDataStore const* data) const
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (GuidUnorderedSet::const_iterator itr = m_PlayersInQueue[team].begin(); itr != m_PlayersInQueue[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->User()->Send(data);
}

void Battlefield::BroadcastPacketToWar(WDataStore const* data) const
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (GuidUnorderedSet::const_iterator itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->User()->Send(data);
}

void Battlefield::SendWarning(uint8 id, WorldObject const* target /*= nullptr*/)
{
    if (Creature* stalker = GetCreature(StalkerGuid))
        sCreatureTextMgr->SendChat(stalker, id, target);
}

void Battlefield::SendUpdateWorldState(uint32 field, uint32 value)
{
    for (uint8 i = 0; i < PVP_TEAMS_COUNT; ++i)
        for (GuidUnorderedSet::iterator itr = m_players[i].begin(); itr != m_players[i].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->SendUpdateWorldState(field, value);
}

void Battlefield::RegisterZone(uint32 zoneId)
{
    sBattlefieldMgr->AddZone(zoneId, this);
}

void Battlefield::HideNpc(Creature* creature)
{
    creature->CombatStop();
    creature->SetReactState(REACT_PASSIVE);
    creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
    creature->SetPhaseMask(2, false); // pussywizard: false because UpdateObjectVisibility(true) is called below in SetVisible(), no need to have it here
    creature->DisappearAndDie();
    creature->SetVisible(false);
}

void Battlefield::ShowNpc(Creature* creature, bool aggressive)
{
    creature->SetPhaseMask(1, false); // pussywizard: false because UpdateObjectVisibility(true) is called below in SetVisible(), no need to have it here
    creature->SetVisible(true);
    creature->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
    if (!creature->IsAlive())
        creature->Respawn(true);
    if (aggressive)
        creature->SetReactState(REACT_AGGRESSIVE);
    else
    {
        creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        creature->SetReactState(REACT_PASSIVE);
    }
}

// ****************************************************
// ******************* Group System *******************
// ****************************************************
Group* Battlefield::GetFreeBfRaid(TeamId TeamId)
{
    for (GuidUnorderedSet::const_iterator itr = m_Groups[TeamId].begin(); itr != m_Groups[TeamId].end(); ++itr)
        if (Group* group = sGroupMgr->GetGroupByGUID(itr->GetCounter()))
            if (!group->IsFull())
                return group;

    return nullptr;
}

Group* Battlefield::GetGroupPlayer(WOWGUID guid, TeamId TeamId)
{
    for (GuidUnorderedSet::const_iterator itr = m_Groups[TeamId].begin(); itr != m_Groups[TeamId].end(); ++itr)
        if (Group* group = sGroupMgr->GetGroupByGUID(itr->GetCounter()))
            if (group->IsMember(guid))
                return group;

    return nullptr;
}

bool Battlefield::AddOrSetPlayerToCorrectBfGroup(Player* player)
{
    if (!player->IsInWorld())
        return false;

    if (player->GetGroup() && (player->GetGroup()->isBGGroup() || player->GetGroup()->isBFGroup()))
    {
        LOG_INFO("misc", "Battlefield::AddOrSetPlayerToCorrectBfGroup - player is already in {} group!", (player->GetGroup()->isBGGroup() ? "BG" : "BF"));
        return false;
    }

    Group* group = GetFreeBfRaid(player->GetTeamId());
    if (!group)
    {
        group = new Group;
        group->SetBattlefieldGroup(this);
        group->Create(player);
        sGroupMgr->AddGroup(group);
        m_Groups[player->GetTeamId()].insert(group->GetGUID());
    }
    else if (group->IsMember(player->GetGUID()))
    {
        uint8 subgroup = group->GetMemberGroup(player->GetGUID());
        player->SetBattlegroundOrBattlefieldRaid(group, subgroup);
    }
    else
        group->AddMember(player);

    return true;
}

//***************End of Group System*******************

//*****************************************************
//***************Spirit Guide System*******************
//*****************************************************

//--------------------
//-Battlefield Method-
//--------------------
BfGraveyard* Battlefield::GetGraveyardById(uint32 id) const
{
    if (id < m_GraveyardList.size())
    {
        if (m_GraveyardList[id])
            return m_GraveyardList[id];
        else
            LOG_ERROR("bg.battlefield", "Battlefield::GetGraveyardById Id:{} not existed", id);
    }
    else
        LOG_ERROR("bg.battlefield", "Battlefield::GetGraveyardById Id:{} cant be found", id);

    return nullptr;
}

GraveyardStruct const* Battlefield::GetClosestGraveyard(Player* player)
{
    BfGraveyard* closestGY = nullptr;
    float maxdist = -1;
    for (uint8 i = 0; i < m_GraveyardList.size(); i++)
    {
        if (m_GraveyardList[i])
        {
            if (m_GraveyardList[i]->GetControlTeamId() != player->GetTeamId())
                continue;

            float dist = m_GraveyardList[i]->GetDistance(player);
            if (dist < maxdist || maxdist < 0)
            {
                closestGY = m_GraveyardList[i];
                maxdist = dist;
            }
        }
    }

    if (closestGY)
        return sGraveyard->GetGraveyard(closestGY->GetGraveyardId());

    return nullptr;
}

void Battlefield::AddPlayerToResurrectQueue(WOWGUID npcGuid, WOWGUID playerGuid)
{
    for (uint8 i = 0; i < m_GraveyardList.size(); i++)
    {
        if (!m_GraveyardList[i])
            continue;

        if (m_GraveyardList[i]->HasNpc(npcGuid))
        {
            m_GraveyardList[i]->AddPlayer(playerGuid);
            break;
        }
    }
}

void Battlefield::RemovePlayerFromResurrectQueue(WOWGUID playerGuid)
{
    for (uint8 i = 0; i < m_GraveyardList.size(); i++)
    {
        if (!m_GraveyardList[i])
            continue;

        if (m_GraveyardList[i]->HasPlayer(playerGuid))
        {
            m_GraveyardList[i]->RemovePlayer(playerGuid);
            break;
        }
    }
}

void Battlefield::SendAreaSpiritHealerQueryOpcode(Player* player, const WOWGUID& guid)
{
    WDataStore data(SMSG_AREA_SPIRIT_HEALER_TIME, 12);
    uint32 time = m_LastResurectTimer;  // resurrect every 30 seconds

    data << guid << time;
    ASSERT(player);
    player->User()->Send(&data);
}

// ----------------------
// - BfGraveyard Method -
// ----------------------
BfGraveyard::BfGraveyard(Battlefield* battlefield)
{
    m_Bf = battlefield;
    m_GraveyardId = 0;
    m_ControlTeam = TEAM_NEUTRAL;
    m_ResurrectQueue.clear();
}

void BfGraveyard::Initialize(TeamId startControl, uint32 graveyardId)
{
    m_ControlTeam = startControl;
    m_GraveyardId = graveyardId;
}

void BfGraveyard::SetSpirit(Creature* spirit, TeamId team)
{
    if (!spirit)
    {
        LOG_ERROR("bg.battlefield", "BfGraveyard::SetSpirit: Invalid Spirit.");
        return;
    }

    m_SpiritGuide[team] = spirit->GetGUID();
    spirit->SetReactState(REACT_PASSIVE);
}

float BfGraveyard::GetDistance(Player* player)
{
    const GraveyardStruct* safeLoc = sGraveyard->GetGraveyard(m_GraveyardId);
    return player->GetDistance2d(safeLoc->x, safeLoc->y);
}

void BfGraveyard::AddPlayer(WOWGUID playerGuid)
{
    if (!m_ResurrectQueue.count(playerGuid))
    {
        m_ResurrectQueue.insert(playerGuid);

        if (Player* player = ObjectAccessor::FindPlayer(playerGuid))
            player->CastSpell(player, SPELL_WAITING_FOR_RESURRECT, true);
    }
}

void BfGraveyard::RemovePlayer(WOWGUID playerGuid)
{
    m_ResurrectQueue.erase(m_ResurrectQueue.find(playerGuid));

    if (Player* player = ObjectAccessor::FindPlayer(playerGuid))
        player->RemoveAurasDueToSpell(SPELL_WAITING_FOR_RESURRECT);
}

void BfGraveyard::Resurrect()
{
    if (m_ResurrectQueue.empty())
        return;

    for (GuidUnorderedSet::const_iterator itr = m_ResurrectQueue.begin(); itr != m_ResurrectQueue.end(); ++itr)
    {
        // Get player object from his guid
        Player* player = ObjectAccessor::FindPlayer(*itr);
        if (!player)
            continue;

        // Check  if the player is in world and on the good graveyard
        if (player->IsInWorld())
            if (Unit* spirit = ObjectAccessor::GetCreature(*player, m_SpiritGuide[m_ControlTeam]))
                spirit->CastSpell(spirit, SPELL_SPIRIT_HEAL, true);

        // Resurect player
        player->CastSpell(player, SPELL_RESURRECTION_VISUAL, true);
        player->Resurrect(1.0f);
        player->CastSpell(player, 6962, true);
        player->CastSpell(player, SPELL_SPIRIT_HEAL_MANA, true);

        player->SpawnCorpseBones(false);
    }

    m_ResurrectQueue.clear();
}

// For changing graveyard control
void BfGraveyard::GiveControlTo(TeamId team)
{
    m_ControlTeam = team;
    // Teleport to other graveyard, player witch were on this graveyard
    RelocateDeadPlayers();
}

void BfGraveyard::RelocateDeadPlayers()
{
    GraveyardStruct const* closestGrave = nullptr;
    for (GuidUnorderedSet::const_iterator itr = m_ResurrectQueue.begin(); itr != m_ResurrectQueue.end(); ++itr)
    {
        Player* player = ObjectAccessor::FindPlayer(*itr);
        if (!player)
            continue;

        if (closestGrave)
            player->Teleport(player->GetMapId(), closestGrave->x, closestGrave->y, closestGrave->z, player->GetOrientation());
        else
        {
            closestGrave = m_Bf->GetClosestGraveyard(player);
            if (closestGrave)
                player->Teleport(player->GetMapId(), closestGrave->x, closestGrave->y, closestGrave->z, player->GetOrientation());
        }
    }
}

// *******************************************************
// *************** End Spirit Guide system ***************
// *******************************************************
// ********************** Misc ***************************
// *******************************************************

Creature* Battlefield::SpawnCreature(uint32 entry, Position pos, TeamId teamId)
{
    return SpawnCreature(entry, pos.m_positionX, pos.m_positionY, pos.m_positionZ, pos.m_orientation, teamId);
}

Creature* Battlefield::SpawnCreature(uint32 entry, float x, float y, float z, float o, TeamId teamId)
{
    //Get map object
    Map* map = sMapMgr->CreateBaseMap(m_MapId);
    if (!map)
    {
        LOG_ERROR("bg.battlefield", "Battlefield::SpawnCreature: Can't create creature entry: {} map not found", entry);
        return nullptr;
    }

    CreatureTemplate const* cinfo = sObjectMgr->GetCreatureTemplate(entry);
    if (!cinfo)
    {
        LOG_ERROR("sql.sql", "Battlefield::SpawnCreature: entry {} does not exist.", entry);
        return nullptr;
    }

    Creature* creature = new Creature(true);
    if (!creature->Create(map->GenerateLowGuid<HighGuid::Unit>(), map, PHASEMASK_NORMAL, entry, 0, x, y, z, o))
    {
        LOG_ERROR("bg.battlefield", "Battlefield::SpawnCreature: Can't create creature entry: {}", entry);
        delete creature;
        return nullptr;
    }

    creature->SetFaction(BattlefieldFactions[teamId]);
    creature->SetHomePosition(x, y, z, o);

    // force using DB speeds -- do we really need this?
    creature->SetSpeed(MOVE_WALK, cinfo->speed_walk);
    creature->SetSpeed(MOVE_RUN, cinfo->speed_run);

    // Set creature in world
    map->AddToMap(creature);
    creature->setActive(true);

    return creature;
}

// Method for spawning gameobject on map
GameObject* Battlefield::SpawnGameObject(uint32 entry, float x, float y, float z, float o)
{
    // Get map object
    Map* map = sMapMgr->CreateBaseMap(m_MapId);
    if (!map)
        return 0;

    // Create gameobject
    GameObject* go = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
    if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), entry, map, PHASEMASK_NORMAL, x, y, z, o, G3D::Quat(), 100, GO_STATE_READY))
    {
        LOG_ERROR("sql.sql", "Battlefield::SpawnGameObject: Gameobject template {} not found in database! Battlefield not created!", entry);
        LOG_ERROR("bg.battlefield", "Battlefield::SpawnGameObject: Cannot create gameobject template {}! Battlefield not created!", entry);
        delete go;
        return nullptr;
    }

    // Add to world
    map->AddToMap(go);
    go->setActive(true);

    return go;
}

Creature* Battlefield::GetCreature(WOWGUID const guid)
{
    if (!m_Map)
        return nullptr;

    return m_Map->GetCreature(guid);
}

GameObject* Battlefield::GetGameObject(WOWGUID const guid)
{
    if (!m_Map)
        return nullptr;

    return m_Map->GetGameObject(guid);
}

// *******************************************************
// ******************* CapturePoint **********************
// *******************************************************

BfCapturePoint::BfCapturePoint(Battlefield* battlefield) : m_Bf(battlefield)
{
    m_team = TEAM_NEUTRAL;
    m_value = 0;
    m_minValue = 0.0f;
    m_maxValue = 0.0f;
    m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL;
    m_OldState = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL;
    m_capturePointEntry = 0;
    m_neutralValuePct = 0;
    m_maxSpeed = 0;
}

bool BfCapturePoint::HandlePlayerEnter(Player* player)
{
    if (GameObject* go = GetCapturePointGo(player))
    {
        player->SendUpdateWorldState(go->GetGOInfo()->capturePoint.worldState1, 1);
        player->SendUpdateWorldState(go->GetGOInfo()->capturePoint.worldstate2, uint32(std::ceil((m_value + m_maxValue) / (2 * m_maxValue) * 100.0f)));
        player->SendUpdateWorldState(go->GetGOInfo()->capturePoint.worldstate3, m_neutralValuePct);
    }
    return m_activePlayers[player->GetTeamId()].insert(player->GetGUID()).second;
}

GuidUnorderedSet::iterator BfCapturePoint::HandlePlayerLeave(Player* player)
{
    if (GameObject* go = GetCapturePointGo(player))
        player->SendUpdateWorldState(go->GetGOInfo()->capturePoint.worldState1, 0);

    GuidUnorderedSet::iterator current = m_activePlayers[player->GetTeamId()].find(player->GetGUID());

    if (current == m_activePlayers[player->GetTeamId()].end())
        return current; // return end()

    current = m_activePlayers[player->GetTeamId()].erase(current);
    return current;
}

void BfCapturePoint::SendChangePhase()
{
    GameObject* capturePoint = GetCapturePointGo();
    if (!capturePoint)
        return;

    for (uint8 team = 0; team < 2; ++team)
        for (GuidUnorderedSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)  // send to all players present in the area
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                // send this too, sometimes the slider disappears, dunno why :(
                player->SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldState1, 1);
                // send these updates to only the ones in this objective
                player->SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldstate2, (uint32) std::ceil((m_value + m_maxValue) / (2 * m_maxValue) * 100.0f));
                // send this too, sometimes it resets :S
                player->SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldstate3, m_neutralValuePct);
            }
}

bool BfCapturePoint::SetCapturePointData(GameObject* capturePoint, TeamId team)
{
    ASSERT(capturePoint);

    //At first call using TEAM_NEUTRAL as a checker but never using it, after first call we reset the capturepoints to the new winner of the last WG war
    if (team == TEAM_NEUTRAL)
        team = m_team;
    LOG_DEBUG("bg.battlefield", "Creating capture point {}", capturePoint->GetEntry());

    m_capturePoint = capturePoint->GetGUID();

    // check info existence
    GameObjectTemplate const* goinfo = capturePoint->GetGOInfo();
    if (goinfo->type != GAMEOBJECT_TYPE_CAPTURE_POINT)
    {
        LOG_ERROR("bg.battlefield", "OutdoorPvP: GO {} is not capture point!", capturePoint->GetEntry());
        return false;
    }

    // get the needed values from goinfo
    m_maxValue = goinfo->capturePoint.maxTime;
    m_maxSpeed = m_maxValue / (goinfo->capturePoint.minTime ? goinfo->capturePoint.minTime : 60);
    m_neutralValuePct = goinfo->capturePoint.neutralPercent;
    m_minValue = m_maxValue * goinfo->capturePoint.neutralPercent / 100;
    m_capturePointEntry = capturePoint->GetEntry();
    if (team == TEAM_ALLIANCE)
    {
        m_value = m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE;
    }
    else
    {
        m_value = -m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE;
    }

    return true;
}

bool BfCapturePoint::DelCapturePoint()
{
    if (GameObject* capturePoint = GetCapturePointGo())
    {
        capturePoint->SetRespawnTime(0);                  // not save respawn time
        capturePoint->Delete();
        m_capturePoint.Clear();
    }

    return true;
}

GameObject* BfCapturePoint::GetCapturePointGo()
{
    return m_Bf->GetGameObject(m_capturePoint);
}

GameObject* BfCapturePoint::GetCapturePointGo(WorldObject* obj)
{
    return ObjectAccessor::GetGameObject(*obj, m_capturePoint);
}

bool BfCapturePoint::Update(uint32 diff)
{
    GameObject* capturePoint = GetCapturePointGo();
    if (!capturePoint)
        return false;

    float radius = capturePoint->GetGOInfo()->capturePoint.radius;

    for (uint8 team = 0; team < 2; ++team)
    {
        for (GuidUnorderedSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end();)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                if (!capturePoint->IsWithinDistInMap(player, radius) || !player->IsOutdoorPvPActive())
                {
                    itr = HandlePlayerLeave(player);
                    continue;
                }

            ++itr;
        }
    }

    std::list<Player*> players;
    Acore::AnyPlayerInObjectRangeCheck checker(capturePoint, radius);
    Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(capturePoint, players, checker);
    Cell::VisitWorldObjects(capturePoint, searcher, radius);

    for (std::list<Player*>::iterator itr = players.begin(); itr != players.end(); ++itr)
        if ((*itr)->IsOutdoorPvPActive())
            if (m_activePlayers[(*itr)->GetTeamId()].insert((*itr)->GetGUID()).second)
                HandlePlayerEnter(*itr);

    // get the difference of numbers
    float fact_diff = ((float) m_activePlayers[0].size() - (float) m_activePlayers[1].size()) * diff / BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL;
    if (G3D::fuzzyEq(fact_diff, 0.0f))
        return false;

    TeamId ChallengerId = TEAM_NEUTRAL;
    float maxDiff = m_maxSpeed * diff;

    if (fact_diff < 0)
    {
        // horde is in majority, but it's already horde-controlled -> no change
        if (m_State == BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE && m_value <= -m_maxValue)
            return false;

        if (fact_diff < -maxDiff)
            fact_diff = -maxDiff;

        ChallengerId = TEAM_HORDE;
    }
    else
    {
        // ally is in majority, but it's already ally-controlled -> no change
        if (m_State == BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE && m_value >= m_maxValue)
            return false;

        if (fact_diff > maxDiff)
            fact_diff = maxDiff;

        ChallengerId = TEAM_ALLIANCE;
    }

    float oldValue = m_value;
    TeamId oldTeam = m_team;

    m_OldState = m_State;

    m_value += fact_diff;

    if (m_value < -m_minValue)                              // red
    {
        if (m_value < -m_maxValue)
            m_value = -m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE;
        m_team = TEAM_HORDE;
    }
    else if (m_value > m_minValue)                          // blue
    {
        if (m_value > m_maxValue)
            m_value = m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE;
        m_team = TEAM_ALLIANCE;
    }
    else if (oldValue * m_value <= 0)                       // grey, go through mid point
    {
        // if challenger is ally, then n->a challenge
        if (ChallengerId == TEAM_ALLIANCE)
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE;
        // if challenger is horde, then n->h challenge
        else if (ChallengerId == TEAM_HORDE)
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE;
        m_team = TEAM_NEUTRAL;
    }
    else                                                    // grey, did not go through mid point
    {
        // old phase and current are on the same side, so one team challenges the other
        if (ChallengerId == TEAM_ALLIANCE && (m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE || m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE))
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE;
        else if (ChallengerId == TEAM_HORDE && (m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE || m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE))
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE;
        m_team = TEAM_NEUTRAL;
    }

    if (G3D::fuzzyNe(m_value, oldValue))
        SendChangePhase();

    if (m_OldState != m_State)
    {
        //LOG_ERROR("bg.battlefield", "{}->{}", m_OldState, m_State);
        if (oldTeam != m_team)
            ChangeTeam(oldTeam);
        return true;
    }

    return false;
}

void BfCapturePoint::SendUpdateWorldState(uint32 field, uint32 value)
{
    for (uint8 team = 0; team < 2; ++team)
        for (GuidUnorderedSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)  // send to all players present in the area
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->SendUpdateWorldState(field, value);
}

void BfCapturePoint::SendObjectiveComplete(uint32 id, WOWGUID guid)
{
    uint8 team;
    switch (m_State)
    {
        case BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE:
            team = 0;
            break;
        case BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE:
            team = 1;
            break;
        default:
            return;
    }

    // send to all players present in the area
    for (GuidUnorderedSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(*itr))
            player->KilledMonsterCredit(id, guid);
}

bool BfCapturePoint::IsInsideObjective(Player* player) const
{
    return m_activePlayers[player->GetTeamId()].find(player->GetGUID()) != m_activePlayers[player->GetTeamId()].end();
}
