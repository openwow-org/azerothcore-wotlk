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

#include "DBCStores.h"
#include "GameTime.h"
#include "Group.h"
#include "LFGMgr.h"
#include "LFGPackets.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "WDataStore.h"
#include "User.h"

void BuildPlayerLockDungeonBlock(WDataStore& data, lfg::LfgLockMap const& lock)
{
    data << uint32(lock.size());                           // Size of lock dungeons
    for (lfg::LfgLockMap::const_iterator it = lock.begin(); it != lock.end(); ++it)
    {
        data << uint32(it->first);                         // Dungeon entry (id + type)
        data << uint32(it->second);                        // Lock status
    }
}

void BuildPartyLockDungeonBlock(WDataStore& data, const lfg::LfgLockPartyMap& lockMap)
{
    data << uint8(lockMap.size());
    for (lfg::LfgLockPartyMap::const_iterator it = lockMap.begin(); it != lockMap.end(); ++it)
    {
        data << it->first;                                 // Player guid
        BuildPlayerLockDungeonBlock(data, it->second);
    }
}

void User::HandleLfgJoinOpcode(WorldPackets::LFG::LFGJoin& packet)
{
    if (!sLFGMgr->isOptionEnabled(lfg::LFG_OPTION_ENABLE_DUNGEON_FINDER | lfg::LFG_OPTION_ENABLE_RAID_BROWSER | lfg::LFG_OPTION_ENABLE_SEASONAL_BOSSES) ||
        (GetPlayer()->GetGroup() && GetPlayer()->GetGroup()->GetLeaderGUID() != GetPlayer()->GetGUID() &&
         (GetPlayer()->GetGroup()->GetMembersCount() == MAXGROUPSIZE || !GetPlayer()->GetGroup()->isLFGGroup())))
        return;

    if (packet.Slots.empty())
    {
        LOG_DEBUG("lfg", "CMSG_LFG_JOIN {} no dungeons selected", GetPlayerInfo());
        return;
    }

    lfg::LfgDungeonSet newDungeons;
    for (uint32 slot : packet.Slots)
    {
        uint32 dungeon = slot & 0x00FFFFFF;                             // remove the type from the dungeon entry
        if (sLFGDungeonStore.LookupEntry(dungeon))
            newDungeons.insert(dungeon);
    }

    LOG_DEBUG("network", "CMSG_LFG_JOIN [{}] roles: {}, Dungeons: {}, Comment: {}",
                 GetPlayerInfo(), packet.Roles, newDungeons.size(), packet.Comment);

    sLFGMgr->JoinLfg(GetPlayer(), uint8(packet.Roles), newDungeons, packet.Comment);
}

void User::HandleLfgLeaveOpcode(WorldPackets::LFG::LFGLeave& /*packet*/)
{
    Group* group = GetPlayer()->GetGroup();
    WOWGUID guid = GetPlayer()->GetGUID();
    WOWGUID gguid = group ? group->GetGUID() : guid;

    LOG_DEBUG("network", "CMSG_LFG_LEAVE [{}] in group: {}", guid.ToString(), group ? 1 : 0);

    // Check cheating - only leader can leave the queue
    if (!group || group->GetLeaderGUID() == guid)
    {
        sLFGMgr->LeaveLfg(sLFGMgr->GetState(guid) == lfg::LFG_STATE_RAIDBROWSER ? guid : gguid);
        sLFGMgr->LeaveAllLfgQueues(guid, true, group ? group->GetGUID() : WOWGUID::Empty);
    }
}

void User::HandleLfgProposalResultOpcode(WDataStore& recvData)
{
    uint32 proposalID;                                      // Internal lfgGroupID
    bool accept;                                           // Accept to join?
    recvData >> proposalID;
    recvData >> accept;

    LOG_DEBUG("network", "CMSG_LFG_PROPOSAL_RESULT [{}] proposal: {} accept: {}", GetPlayer()->GetGUID().ToString(), proposalID, accept ? 1 : 0);
    sLFGMgr->UpdateProposal(proposalID, GetPlayer()->GetGUID(), accept);
}

