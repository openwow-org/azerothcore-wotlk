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

#include "Common.h"
#include "GameTime.h"
#include "Log.h"
#include "MapMgr.h"
#include "NPCHandler.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"
#include "World.h"
#include "WDataStore.h"
#include "User.h"

void User::SendNameQueryOpcode(WOWGUID guid)
{
    CharacterCacheEntry const* playerData = sCharacterCache->GetCharacterCacheByGuid(guid);

    WDataStore data(SMSG_NAME_QUERY_RESPONSE, (8 + 1 + 1 + 1 + 1 + 1 + 10));
    data << guid.WriteAsPacked();
    if (!playerData)
    {
        data << uint8(1);                           // name unknown
        Send(&data);
        return;
    }

    Player* player = ObjectAccessor::FindConnectedPlayer(guid);

    data << uint8(0);                               // name known
    data << playerData->Name;                       // played name
    data << uint8(0);                               // realm name - only set for cross realm interaction (such as Battlegrounds)
    data << uint8(player ? player->getRace() : playerData->Race);
    data << uint8(playerData->Sex);
    data << uint8(playerData->Class);

    // pussywizard: optimization
    /*Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (DeclinedName const* names = (player ? player->GetDeclinedNames() : nullptr))
    {
        data << uint8(1);                           // Name is declined
        for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
            data << names->name[i];
    }
    else*/
    data << uint8(0);                           // Name is not declined

    Send(&data);
}

void User::HandleNameQueryOpcode(WDataStore& recvData)
{
    WOWGUID guid;
    recvData >> guid;

    // This is disable by default to prevent lots of console spam
    // LOG_INFO("network.opcode", "HandleNameQueryOpcode {}", guid);

    SendNameQueryOpcode(guid);
}

void User::HandleQueryTimeOpcode(WDataStore& /*recvData*/)
{
    SendQueryTimeResponse();
}

void User::SendQueryTimeResponse()
{
    auto timeResponse = sWorld->GetNextDailyQuestsResetTime() - GameTime::GetGameTime();

    WDataStore data(SMSG_QUERY_TIME_RESPONSE, 4 + 4);
    data << uint32(GameTime::GetGameTime().count());
    data << uint32(timeResponse.count());
    Send(&data);
}

/// Only _static_ data is sent in this packet !!!
void User::HandleCreatureQueryOpcode(WDataStore& recvData)
{
    uint32 entry;
    recvData >> entry;
    WOWGUID guid;
    recvData >> guid;

    CreatureTemplate const* ci = sObjectMgr->GetCreatureTemplate(entry);
    if (ci)
    {
        std::string Name, Title;
        Name = ci->Name;
        Title = ci->SubName;

        LocaleConstant loc_idx = GetSessionDbLocaleIndex();
        if (loc_idx >= 0)
        {
            if (CreatureLocale const* cl = sObjectMgr->GetCreatureLocale(entry))
            {
                ObjectMgr::GetLocaleString(cl->Name, loc_idx, Name);
                ObjectMgr::GetLocaleString(cl->Title, loc_idx, Title);
            }
        }
        // guess size
        WDataStore data(SMSG_CREATURE_QUERY_RESPONSE, 100);
        data << uint32(entry);                                       // creature entry
        data << Name;
        data << uint8(0) << uint8(0) << uint8(0);                    // name2, name3, name4, always empty
        data << Title;
        data << ci->IconName;                                        // "Directions" for guard, string for Icons 2.3.0
        data << uint32(ci->type_flags);                              // flags
        data << uint32(ci->type);                                    // CreatureType.dbc
        data << uint32(ci->family);                                  // CreatureFamily.dbc
        data << uint32(ci->rank);                                    // Creature Rank (elite, boss, etc)
        data << uint32(ci->KillCredit[0]);                           // new in 3.1, kill credit
        data << uint32(ci->KillCredit[1]);                           // new in 3.1, kill credit
        if (ci->GetModelByIdx(0))
            data << uint32(ci->GetModelByIdx(0)->CreatureDisplayID); // Modelid1
        else
            data << uint32(0);                                       // Modelid1
        if (ci->GetModelByIdx(1))
            data << uint32(ci->GetModelByIdx(1)->CreatureDisplayID); // Modelid2
        else
            data << uint32(0);                                       // Modelid2
        if (ci->GetModelByIdx(2))
            data << uint32(ci->GetModelByIdx(2)->CreatureDisplayID); // Modelid3
        else
            data << uint32(0);                                       // Modelid3
        if (ci->GetModelByIdx(3))
            data << uint32(ci->GetModelByIdx(3)->CreatureDisplayID); // Modelid4
        else
            data << uint32(0);                                       // Modelid4
        data << float(ci->ModHealth);                                // dmg/hp modifier
        data << float(ci->ModMana);                                  // dmg/mana modifier
        data << uint8(ci->RacialLeader);

        CreatureQuestItemList const* items = sObjectMgr->GetCreatureQuestItemList(entry);
        if (items)
            for (std::size_t i = 0; i < MAX_CREATURE_QUEST_ITEMS; ++i)
                data << (i < items->size() ? uint32((*items)[i]) : uint32(0));
        else
            for (std::size_t i = 0; i < MAX_CREATURE_QUEST_ITEMS; ++i)
                data << uint32(0);

        data << uint32(ci->movementId);                     // CreatureMovementInfo.dbc
        Send(&data);
    }
    else
    {
        LOG_DEBUG("network", "WORLD: CMSG_CREATURE_QUERY - NO CREATURE INFO! ({})", guid.ToString());
        WDataStore data(SMSG_CREATURE_QUERY_RESPONSE, 4);
        data << uint32(entry | 0x80000000);
        Send(&data);
        LOG_DEBUG("network", "WORLD: Sent SMSG_CREATURE_QUERY_RESPONSE");
    }
}

