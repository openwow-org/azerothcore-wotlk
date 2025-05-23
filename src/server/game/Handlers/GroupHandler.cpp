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

#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
#include "LFGMgr.h"
#include "Language.h"
#include "Log.h"
#include "MiscPackets.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "Util.h"
#include "Vehicle.h"
#include "World.h"
#include "WDataStore.h"
#include "User.h"

class Aura;

/* differeces from off:
    -you can uninvite yourself - is is useful
    -you can accept invitation even if leader went offline
*/
/* todo:
    -group_destroyed msg is sent but not shown
    -reduce xp gaining when in raid group
    -quest sharing has to be corrected
    -FIX sending PartyMemberStats
*/

void User::SendPartyResult(PartyOperation operation, const std::string& member, PartyResult res, uint32 val /* = 0 */)
{
    WDataStore data(SMSG_PARTY_COMMAND_RESULT, 4 + member.size() + 1 + 4 + 4);
    data << uint32(operation);
    data << member;
    data << uint32(res);
    data << uint32(val);                                    // LFD cooldown related (used with ERR_PARTY_LFG_BOOT_COOLDOWN_S and ERR_PARTY_LFG_BOOT_NOT_ELIGIBLE_S)

    Send(&data);
}

void User::HandleGroupInviteOpcode(WDataStore& recvData)
{
    std::string membername;
    recvData >> membername;
    recvData.read_skip<uint32>();

    // attempt add selected player

    // cheating
    if (!normalizePlayerName(membername))
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    Player* invitingPlayer = GetPlayer();
    Player* invitedPlayer = ObjectAccessor::FindPlayerByName(membername, false);

    // no player or cheat self-invite
    if (!invitedPlayer || invitedPlayer == invitingPlayer)
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    if (!sScriptMgr->CanGroupInvite(invitingPlayer, membername))
        return;

    if (invitingPlayer->IsSpectator() || invitedPlayer->IsSpectator())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_INVITE_RESTRICTED);
        return;
    }

    // restrict invite to GMs
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_GM_GROUP) && !invitingPlayer->IsGameMaster() && invitedPlayer->IsGameMaster())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    // can't group with
    if (!invitingPlayer->IsGameMaster() && !sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP) && invitingPlayer->GetTeamId() != invitedPlayer->GetTeamId())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_PLAYER_WRONG_FACTION);
        return;
    }
    if (invitingPlayer->GetInstanceId() != 0 && invitedPlayer->GetInstanceId() != 0 && invitingPlayer->GetInstanceId() != invitedPlayer->GetInstanceId() && invitingPlayer->GetMapId() == invitedPlayer->GetMapId())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_TARGET_NOT_IN_INSTANCE_S);
        return;
    }

    if (invitedPlayer->FriendListPtr()->IsIgnored(invitingPlayer->GetGUID()))
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_IGNORING_YOU_S);
        return;
    }

    if (!invitedPlayer->FriendListPtr()->GetFriend(invitingPlayer->GetGUID()) && invitingPlayer->GetLevel() < sWorld->getIntConfig(CONFIG_PARTY_LEVEL_REQ))
    {
        SendPartyResult(PARTY_OP_INVITE, invitedPlayer->GetName(), ERR_INVITE_RESTRICTED);
        return;
    }

    Group* group = invitingPlayer->GetGroup();
    if (group && group->isBGGroup())
        group = invitingPlayer->GetOriginalGroup();
    if (!group)
        group = invitingPlayer->GetGroupInvite();

    Group* group2 = invitedPlayer->GetGroup();
    if (group2 && group2->isBGGroup())
        group2 = invitedPlayer->GetOriginalGroup();
    // player already in another group or invited
    if (group2 || invitedPlayer->GetGroupInvite())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_ALREADY_IN_GROUP_S);

        if (group2)
        {
            // tell the player that they were invited but it failed as they were already in a group
            WDataStore data(SMSG_GROUP_INVITE, 25);                // guess size
            data << uint8(0);                                       // invited/already in group flag
            data << invitingPlayer->GetName();                         // max len 48
            data << uint32(0);                                      // unk
            data << uint8(0);                                       // count
            data << uint32(0);                                      // unk
            invitedPlayer->User()->Send(&data);
        }

        return;
    }

    if (group)
    {
        // not have permissions for invite
        if (!group->IsLeader(invitingPlayer->GetGUID()) && !group->IsAssistant(invitingPlayer->GetGUID()))
        {
            SendPartyResult(PARTY_OP_INVITE, "", ERR_NOT_LEADER);
            return;
        }
        // not have place
        if (group->IsFull())
        {
            SendPartyResult(PARTY_OP_INVITE, "", ERR_GROUP_FULL);
            return;
        }
    }

    // xinef: if player has no group, check group invite
    if (!group && invitingPlayer->GetGroupInvite() && invitingPlayer->GetGroupInvite()->GetLeaderGUID() == invitingPlayer->GetGUID())
        group = invitingPlayer->GetGroupInvite();

    // ok, but group not exist, start a new group
    // but don't create and save the group to the DB until
    // at least one person joins
    if (!group)
    {
        group = new Group();
        // new group: if can't add then delete
        if (!group->AddLeaderInvite(invitingPlayer))
        {
            delete group;
            return;
        }
        if (!group->AddInvite(invitedPlayer))
        {
            delete group;
            return;
        }
    }
    else
    {
        // already existed group: if can't add then just leave
        if (!group->AddInvite(invitedPlayer))
        {
            return;
        }
    }

    // ok, we do it
    WDataStore data(SMSG_GROUP_INVITE, 10);                // guess size
    data << uint8(1);                                       // invited/already in group flag
    data << invitingPlayer->GetName();                         // max len 48
    data << uint32(0);                                      // unk
    data << uint8(0);                                       // count
    data << uint32(0);                                      // unk
    invitedPlayer->User()->Send(&data);

    SendPartyResult(PARTY_OP_INVITE, membername, ERR_PARTY_RESULT_OK);
}