void User::HandleLfgSetRolesOpcode(WDataStore& recvData)
{
    uint8 roles;
    recvData >> roles;                                    // Player Group Roles
    WOWGUID guid = GetPlayer()->GetGUID();
    Group* group = GetPlayer()->GetGroup();
    if (!group)
    {
        LOG_DEBUG("network", "CMSG_LFG_SET_ROLES [{}] Not in group", guid.ToString());
        return;
    }
    WOWGUID gguid = group->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_SET_ROLES: Group [{}], Player [{}], Roles: {}", gguid.ToString(), guid.ToString(), roles);
    sLFGMgr->UpdateRoleCheck(gguid, guid, roles);
}

void User::HandleLfgSetCommentOpcode(WDataStore&  recvData)
{
    std::string comment;
    recvData >> comment;
    WOWGUID guid = GetPlayer()->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_SET_COMMENT [{}] comment: {}", guid.ToString(), comment);

    sLFGMgr->SetComment(GetPlayer()->GetGUID(), comment);
    sLFGMgr->LfrSetComment(GetPlayer(), comment);
}

void User::HandleLfgSetBootVoteOpcode(WDataStore& recvData)
{
    bool agree;                                            // Agree to kick player
    recvData >> agree;

    WOWGUID guid = GetPlayer()->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_SET_BOOT_VOTE [{}] agree: {}", guid.ToString(), agree ? 1 : 0);
    sLFGMgr->UpdateBoot(guid, agree);
}

void User::HandleLfgTeleportOpcode(WDataStore& recvData)
{
    bool out;
    recvData >> out;

    LOG_DEBUG("network", "CMSG_LFG_TELEPORT [{}] out: {}", GetPlayer()->GetGUID().ToString(), out ? 1 : 0);
    sLFGMgr->TeleportPlayer(GetPlayer(), out);
}

void User::HandleLfgPlayerLockInfoRequestOpcode(WDataStore& /*recvData*/)
{
    WOWGUID guid = GetPlayer()->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_PLAYER_LOCK_INFO_REQUEST [{}]", guid.ToString());

    // Get Random dungeons that can be done at a certain level and expansion
    uint8 level = GetPlayer()->GetLevel();
    lfg::LfgDungeonSet const& randomDungeons =
        sLFGMgr->GetRandomAndSeasonalDungeons(level, GetPlayer()->User()->Expansion());

    // Get player locked Dungeons
    sLFGMgr->InitializeLockedDungeons(GetPlayer(), GetPlayer()->GetGroup()); // pussywizard
    lfg::LfgLockMap const& lock = sLFGMgr->GetLockedDungeons(guid);
    uint32 rsize = uint32(randomDungeons.size());
    uint32 lsize = uint32(lock.size());

    LOG_DEBUG("network", "SMSG_LFG_PLAYER_INFO [{}]", guid.ToString());
    WDataStore data(SMSG_LFG_PLAYER_INFO, 1 + rsize * (4 + 1 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4) + 4 + lsize * (1 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4));

    data << uint8(randomDungeons.size());                  // Random Dungeon count
    for (lfg::LfgDungeonSet::const_iterator it = randomDungeons.begin(); it != randomDungeons.end(); ++it)
    {
        data << uint32(*it);                               // Dungeon Entry (id + type)
        lfg::LfgReward const* reward = sLFGMgr->GetRandomDungeonReward(*it, level);
        Quest const* quest = nullptr;
        bool done = false;
        if (reward)
        {
            quest = sObjectMgr->GetQuestTemplate(reward->firstQuest);
            if (quest)
            {
                done = !GetPlayer()->CanRewardQuest(quest, false);
                if (done)
                    quest = sObjectMgr->GetQuestTemplate(reward->otherQuest);
            }
        }

        if (quest)
        {
            uint8 playerLevel = GetPlayer() ? GetPlayer()->GetLevel() : 0;
            data << uint8(done);
            data << uint32(quest->GetRewOrReqMoney(playerLevel));
            if (!GetPlayer()->IsMaxLevel())
                data << uint32(quest->XPValue(playerLevel));
            else
                data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint8(quest->GetRewItemsCount());
            if (quest->GetRewItemsCount())
            {
                for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
                    if (uint32 itemId = quest->RewardItemId[i])
                    {
                        ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemId);
                        data << uint32(itemId);
                        data << uint32(item ? item->DisplayInfoID : 0);
                        data << uint32(quest->RewardItemIdCount[i]);
                    }
            }
        }
        else
        {
            data << uint8(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint8(0);
        }
    }
    BuildPlayerLockDungeonBlock(data, lock);
    Send(&data);
}