/// Only _static_ data is sent in this packet !!!
void User::HandleGameObjectQueryOpcode(WDataStore& recvData)
{
    uint32 entry;
    recvData >> entry;
    WOWGUID guid;
    recvData >> guid;

    const GameObjectTemplate* info = sObjectMgr->GetGameObjectTemplate(entry);
    if (info)
    {
        std::string Name;
        std::string IconName;
        std::string CastBarCaption;

        Name = info->name;
        IconName = info->IconName;
        CastBarCaption = info->castBarCaption;

        LocaleConstant localeConstant = GetSessionDbLocaleIndex();
        if (localeConstant >= LOCALE_enUS)
            if (GameObjectLocale const* gameObjectLocale = sObjectMgr->GetGameObjectLocale(entry))
            {
                ObjectMgr::GetLocaleString(gameObjectLocale->Name, localeConstant, Name);
                ObjectMgr::GetLocaleString(gameObjectLocale->CastBarCaption, localeConstant, CastBarCaption);
            }

        LOG_DEBUG("network", "WORLD: CMSG_GAMEOBJECT_QUERY '{}' - Entry: {}. ", info->name, entry);
        WDataStore data (SMSG_GAMEOBJECT_QUERY_RESPONSE, 150);
        data << uint32(entry);
        data << uint32(info->type);
        data << uint32(info->displayId);
        data << Name;
        data << uint8(0) << uint8(0) << uint8(0);           // name2, name3, name4
        data << IconName;                                   // 2.0.3, string. Icon name to use instead of default icon for go's (ex: "Attack" makes sword)
        data << CastBarCaption;                             // 2.0.3, string. Text will appear in Cast Bar when using GO (ex: "Collecting")
        data << info->unk1;                                 // 2.0.3, string
        data.append(info->raw.data, MAX_GAMEOBJECT_DATA);
        data << float(info->size);                          // go size

        GameObjectQuestItemList const* items = sObjectMgr->GetGameObjectQuestItemList(entry);
        if (items)
            for (std::size_t i = 0; i < MAX_GAMEOBJECT_QUEST_ITEMS; ++i)
                data << (i < items->size() ? uint32((*items)[i]) : uint32(0));
        else
            for (std::size_t i = 0; i < MAX_GAMEOBJECT_QUEST_ITEMS; ++i)
                data << uint32(0);

        Send(&data);
        LOG_DEBUG("network", "WORLD: Sent SMSG_GAMEOBJECT_QUERY_RESPONSE");
    }
    else
    {
        LOG_DEBUG("network", "WORLD: CMSG_GAMEOBJECT_QUERY - Missing gameobject info for ({})", guid.ToString());
        WDataStore data (SMSG_GAMEOBJECT_QUERY_RESPONSE, 4);
        data << uint32(entry | 0x80000000);
        Send(&data);
        LOG_DEBUG("network", "WORLD: Sent SMSG_GAMEOBJECT_QUERY_RESPONSE");
    }
}