void User::HandleGroupAcceptOpcode(WDataStore& recvData)
{
    recvData.read_skip<uint32>();
    Group* group = GetPlayer()->GetGroupInvite();

    if (!group)
        return;

    // Remove player from invitees in any case
    group->RemoveInvite(GetPlayer());

    if (GetPlayer()->IsSpectator())
    {
        SendPartyResult(PARTY_OP_INVITE, "", ERR_INVITE_RESTRICTED);
        return;
    }

    if (!sScriptMgr->CanGroupAccept(GetPlayer(), group))
        return;

    if (group->GetLeaderGUID() == GetPlayer()->GetGUID())
    {
        LOG_ERROR("network.opcode", "HandleGroupAcceptOpcode: player {} ({}) tried to accept an invite to his own group",
            GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());
        return;
    }

    // Group is full
    if (group->IsFull())
    {
        SendPartyResult(PARTY_OP_INVITE, "", ERR_GROUP_FULL);
        return;
    }

    Player* leader = ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID());

    // Forming a new group, create it
    if (!group->IsCreated())
    {
        // This can happen if the leader is zoning. To be removed once delayed actions for zoning are implemented
        if (!leader || leader->IsSpectator())
        {
            group->RemoveAllInvites();
            return;
        }

        // If we're about to create a group there really should be a leader present
        ASSERT(leader);
        group->RemoveInvite(leader);
        group->Create(leader);
        sGroupMgr->AddGroup(group);
    }

    // Everything is fine, do it, PLAYER'S GROUP IS SET IN ADDMEMBER!!!
    if (!group->AddMember(GetPlayer()))
        return;

    group->BroadcastGroupUpdate();
}

void User::HandleGroupDeclineOpcode(WDataStore& /*recvData*/)
{
    Group* group = GetPlayer()->GetGroupInvite();
    if (!group)
        return;

    // Remember leader if online (group pointer will be invalid if group gets disbanded)
    Player* leader = ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID());

    // uninvite, group can be deleted
    GetPlayer()->UninviteFromGroup();

    if (!leader)
        return;

    // report
    WDataStore data(SMSG_GROUP_DECLINE, GetPlayer()->GetName().length());
    data << GetPlayer()->GetName();
    leader->User()->Send(&data);
}