void User::HandleLfgPartyLockInfoRequestOpcode(WDataStore&  /*recvData*/)
{
    WOWGUID guid = GetPlayer()->GetGUID();
    LOG_DEBUG("network", "CMSG_LFG_PARTY_LOCK_INFO_REQUEST [{}]", guid.ToString());

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    // Get the locked dungeons of the other party members
    lfg::LfgLockPartyMap lockMap;
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* plrg = itr->GetSource();
        if (!plrg)
            continue;

        WOWGUID pguid = plrg->GetGUID();
        if (pguid == guid)
            continue;

        sLFGMgr->InitializeLockedDungeons(plrg, group); // pussywizard
        lockMap[pguid] = sLFGMgr->GetLockedDungeons(pguid);
    }

    uint32 size = 0;
    for (lfg::LfgLockPartyMap::const_iterator it = lockMap.begin(); it != lockMap.end(); ++it)
        size += 8 + 4 + uint32(it->second.size()) * (4 + 4);

    LOG_DEBUG("network", "SMSG_LFG_PARTY_INFO [{}]", guid.ToString());
    WDataStore data(SMSG_LFG_PARTY_INFO, 1 + size);
    BuildPartyLockDungeonBlock(data, lockMap);
    Send(&data);
}

void User::HandleLfrSearchJoinOpcode(WDataStore& recvData)
{
    uint32 dungeonId;
    recvData >> dungeonId;
    dungeonId = (dungeonId & 0x00FFFFFF); // remove the type from the dungeon entry
    sLFGMgr->LfrSearchAdd(GetPlayer(), dungeonId);
    sLFGMgr->SendRaidBrowserCachedList(GetPlayer(), dungeonId);
}

void User::HandleLfrSearchLeaveOpcode(WDataStore& recvData)
{
    uint32 dungeonId;
    recvData >> dungeonId;
    sLFGMgr->LfrSearchRemove(GetPlayer());
}

void User::HandleLfgGetStatus(WDataStore& /*recvData*/)
{
    LOG_DEBUG("lfg", "CMSG_LFG_GET_STATUS {}", GetPlayerInfo());

    WOWGUID guid = GetPlayer()->GetGUID();
    lfg::LfgUpdateData updateData = sLFGMgr->GetLfgStatus(guid);

    if (GetPlayer()->GetGroup())
    {
        SendLfgUpdateParty(updateData);
        updateData.dungeons.clear();
        SendLfgUpdatePlayer(updateData);
    }
    else
    {
        SendLfgUpdatePlayer(updateData);
        updateData.dungeons.clear();
        SendLfgUpdateParty(updateData);
    }
}