void User::HandleCorpseQueryOpcode(WDataStore& /*recvData*/)
{
    if (!m_player->HasCorpse())
    {
        WDataStore data(MSG_CORPSE_QUERY, 1);
        data << uint8(0);                                   // corpse not found
        Send(&data);
        return;
    }

    WorldLocation corpseLocation = m_player->GetCorpseLocation();
    uint32 corpseMapID = corpseLocation.GetMapId();
    uint32 mapID = corpseLocation.GetMapId();
    float x = corpseLocation.GetPositionX();
    float y = corpseLocation.GetPositionY();
    float z = corpseLocation.GetPositionZ();

    // if corpse at different map
    if (mapID != m_player->GetMapId())
    {
        // search entrance map for proper show entrance
        if (MapEntry const* corpseMapEntry = sMapStore.LookupEntry(mapID))
        {
            if (corpseMapEntry->IsDungeon() && corpseMapEntry->entrance_map >= 0)
            {
                // if corpse map have entrance
                if (Map const* entranceMap = sMapMgr->CreateBaseMap(corpseMapEntry->entrance_map))
                {
                    mapID = corpseMapEntry->entrance_map;
                    x = corpseMapEntry->entrance_x;
                    y = corpseMapEntry->entrance_y;
                    z = entranceMap->GetHeight(GetPlayer()->GetPhaseMask(), x, y, MAX_HEIGHT);
                }
            }
        }
    }

    WDataStore data(MSG_CORPSE_QUERY, 1 + (6 * 4));
    data << uint8(1);                                       // corpse found
    data << int32(mapID);
    data << float(x);
    data << float(y);
    data << float(z);
    data << int32(corpseMapID);
    data << uint32(0);                                      // unknown
    Send(&data);
}

void User::HandleNpcTextQueryOpcode(WDataStore& recvData)
{
    uint32 textID;
    WOWGUID guid;

    recvData >> textID;
    LOG_DEBUG("network", "WORLD: CMSG_NPC_TEXT_QUERY TextId: {}", textID);

    recvData >> guid;

    GossipText const* gossip = sObjectMgr->GetGossipText(textID);

    WDataStore data(SMSG_NPC_TEXT_UPDATE, 100);          // guess size
    data << textID;

    if (!gossip)
    {
        for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
        {
            data << float(0);
            data << "Greetings $N";
            data << "Greetings $N";
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
        }
    }
    else
    {
        std::string text0[MAX_GOSSIP_TEXT_OPTIONS], text1[MAX_GOSSIP_TEXT_OPTIONS];
        LocaleConstant locale = GetSessionDbLocaleIndex();

        for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
        {
            BroadcastText const* bct = sObjectMgr->GetBroadcastText(gossip->Options[i].BroadcastTextID);
            if (bct)
            {
                text0[i] = bct->GetText(locale, GENDER_MALE, true);
                text1[i] = bct->GetText(locale, GENDER_FEMALE, true);
            }
            else
            {
                text0[i] = gossip->Options[i].Text_0;
                text1[i] = gossip->Options[i].Text_1;
            }

            if (locale != DEFAULT_LOCALE && !bct)
            {
                if (NpcTextLocale const* npcTextLocale = sObjectMgr->GetNpcTextLocale(textID))
                {
                    ObjectMgr::GetLocaleString(npcTextLocale->Text_0[i], locale, text0[i]);
                    ObjectMgr::GetLocaleString(npcTextLocale->Text_1[i], locale, text1[i]);
                }
            }

            data << gossip->Options[i].Probability;

            if (text0[i].empty())
                data << text1[i];
            else
                data << text0[i];

            if (text1[i].empty())
                data << text0[i];
            else
                data << text1[i];

            data << gossip->Options[i].Language;

            for (uint8 j = 0; j < MAX_GOSSIP_TEXT_EMOTES; ++j)
            {
                data << gossip->Options[i].Emotes[j]._Delay;
                data << gossip->Options[i].Emotes[j]._Emote;
            }
        }
    }

    Send(&data);

    LOG_DEBUG("network", "WORLD: Sent SMSG_NPC_TEXT_UPDATE");
}