void User::HandleGroupUninviteGuidOpcode(WDataStore& recvData)
{
    WOWGUID guid;
    std::string reason, name;
    recvData >> guid;
    recvData >> reason;

    //can't uninvite yourself
    if (guid == GetPlayer()->GetGUID())
    {
        LOG_ERROR("network.opcode", "User::HandleGroupUninviteGuidOpcode: leader {} ({}) tried to uninvite himself from the group.",
            GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());
        return;
    }

    sCharacterCache->GetCharacterNameByGuid(guid, name);

    PartyResult res = GetPlayer()->CanUninviteFromGroup(guid);
    if (res != ERR_PARTY_RESULT_OK)
    {
        if (res == ERR_PARTY_LFG_BOOT_NOT_ELIGIBLE_S)
        {
            if (Player* kickTarget = ObjectAccessor::FindConnectedPlayer(guid))
            {
                if (Aura* dungeonCooldownAura = kickTarget->GetAura(lfg::LFG_SPELL_DUNGEON_COOLDOWN))
                {
                    int32 elapsedTime = dungeonCooldownAura->GetMaxDuration() - dungeonCooldownAura->GetDuration();
                    if (static_cast<int32>(sWorld->getIntConfig(CONFIG_LFG_KICK_PREVENTION_TIMER)) > elapsedTime)
                    {
                        SendPartyResult(PARTY_OP_UNINVITE, name, res, (sWorld->getIntConfig(CONFIG_LFG_KICK_PREVENTION_TIMER) - elapsedTime) / 1000);
                    }
                }
            }
        } else
        {
            SendPartyResult(PARTY_OP_UNINVITE, name, res);
        }

        return;
    }

    Group* grp = GetPlayer()->GetGroup();
    if (!grp)
        return;

    // Xinef: do not allow to kick with empty reason, this will resend packet with given reason
    if (grp->isLFGGroup(true) && reason.empty())
    {
        SendPartyResult(PARTY_OP_UNINVITE, name, ERR_VOTE_KICK_REASON_NEEDED);
        return;
    }

    if (grp->IsLeader(guid) && !grp->isLFGGroup(true))
    {
        SendPartyResult(PARTY_OP_UNINVITE, name, ERR_NOT_LEADER);
        return;
    }

    if (grp->IsMember(guid))
    {
        Player::RemoveFromGroup(grp, guid, GROUP_REMOVEMETHOD_KICK, GetPlayer()->GetGUID(), reason.c_str());
        return;
    }

    if (Player* player = grp->GetInvited(guid))
    {
        player->UninviteFromGroup();
        return;
    }

    SendPartyResult(PARTY_OP_UNINVITE, name, ERR_TARGET_NOT_IN_GROUP_S);
}

void User::HandleGroupUninviteOpcode(WDataStore& recvData)
{
    std::string membername;
    recvData >> membername;

    // player not found
    if (!normalizePlayerName(membername))
        return;

    // can't uninvite yourself
    if (GetPlayer()->GetName() == membername)
    {
        LOG_ERROR("network.opcode", "User::HandleGroupUninviteOpcode: leader {} ({}) tried to uninvite himself from the group.",
            GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());
        return;
    }

    Group* grp = GetPlayer()->GetGroup();
    if (!grp)
    {
        return;
    }

    PartyResult res = GetPlayer()->CanUninviteFromGroup(grp->GetMemberGUID(membername));
    if (res != ERR_PARTY_RESULT_OK)
    {
        SendPartyResult(PARTY_OP_UNINVITE, "", res);
        return;
    }

    if (WOWGUID guid = grp->GetMemberGUID(membername))
    {
        Player::RemoveFromGroup(grp, guid, GROUP_REMOVEMETHOD_KICK, GetPlayer()->GetGUID());
        return;
    }

    if (Player* player = grp->GetInvited(membername))
    {
        player->UninviteFromGroup();
        return;
    }

    SendPartyResult(PARTY_OP_UNINVITE, membername, ERR_TARGET_NOT_IN_GROUP_S);
}

void User::HandleGroupSetLeaderOpcode(WDataStore& recvData)
{
    WOWGUID guid;
    recvData >> guid;

    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    Group* group = GetPlayer()->GetGroup();

    if (!group || !player)
        return;

    if (!group->IsLeader(GetPlayer()->GetGUID()) || player->GetGroup() != group || guid == GetPlayer()->GetGUID())
        return;

    // Everything's fine, accepted.
    group->ChangeLeader(guid);
    group->SendUpdate();
}

void User::HandleGroupDisbandOpcode(WDataStore& /*recvData*/)
{
    Group* grp = GetPlayer()->GetGroup();
    Group* grpInvite = GetPlayer()->GetGroupInvite();
    if (!grp && !grpInvite)
        return;

    if (m_player->InBattleground())
    {
        SendPartyResult(PARTY_OP_INVITE, "", ERR_INVITE_RESTRICTED);
        return;
    }

    /** error handling **/
    /********************/

    // everything's fine, do it
    if (grp)
    {
        SendPartyResult(PARTY_OP_LEAVE, GetPlayer()->GetName(), ERR_PARTY_RESULT_OK);
        GetPlayer()->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
    }
    else if (grpInvite && grpInvite->GetLeaderGUID() == GetPlayer()->GetGUID())
    { // pending group creation being cancelled
        SendPartyResult(PARTY_OP_LEAVE, GetPlayer()->GetName(), ERR_PARTY_RESULT_OK);
        grpInvite->Disband();
    }
}