void User::SendLfgUpdatePlayer(lfg::LfgUpdateData const& updateData)
{
    bool queued = false;
    uint8 size = uint8(updateData.dungeons.size());

    switch (updateData.updateType)
    {
        case lfg::LFG_UPDATETYPE_JOIN_QUEUE:
        case lfg::LFG_UPDATETYPE_ADDED_TO_QUEUE:
            queued = true;
            break;
        case lfg::LFG_UPDATETYPE_UPDATE_STATUS:
            queued = updateData.state == lfg::LFG_STATE_QUEUED;
            break;
        default:
            break;
    }

    LOG_DEBUG("lfg", "SMSG_LFG_UPDATE_PLAYER {} updatetype: {}",
                   GetPlayerInfo(), updateData.updateType);
    WDataStore data(SMSG_LFG_UPDATE_PLAYER, 1 + 1 + (size > 0 ? 1 : 0) * (1 + 1 + 1 + 1 + size * 4 + updateData.comment.length()));
    data << uint8(updateData.updateType);                  // Lfg Update type
    data << uint8(size > 0);                               // Extra info
    if (size)
    {
        data << uint8(queued);                             // Join the queue
        data << uint8(0);                                  // unk - Always 0
        data << uint8(0);                                  // unk - Always 0

        data << uint8(size);
        for (lfg::LfgDungeonSet::const_iterator it = updateData.dungeons.begin(); it != updateData.dungeons.end(); ++it)
            data << uint32(*it);
        data << updateData.comment;
    }
    Send(&data);
}

void User::SendLfgUpdateParty(lfg::LfgUpdateData const& updateData)
{
    bool join = false;
    bool queued = false;
    uint8 size = uint8(updateData.dungeons.size());

    switch (updateData.updateType)
    {
        case lfg::LFG_UPDATETYPE_ADDED_TO_QUEUE:                // Rolecheck Success
            queued = true;
            [[fallthrough]];
        case lfg::LFG_UPDATETYPE_PROPOSAL_BEGIN:
            join = true;
            break;
        case lfg::LFG_UPDATETYPE_UPDATE_STATUS:
            join = updateData.state != lfg::LFG_STATE_ROLECHECK && updateData.state != lfg::LFG_STATE_NONE;
            queued = updateData.state == lfg::LFG_STATE_QUEUED;
            break;
        default:
            break;
    }

    LOG_DEBUG("lfg", "SMSG_LFG_UPDATE_PARTY {} updatetype: {}",
                   GetPlayerInfo(), updateData.updateType);
    WDataStore data(SMSG_LFG_UPDATE_PARTY, 1 + 1 + (size > 0 ? 1 : 0) * (1 + 1 + 1 + 1 + 1 + size * 4 + updateData.comment.length()));
    data << uint8(updateData.updateType);                  // Lfg Update type
    data << uint8(size > 0);                               // Extra info
    if (size)
    {
        data << uint8(join);                               // LFG Join
        data << uint8(queued);                             // Join the queue
        data << uint8(0);                                  // unk - Always 0
        data << uint8(0);                                  // unk - Always 0
        for (uint8 i = 0; i < 3; ++i)
            data << uint8(0);                              // unk - Always 0

        data << uint8(size);
        for (lfg::LfgDungeonSet::const_iterator it = updateData.dungeons.begin(); it != updateData.dungeons.end(); ++it)
            data << uint32(*it);
        data << updateData.comment;
    }
    Send(&data);
}

void User::SendLfgRoleChosen(WOWGUID guid, uint8 roles)
{
    LOG_DEBUG("network", "SMSG_LFG_ROLE_CHOSEN [{}] guid: [{}] roles: {}", GetPlayer()->GetGUID().ToString(), guid.ToString(), roles);

    WDataStore data(SMSG_LFG_ROLE_CHOSEN, 8 + 1 + 4);
    data << guid;                                          // Guid
    data << uint8(roles > 0);                              // Ready
    data << uint32(roles);                                 // Roles
    Send(&data);
}