/// Only _static_ data is sent in this packet !!!
void User::HandlePageTextQueryOpcode(WDataStore& recvData)
{
    uint32 pageID;
    recvData >> pageID;
    recvData.read_skip<uint64>();                          // guid

    while (pageID)
    {
        PageText const* pageText = sObjectMgr->GetPageText(pageID);
        // guess size
        WDataStore data(SMSG_PAGE_TEXT_QUERY_RESPONSE, 50);
        data << pageID;

        if (!pageText)
        {
            data << "Item page missing.";
            data << uint32(0);
            pageID = 0;
        }
        else
        {
            std::string Text = pageText->Text;

            int loc_idx = GetSessionDbLocaleIndex();
            if (loc_idx >= 0)
                if (PageTextLocale const* player = sObjectMgr->GetPageTextLocale(pageID))
                    ObjectMgr::GetLocaleString(player->Text, loc_idx, Text);

            data << Text;
            data << uint32(pageText->NextPage);
            pageID = pageText->NextPage;
        }
        Send(&data);

        LOG_DEBUG("network", "WORLD: Sent SMSG_PAGE_TEXT_QUERY_RESPONSE");
    }
}

void User::HandleCorpseMapPositionQuery(WDataStore& recvData)
{
    LOG_DEBUG("network", "WORLD: Recv CMSG_CORPSE_MAP_POSITION_QUERY");

    uint32 corpseTransportGUID;
    recvData >> corpseTransportGUID;

    WDataStore data(SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE, 4 + 4 + 4 + 4);
    data << float(0);
    data << float(0);
    data << float(0);
    data << float(0);
    Send(&data);
}

void User::HandleQuestPOIQuery(WDataStore& recvData)
{
    uint32 count;
    recvData >> count; // quest count, max=25

    if (count > MAX_QUEST_LOG_SIZE)
    {
        recvData.rfinish();
        return;
    }

    // Read quest ids and add the in a unordered_set so we don't send POIs for the same quest multiple times
    std::unordered_set<uint32> questIds;
    for (uint32 i = 0; i < count; ++i)
        questIds.insert(recvData.read<uint32>()); // quest id

    WDataStore data(SMSG_QUEST_POI_QUERY_RESPONSE, 4 + (4 + 4)*questIds.size());
    data << uint32(questIds.size()); // count

    for (std::unordered_set<uint32>::const_iterator itr = questIds.begin(); itr != questIds.end(); ++itr)
    {
        uint32 questId = *itr;
        bool questOk = false;

        uint16 questSlot = m_player->FindQuestSlot(questId);

        if (questSlot != MAX_QUEST_LOG_SIZE)
            questOk = m_player->GetQuestSlotQuestId(questSlot) == questId;

        if (questOk)
        {
            QuestPOIVector const* POI = sObjectMgr->GetQuestPOIVector(questId);

            if (POI)
            {
                data << uint32(questId); // quest ID
                data << uint32(POI->size()); // POI count

                for (QuestPOIVector::const_iterator itr = POI->begin(); itr != POI->end(); ++itr)
                {
                    data << uint32(itr->Id);                // POI index
                    data << int32(itr->ObjectiveIndex);     // objective index
                    data << uint32(itr->MapId);             // mapid
                    data << uint32(itr->AreaId);            // areaid
                    data << uint32(itr->FloorId);           // floorid
                    data << uint32(itr->Unk3);              // unknown
                    data << uint32(itr->Unk4);              // unknown
                    data << uint32(itr->points.size());     // POI points count

                    for (std::vector<QuestPOIPoint>::const_iterator itr2 = itr->points.begin(); itr2 != itr->points.end(); ++itr2)
                    {
                        data << int32(itr2->x); // POI point x
                        data << int32(itr2->y); // POI point y
                    }
                }
            }
            else
            {
                data << uint32(questId); // quest ID
                data << uint32(0); // POI count
            }
        }
        else
        {
            data << uint32(questId); // quest ID
            data << uint32(0); // POI count
        }
    }

    Send(&data);
}