void User::HandleLootMethodOpcode(WDataStore& recvData)
{
    uint32 lootMethod;
    WOWGUID lootMaster;
    uint32 lootThreshold;
    recvData >> lootMethod >> lootMaster >> lootThreshold;

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    /** error handling **/
    // Xinef: Check if group is LFG
    if (!group->IsLeader(GetPlayer()->GetGUID()) || group->isLFGGroup(true))
        return;

    if (lootMethod > NEED_BEFORE_GREED)
        return;

    if (lootThreshold < ITEM_QUALITY_UNCOMMON || lootThreshold > ITEM_QUALITY_ARTIFACT)
        return;

    if (lootMethod == MASTER_LOOT && !group->IsMember(lootMaster))
        return;
    /********************/

    // everything's fine, do it
    group->SetLootMethod((LootMethod)lootMethod);
    group->SetMasterLooterGuid(lootMaster);
    group->SetLootThreshold((ItemQualities)lootThreshold);
    group->SendUpdate();
}

void User::HandleLootRoll(WDataStore& recvData)
{
    WOWGUID guid;
    uint32 itemSlot;
    uint8  rollType;
    recvData >> guid;                  // guid of the item rolled
    recvData >> itemSlot;
    recvData >> rollType;              // 0: pass, 1: need, 2: greed

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    group->CountRollVote(GetPlayer()->GetGUID(), guid, rollType);

    switch (rollType)
    {
        case ROLL_NEED:
            GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED, 1);
            break;
        case ROLL_GREED:
            GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED, 1);
            break;
        case ROLL_DISENCHANT:
            GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_DISENCHANT, 1);
            break;
    }
}

void User::HandleMinimapPingOpcode(WDataStore& recvData)
{
    if (!GetPlayer()->GetGroup())
        return;

    float x, y;
    recvData >> x;
    recvData >> y;

    /** error handling **/
    /********************/

    // everything's fine, do it
    WDataStore data(MSG_MINIMAP_PING, (8 + 4 + 4));
    data << GetPlayer()->GetGUID();
    data << float(x);
    data << float(y);
    GetPlayer()->GetGroup()->BroadcastPacket(&data, true, -1, GetPlayer()->GetGUID());
}

void User::HandleRandomRollOpcode(WorldPackets::Misc::RandomRollClient& packet)
{
    uint32 minimum, maximum;
    minimum = packet.Min;
    maximum = packet.Max;

    /** error handling **/
    if (minimum > maximum || maximum > 10000) // < 32768 for urand call
    {
        return;
    }

    GetPlayer()->DoRandomRoll(minimum, maximum);
}

void User::HandleRaidTargetUpdateOpcode(WDataStore& recvData)
{
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    uint8  x;
    recvData >> x;

    /** error handling **/
    /********************/

    // everything's fine, do it
    if (x == 0xFF)                                           // target icon request
    {
        group->SendTargetIconList(this);
    }
    else                                                    // target icon update
    {
        if (group->isRaidGroup() && !group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()))
            return;

        WOWGUID guid;
        recvData >> guid;

        if (guid.IsPlayer())
        {
            Player* target = ObjectAccessor::FindConnectedPlayer(guid);

            if (!target || target->IsHostileTo(GetPlayer()))
                return;
        }

        group->SetTargetIcon(x, m_player->GetGUID(), guid);
    }
}

void User::HandleGroupRaidConvertOpcode(WDataStore& /*recvData*/)
{
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (m_player->InBattleground())
        return;

    /** error handling **/
    if (group->CheckLevelForRaid())
    {
        SendPartyResult(PARTY_OP_INVITE, "", ERR_RAID_DISALLOWED_BY_LEVEL);
        return;
    }

    if (!group->IsLeader(GetPlayer()->GetGUID()) || group->GetMembersCount() < 2 || group->isLFGGroup()) // pussywizard: not allowed for lfg groups, it is either raid from the beginning or not!
        return;
    /********************/

    // everything's fine, do it (is it 0 (PARTY_OP_INVITE) correct code)
    SendPartyResult(PARTY_OP_INVITE, "", ERR_PARTY_RESULT_OK);
    group->ConvertToRaid();
}

void User::HandleGroupChangeSubGroupOpcode(WDataStore& recvData)
{
    // we will get correct pointer for group here, so we don't have to check if group is BG raid
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    std::string name;
    uint8 groupNr;
    recvData >> name;
    recvData >> groupNr;

    if (groupNr >= MAX_RAID_SUBGROUPS)
        return;

    WOWGUID senderGuid = GetPlayer()->GetGUID();
    if (!group->IsLeader(senderGuid) && !group->IsAssistant(senderGuid))
        return;

    if (!group->HasFreeSlotSubGroup(groupNr))
        return;

    Player* movedPlayer = ObjectAccessor::FindPlayerByName(name, false);
    WOWGUID guid;
    if (movedPlayer)
    {
        guid = movedPlayer->GetGUID();
    }
    else
    {
        CharacterDatabase.EscapeString(name);
        guid = sCharacterCache->GetCharacterGuidByName(name);
    }

    group->ChangeMembersGroup(guid, groupNr);
}