void User::SendLfgRoleCheckUpdate(lfg::LfgRoleCheck const& roleCheck)
{
    lfg::LfgDungeonSet dungeons;
    if (roleCheck.rDungeonId)
        dungeons.insert(roleCheck.rDungeonId);
    else
        dungeons = roleCheck.dungeons;

    LOG_DEBUG("network", "SMSG_LFG_ROLE_CHECK_UPDATE [{}]", GetPlayer()->GetGUID().ToString());
    WDataStore data(SMSG_LFG_ROLE_CHECK_UPDATE, 4 + 1 + 1 + dungeons.size() * 4 + 1 + roleCheck.roles.size() * (8 + 1 + 4 + 1));

    data << uint32(roleCheck.state);                       // Check result
    data << uint8(roleCheck.state == lfg::LFG_ROLECHECK_INITIALITING);
    data << uint8(dungeons.size());                        // Number of dungeons
    if (!dungeons.empty())
        for (lfg::LfgDungeonSet::iterator it = dungeons.begin(); it != dungeons.end(); ++it)
            data << uint32(sLFGMgr->GetLFGDungeonEntry(*it)); // Dungeon

    data << uint8(roleCheck.roles.size());                 // Players in group
    if (!roleCheck.roles.empty())
    {
        // Leader info MUST be sent 1st :S
        WOWGUID guid = roleCheck.leader;
        uint8 roles = roleCheck.roles.find(guid)->second;
        data << guid;                                      // Guid
        data << uint8(roles > 0);                          // Ready
        data << uint32(roles);                             // Roles
        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        data << uint8(player ? player->GetLevel() : 0);    // Level

        for (lfg::LfgRolesMap::const_iterator it = roleCheck.roles.begin(); it != roleCheck.roles.end(); ++it)
        {
            if (it->first == roleCheck.leader)
                continue;

            guid = it->first;
            roles = it->second;
            data << guid;                                  // Guid
            data << uint8(roles > 0);                      // Ready
            data << uint32(roles);                         // Roles
            player = ObjectAccessor::FindConnectedPlayer(guid);
            data << uint8(player ? player->GetLevel() : 0);// Level
        }
    }
    Send(&data);
}

void User::SendLfgJoinResult(lfg::LfgJoinResultData const& joinData)
{
    uint32 size = 0;
    for (lfg::LfgLockPartyMap::const_iterator it = joinData.lockmap.begin(); it != joinData.lockmap.end(); ++it)
        size += 8 + 4 + uint32(it->second.size()) * (4 + 4);

    LOG_DEBUG("network", "SMSG_LFG_JOIN_RESULT [{}] checkResult: {} checkValue: {}", GetPlayer()->GetGUID().ToString(), joinData.result, joinData.state);
    WDataStore data(SMSG_LFG_JOIN_RESULT, 4 + 4 + size);
    data << uint32(joinData.result);                       // Check Result
    data << uint32(joinData.state);                        // Check Value
    if (!joinData.lockmap.empty())
        BuildPartyLockDungeonBlock(data, joinData.lockmap);
    Send(&data);
}

void User::SendLfgQueueStatus(lfg::LfgQueueStatusData const& queueData)
{
    LOG_DEBUG("network", "SMSG_LFG_QUEUE_STATUS [{}] dungeon: {} - waitTime: {} - avgWaitTime: {} - waitTimeTanks: {} - waitTimeHealer: {} - waitTimeDps: {} - queuedTime: {} - tanks: {} - healers: {} - dps: {}",
                   GetPlayer()->GetGUID().ToString(), queueData.dungeonId, queueData.waitTime, queueData.waitTimeAvg, queueData.waitTimeTank,
                   queueData.waitTimeHealer, queueData.waitTimeDps, queueData.queuedTime, queueData.tanks, queueData.healers, queueData.dps);
    WDataStore data(SMSG_LFG_QUEUE_STATUS, 4 + 4 + 4 + 4 + 4 + 4 + 1 + 1 + 1 + 4);
    data << uint32(queueData.dungeonId);                   // Dungeon
    data << int32(queueData.waitTimeAvg);                  // Average Wait time
    data << int32(queueData.waitTime);                     // Wait Time
    data << int32(queueData.waitTimeTank);                 // Wait Tanks
    data << int32(queueData.waitTimeHealer);               // Wait Healers
    data << int32(queueData.waitTimeDps);                  // Wait Dps
    data << uint8(queueData.tanks);                        // Tanks needed
    data << uint8(queueData.healers);                      // Healers needed
    data << uint8(queueData.dps);                          // Dps needed
    data << uint32(queueData.queuedTime);                  // Player wait time in queue
    Send(&data);
}

void User::SendLfgPlayerReward(lfg::LfgPlayerRewardData const& rewardData)
{
    if (!rewardData.rdungeonEntry || !rewardData.sdungeonEntry || !rewardData.quest)
        return;

    LOG_DEBUG("lfg", "SMSG_LFG_PLAYER_REWARD {} rdungeonEntry: {}, sdungeonEntry: {}, done: {}",
                   GetPlayerInfo(), rewardData.rdungeonEntry, rewardData.sdungeonEntry, rewardData.done);

    uint8 itemNum = rewardData.quest->GetRewItemsCount();

    uint8 playerLevel = GetPlayer() ? GetPlayer()->GetLevel() : 0;

    WDataStore data(SMSG_LFG_PLAYER_REWARD, 4 + 4 + 1 + 4 + 4 + 4 + 4 + 4 + 1 + itemNum * (4 + 4 + 4));
    data << uint32(rewardData.rdungeonEntry);              // Random Dungeon Finished
    data << uint32(rewardData.sdungeonEntry);              // Dungeon Finished
    data << uint8(rewardData.done);
    data << uint32(1);
    data << uint32(rewardData.quest->GetRewOrReqMoney(playerLevel));
    data << uint32(rewardData.quest->XPValue(playerLevel));
    data << uint32(0);
    data << uint32(0);
    data << uint8(itemNum);
    if (itemNum)
    {
        for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
            if (uint32 itemId = rewardData.quest->RewardItemId[i])
            {
                ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemId);
                data << uint32(itemId);
                data << uint32(item ? item->DisplayInfoID : 0);
                data << uint32(rewardData.quest->RewardItemIdCount[i]);
            }
    }
    Send(&data);
}

void User::SendLfgBootProposalUpdate(lfg::LfgPlayerBoot const& boot)
{
    WOWGUID guid = GetPlayer()->GetGUID();
    lfg::LfgAnswer playerVote = boot.votes.find(guid)->second;
    uint8 votesNum = 0;
    uint8 agreeNum = 0;
    uint32 secsleft = boot.cancelTime - GameTime::GetGameTime().count();
    for (lfg::LfgAnswerContainer::const_iterator it = boot.votes.begin(); it != boot.votes.end(); ++it)
    {
        if (it->second != lfg::LFG_ANSWER_PENDING)
        {
            ++votesNum;
            if (it->second == lfg::LFG_ANSWER_AGREE)
                ++agreeNum;
        }
    }
    LOG_DEBUG("network", "SMSG_LFG_BOOT_PROPOSAL_UPDATE [{}] inProgress: {} - didVote: {} - agree: {} - victim: [{}] votes: {} - agrees: {} - left: {} - needed: {} - reason {}",
                   guid.ToString(), uint8(boot.inProgress), uint8(playerVote != lfg::LFG_ANSWER_PENDING), uint8(playerVote == lfg::LFG_ANSWER_AGREE),
                   boot.victim.ToString(), votesNum, agreeNum, secsleft, lfg::LFG_GROUP_KICK_VOTES_NEEDED, boot.reason);
    WDataStore data(SMSG_LFG_BOOT_PROPOSAL_UPDATE, 1 + 1 + 1 + 8 + 4 + 4 + 4 + 4 + boot.reason.length());
    data << uint8(boot.inProgress);                        // Vote in progress
    data << uint8(playerVote != lfg::LFG_ANSWER_PENDING);  // Did Vote
    data << uint8(playerVote == lfg::LFG_ANSWER_AGREE);    // Agree
    data << boot.victim;                                   // Victim GUID
    data << uint32(votesNum);                              // Total Votes
    data << uint32(agreeNum);                              // Agree Count
    data << uint32(secsleft);                              // Time Left
    data << uint32(lfg::LFG_GROUP_KICK_VOTES_NEEDED);      // Needed Votes
    data << boot.reason.c_str();                           // Kick reason
    Send(&data);
}