void User::HandleGroupAssistantLeaderOpcode(WDataStore& recvData)
{
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (!group->IsLeader(GetPlayer()->GetGUID()))
        return;

    WOWGUID guid;
    bool apply;
    recvData >> guid;
    recvData >> apply;

    group->SetGroupMemberFlag(guid, apply, MEMBER_FLAG_ASSISTANT);

    group->SendUpdate();
}

void User::HandlePartyAssignmentOpcode(WDataStore& recvData)
{
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    WOWGUID senderGuid = GetPlayer()->GetGUID();
    if (!group->IsLeader(senderGuid) && !group->IsAssistant(senderGuid))
        return;

    uint8 assignment;
    bool apply;
    WOWGUID guid;
    recvData >> assignment >> apply;
    recvData >> guid;

    switch (assignment)
    {
        case GROUP_ASSIGN_MAINASSIST:
            group->RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINASSIST);
            group->SetGroupMemberFlag(guid, apply, MEMBER_FLAG_MAINASSIST);
            break;
        case GROUP_ASSIGN_MAINTANK:
            group->RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINTANK);           // Remove main assist flag from current if any.
            group->SetGroupMemberFlag(guid, apply, MEMBER_FLAG_MAINTANK);
        default:
            break;
    }

    group->SendUpdate();
}

void User::HandleRaidReadyCheckOpcode(WDataStore& recvData)
{
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (recvData.empty())                                   // request
    {
        /** error handling **/
        if (!group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()))
            return;
        /********************/

        // Check if Ready Check in BG is enabled
        if (sWorld->getBoolConfig(CONFIG_BATTLEGROUND_DISABLE_READY_CHECK_IN_BG))
        {
            // Check if player is in BG
            if (m_player->InBattleground())
            {
                m_player->User()->SendNotification(LANG_BG_READY_CHECK_ERROR);
                return;
            }
        }

        // everything's fine, do it
        WDataStore data(MSG_RAID_READY_CHECK, 8);
        data << GetPlayer()->GetGUID();
        group->BroadcastPacket(&data, false, -1);

        group->OfflineReadyCheck();
    }
    else                                                    // answer
    {
        uint8 state;
        recvData >> state;

        // everything's fine, do it
        WDataStore data(MSG_RAID_READY_CHECK_CONFIRM, 9);
        data << GetPlayer()->GetGUID();
        data << uint8(state);
        group->BroadcastReadyCheck(&data);
    }
}

void User::HandleRaidReadyCheckFinishedOpcode(WDataStore& /*recvData*/)
{
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (!group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()))
        return;

    WDataStore data(MSG_RAID_READY_CHECK_FINISHED);
    group->BroadcastPacket(&data, true, -1);
}