void User::SendLfgUpdateProposal(lfg::LfgProposal const& proposal)
{
    WOWGUID guid = GetPlayer()->GetGUID();
    WOWGUID gguid = proposal.players.find(guid)->second.group;
    bool silent = !proposal.isNew && gguid == proposal.group;
    uint32 dungeonEntry = proposal.dungeonId;

    LOG_DEBUG("network", "SMSG_LFG_PROPOSAL_UPDATE [{} state: {}", guid.ToString(), proposal.state);

    // show random dungeon if player selected random dungeon and it's not lfg group
    if (!silent)
    {
        lfg::LfgDungeonSet const& playerDungeons = sLFGMgr->GetSelectedDungeons(guid);
        if (playerDungeons.find(proposal.dungeonId) == playerDungeons.end())
            dungeonEntry = (*playerDungeons.begin());
    }

    dungeonEntry = sLFGMgr->GetLFGDungeonEntry(dungeonEntry);

    WDataStore data(SMSG_LFG_PROPOSAL_UPDATE, 4 + 1 + 4 + 4 + 1 + 1 + proposal.players.size() * (4 + 1 + 1 + 1 + 1 + 1));
    data << uint32(dungeonEntry);                          // Dungeon
    data << uint8(proposal.state);                         // Proposal state
    data << uint32(proposal.id);                           // Proposal ID
    data << uint32(proposal.encounters);                   // encounters done
    data << uint8(silent);                                 // Show proposal window
    data << uint8(proposal.players.size());                // Group size

    for (lfg::LfgProposalPlayerContainer::const_iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
    {
        lfg::LfgProposalPlayer const& player = it->second;
        data << uint32(player.role);                       // Role
        data << uint8(it->first == guid);                  // Self player
        if (!player.group)                                 // Player not it a group
        {
            data << uint8(0);                              // Not in dungeon
            data << uint8(0);                              // Not same group
        }
        else
        {
            data << uint8(player.group == proposal.group); // In dungeon (silent)
            data << uint8(player.group == gguid);          // Same Group than player
        }
        data << uint8(player.accept != lfg::LFG_ANSWER_PENDING);// Answered
        data << uint8(player.accept == lfg::LFG_ANSWER_AGREE);  // Accepted
    }
    Send(&data);
}

void User::SendLfgLfrList(bool update)
{
    LOG_DEBUG("network", "SMSG_LFG_LFR_LIST [{}] update: {}", GetPlayer()->GetGUID().ToString(), update ? 1 : 0);
    WDataStore data(SMSG_LFG_UPDATE_SEARCH, 1);
    data << uint8(update);                                 // In Lfg Queue?
    Send(&data);
}

void User::SendLfgDisabled()
{
    LOG_DEBUG("network", "SMSG_LFG_DISABLED [{}]", GetPlayer()->GetGUID().ToString());
    WDataStore data(SMSG_LFG_DISABLED, 0);
    Send(&data);
}

void User::SendLfgOfferContinue(uint32 dungeonEntry)
{
    LOG_DEBUG("network", "SMSG_LFG_OFFER_CONTINUE [{}] dungeon entry: {}", GetPlayer()->GetGUID().ToString(), dungeonEntry);
    WDataStore data(SMSG_LFG_OFFER_CONTINUE, 4);
    data << uint32(dungeonEntry);
    Send(&data);
}

void User::SendLfgTeleportError(uint8 err)
{
    LOG_DEBUG("network", "SMSG_LFG_TELEPORT_DENIED [{}] reason: {}", GetPlayer()->GetGUID().ToString(), err);
    WDataStore data(SMSG_LFG_TELEPORT_DENIED, 4);
    data << uint32(err);                                   // Error
    Send(&data);
}