void User::BuildPartyMemberStatsChangedPacket(Player* player, WDataStore* data)
{
    uint32 mask = player->GetGroupUpdateFlag();

    if (mask == GROUP_UPDATE_FLAG_NONE)
        return;

    if (mask & GROUP_UPDATE_FLAG_POWER_TYPE)                // if update power type, update current/max power also
        mask |= (GROUP_UPDATE_FLAG_CUR_POWER | GROUP_UPDATE_FLAG_MAX_POWER);

    if (mask & GROUP_UPDATE_FLAG_PET_POWER_TYPE)            // same for pets
        mask |= (GROUP_UPDATE_FLAG_PET_CUR_POWER | GROUP_UPDATE_FLAG_PET_MAX_POWER);

    uint32 byteCount = 0;
    for (int i = 1; i < GROUP_UPDATE_FLAGS_COUNT; ++i)
        if (mask & (1 << i))
            byteCount += GroupUpdateLength[i];

    data->Initialize(SMSG_PARTY_MEMBER_STATS, 8 + 4 + byteCount);
    *data << player->GetPackGUID();
    *data << uint32(mask);

    if (mask & GROUP_UPDATE_FLAG_STATUS)
    {
        uint16 playerStatus = MEMBER_STATUS_ONLINE;
        if (player->IsPvP())
            playerStatus |= MEMBER_STATUS_PVP;

        if (!player->IsAlive())
        {
            if (player->HasPlayerFlag(PLAYER_FLAGS_GHOST))
                playerStatus |= MEMBER_STATUS_GHOST;
            else
                playerStatus |= MEMBER_STATUS_DEAD;
        }

        if (player->IsFFAPvP())
            playerStatus |= MEMBER_STATUS_PVP_FFA;

        if (player->isAFK())
            playerStatus |= MEMBER_STATUS_AFK;

        if (player->isDND())
            playerStatus |= MEMBER_STATUS_DND;

        *data << uint16(playerStatus);
    }

    if (mask & GROUP_UPDATE_FLAG_CUR_HP)
        *data << uint32(player->GetHealth());

    if (mask & GROUP_UPDATE_FLAG_MAX_HP)
        *data << uint32(player->GetMaxHealth());

    POWER_TYPE powerType = player->GetPowerType();
    if (mask & GROUP_UPDATE_FLAG_POWER_TYPE)
        *data << uint8(powerType);

    if (mask & GROUP_UPDATE_FLAG_CUR_POWER)
        *data << uint16(player->GetPower(powerType));

    if (mask & GROUP_UPDATE_FLAG_MAX_POWER)
        *data << uint16(player->GetMaxPower(powerType));

    if (mask & GROUP_UPDATE_FLAG_LEVEL)
        *data << uint16(player->GetLevel());

    if (mask & GROUP_UPDATE_FLAG_ZONE)
        *data << uint16(player->GetZoneId());

    if (mask & GROUP_UPDATE_FLAG_POSITION)
    {
        *data << uint16(player->GetPositionX());
        *data << uint16(player->GetPositionY());
    }

    if (mask & GROUP_UPDATE_FLAG_AURAS)
    {
        uint64 auramask = player->GetAuraUpdateMaskForRaid();
        *data << uint64(auramask);
        for (uint32 i = 0; i < MAX_AURAS_GROUP_UPDATE; ++i)
        {
            if (auramask & (uint64(1) << i))
            {
                AuraApplication const* aurApp = player->GetVisibleAura(i);
                *data << uint32(aurApp ? aurApp->GetBase()->GetId() : 0);
                *data << uint8(1);
            }
        }
    }

    Pet* pet = player->GetPet();
    if (mask & GROUP_UPDATE_FLAG_PET_GUID)
    {
        if (pet)
            *data << pet->GetGUID();
        else
            *data << (uint64) 0;
    }

    if (mask & GROUP_UPDATE_FLAG_PET_NAME)
    {
        if (pet)
            *data << pet->GetName();
        else
            *data << uint8(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_MODEL_ID)
    {
        if (pet)
            *data << uint16(pet->GetDisplayId());
        else
            *data << uint16(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_CUR_HP)
    {
        if (pet)
            *data << uint32(pet->GetHealth());
        else
            *data << uint32(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_MAX_HP)
    {
        if (pet)
            *data << uint32(pet->GetMaxHealth());
        else
            *data << uint32(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_POWER_TYPE)
    {
        if (pet)
            *data << uint8(pet->GetPowerType());
        else
            *data << uint8(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_CUR_POWER)
    {
        if (pet)
            *data << uint16(pet->GetPower(pet->GetPowerType()));
        else
            *data << uint16(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_MAX_POWER)
    {
        if (pet)
            *data << uint16(pet->GetMaxPower(pet->GetPowerType()));
        else
            *data << uint16(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_AURAS)
    {
        if (pet)
        {
            uint64 auramask = pet->GetAuraUpdateMaskForRaid();
            *data << uint64(auramask);
            for (uint32 i = 0; i < MAX_AURAS_GROUP_UPDATE; ++i)
            {
                if (auramask & (uint64(1) << i))
                {
                    AuraApplication const* aurApp = pet->GetVisibleAura(i);
                    *data << uint32(aurApp ? aurApp->GetBase()->GetId() : 0);
                    *data << uint8(aurApp ? aurApp->GetFlags() : 0);
                }
            }
        }
        else
            *data << uint64(0);
    }

    if (mask & GROUP_UPDATE_FLAG_VEHICLE_SEAT)
    {
        if (Vehicle* veh = player->GetVehicle())
            * data << uint32(veh->GetVehicleInfo()->m_seatID[player->m_movement.transport.seat]);
        else
            *data << uint32(0);
    }
}

/*this procedure handles clients CMSG_REQUEST_PARTY_MEMBER_STATS request*/
void User::HandleRequestPartyMemberStatsOpcode(WDataStore& recvData)
{
    WOWGUID Guid;
    recvData >> Guid;

    Player* player = HashMapHolder<Player>::Find(Guid);
    if (!player || !player->IsInSameRaidWith(m_player))
    {
        WDataStore data(SMSG_PARTY_MEMBER_STATS_FULL, 3 + 4 + 2);
        data << uint8(0);                                   // only for SMSG_PARTY_MEMBER_STATS_FULL, probably arena/bg related
        data << Guid.WriteAsPacked();
        data << uint32(GROUP_UPDATE_FLAG_STATUS);
        data << uint16(MEMBER_STATUS_OFFLINE);
        Send(&data);
        return;
    }

    Pet* pet = player->GetPet();
    POWER_TYPE powerType = player->GetPowerType();

    WDataStore data(SMSG_PARTY_MEMBER_STATS_FULL, 4 + 2 + 2 + 2 + 1 + 2 * 6 + 8 + 1 + 8);
    data << uint8(0);                                       // only for SMSG_PARTY_MEMBER_STATS_FULL, probably arena/bg related
    data << player->GetPackGUID();

    uint32 updateFlags = GROUP_UPDATE_FLAG_STATUS | GROUP_UPDATE_FLAG_CUR_HP | GROUP_UPDATE_FLAG_MAX_HP
                         | GROUP_UPDATE_FLAG_CUR_POWER | GROUP_UPDATE_FLAG_MAX_POWER | GROUP_UPDATE_FLAG_LEVEL
                         | GROUP_UPDATE_FLAG_ZONE | GROUP_UPDATE_FLAG_POSITION | GROUP_UPDATE_FLAG_AURAS
                         | GROUP_UPDATE_FLAG_PET_NAME | GROUP_UPDATE_FLAG_PET_MODEL_ID | GROUP_UPDATE_FLAG_PET_AURAS;

    if (powerType != POWER_TYPE_MANA)
        updateFlags |= GROUP_UPDATE_FLAG_POWER_TYPE;

    if (pet)
        updateFlags |= GROUP_UPDATE_FLAG_PET_GUID | GROUP_UPDATE_FLAG_PET_CUR_HP | GROUP_UPDATE_FLAG_PET_MAX_HP
                       | GROUP_UPDATE_FLAG_PET_POWER_TYPE | GROUP_UPDATE_FLAG_PET_CUR_POWER | GROUP_UPDATE_FLAG_PET_MAX_POWER;

    if (player->GetVehicle())
        updateFlags |= GROUP_UPDATE_FLAG_VEHICLE_SEAT;

    uint16 playerStatus = MEMBER_STATUS_ONLINE;
    if (player->IsPvP())
        playerStatus |= MEMBER_STATUS_PVP;

    if (!player->IsAlive())
    {
        if (player->HasPlayerFlag(PLAYER_FLAGS_GHOST))
            playerStatus |= MEMBER_STATUS_GHOST;
        else
            playerStatus |= MEMBER_STATUS_DEAD;
    }

    if (player->IsFFAPvP())
        playerStatus |= MEMBER_STATUS_PVP_FFA;

    if (player->isAFK())
        playerStatus |= MEMBER_STATUS_AFK;

    if (player->isDND())
        playerStatus |= MEMBER_STATUS_DND;

    data << uint32(updateFlags);
    data << uint16(playerStatus);                           // GROUP_UPDATE_FLAG_STATUS
    data << uint32(player->GetHealth());                    // GROUP_UPDATE_FLAG_CUR_HP
    data << uint32(player->GetMaxHealth());                 // GROUP_UPDATE_FLAG_MAX_HP
    if (updateFlags & GROUP_UPDATE_FLAG_POWER_TYPE)
        data << uint8(powerType);

    data << uint16(player->GetPower(powerType));            // GROUP_UPDATE_FLAG_CUR_POWER
    data << uint16(player->GetMaxPower(powerType));         // GROUP_UPDATE_FLAG_MAX_POWER
    data << uint16(player->GetLevel());                     // GROUP_UPDATE_FLAG_LEVEL
    data << uint16(player->GetZoneId());                    // GROUP_UPDATE_FLAG_ZONE
    data << uint16(player->GetPositionX());                 // GROUP_UPDATE_FLAG_POSITION
    data << uint16(player->GetPositionY());                 // GROUP_UPDATE_FLAG_POSITION

    uint64 auraMask = 0;
    std::size_t maskPos = data.wpos();
    data << uint64(auraMask);                               // placeholder
    for (uint8 i = 0; i < MAX_AURAS_GROUP_UPDATE; ++i)
    {
        if (AuraApplication const* aurApp = player->GetVisibleAura(i))
        {
            auraMask |= uint64(1) << i;
            data << uint32(aurApp->GetBase()->GetId());
            data << uint8(aurApp->GetFlags());
        }
    }

    data.put<uint64>(maskPos, auraMask);                    // GROUP_UPDATE_FLAG_AURAS

    if (pet && (updateFlags & GROUP_UPDATE_FLAG_PET_GUID))
        data << pet->GetGUID();

    data << std::string(pet ? pet->GetName() : "");         // GROUP_UPDATE_FLAG_PET_NAME
    data << uint16(pet ? pet->GetDisplayId() : 0);          // GROUP_UPDATE_FLAG_PET_MODEL_ID

    if (pet && (updateFlags & GROUP_UPDATE_FLAG_PET_CUR_HP))
        data << uint32(pet->GetHealth());

    if (pet && (updateFlags & GROUP_UPDATE_FLAG_PET_MAX_HP))
        data << uint32(pet->GetMaxHealth());

    if (pet && (updateFlags & GROUP_UPDATE_FLAG_PET_POWER_TYPE))
        data << (uint8)pet->GetPowerType();

    if (pet && (updateFlags & GROUP_UPDATE_FLAG_PET_CUR_POWER))
        data << uint16(pet->GetPower(pet->GetPowerType()));

    if (pet && (updateFlags & GROUP_UPDATE_FLAG_PET_MAX_POWER))
        data << uint16(pet->GetMaxPower(pet->GetPowerType()));

    uint64 petAuraMask = 0;
    maskPos = data.wpos();
    data << uint64(petAuraMask);                            // placeholder
    if (pet)
    {
        for (uint8 i = 0; i < MAX_AURAS_GROUP_UPDATE; ++i)
        {
            if (AuraApplication const* aurApp = pet->GetVisibleAura(i))
            {
                petAuraMask |= uint64(1) << i;
                data << uint32(aurApp->GetBase()->GetId());
                data << uint8(aurApp->GetFlags());
            }
        }
    }

    data.put<uint64>(maskPos, petAuraMask);                 // GROUP_UPDATE_FLAG_PET_AURAS

    if (updateFlags & GROUP_UPDATE_FLAG_VEHICLE_SEAT)
        data << uint32(player->GetVehicle()->GetVehicleInfo()->m_seatID[player->m_movement.transport.seat]);

    Send(&data);
}

void User::HandleRequestRaidInfoOpcode(WDataStore& /*recvData*/)
{
    // every time the player checks the character screen
    m_player->SendRaidInfo();
}

void User::HandleOptOutOfLootOpcode(WDataStore& recvData)
{
    uint32 passOnLoot;
    recvData >> passOnLoot; // 1 always pass, 0 do not pass

    if (!GetPlayer())
        return;

    GetPlayer()->SetPassOnGroupLoot(passOnLoot);
}

void User::HandleGroupSwapSubGroupOpcode(WDataStore& recv_data)
{
    std::string playerName1, playerName2;

    // first = moved from, second = moved to
    recv_data >> playerName1;
    recv_data >> playerName2;

    if (!normalizePlayerName(playerName1))
    {
        SendPartyResult(PARTY_OP_SWAP, playerName1, ERR_GROUP_SWAP_FAILED);
        return;
    }
    if (!normalizePlayerName(playerName2))
    {
        SendPartyResult(PARTY_OP_SWAP, playerName1, ERR_GROUP_SWAP_FAILED);
        return;
    }

    Group* group = GetPlayer()->GetGroup();
    if (!group || !group->isRaidGroup())
    {
        return;
    }

    if (!group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()))
    {
        return;
    }

    //get guid, member may be offline
    auto getGuid = [&group](std::string const& playerName)
    {
        // no player, cheating?
        if (!group->GetMemberGUID(playerName))
        {
            return WOWGUID::Empty;
        }

        if (Player* player = ObjectAccessor::FindPlayerByName(playerName.c_str()))
        {
            return player->GetGUID();
        }
        else
        {
            if (WOWGUID guid = sCharacterCache->GetCharacterGuidByName(playerName))
            {
                return guid;
            }
            else
            {
                return WOWGUID::Empty; // no player - again, cheating?
            }
        }
    };

    WOWGUID guid1 = getGuid(playerName1);
    WOWGUID guid2 = getGuid(playerName2);

    if(!guid1 || !guid2)
    {
        SendPartyResult(PARTY_OP_SWAP, playerName1, ERR_GROUP_SWAP_FAILED);
        return;
    }

    uint8 groupId1 = group->GetMemberGroup(guid1);
    uint8 groupId2 = group->GetMemberGroup(guid2);

    if (groupId1 == MAX_RAID_SUBGROUPS + 1 || groupId2 == MAX_RAID_SUBGROUPS + 1)
    {
        return;
    }

    if (groupId1 == groupId2)
    {
        return;
    }

    group->ChangeMembersGroup(guid1, groupId2);
    group->ChangeMembersGroup(guid2, groupId1);
}
