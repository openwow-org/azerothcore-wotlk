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

#include "Corpse.h"
#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "LootItemStorage.h"
#include "LootMgr.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WDataStore.h"
#include "User.h"

void User::HandleAutostoreLootItemOpcode(WDataStore& recvData)
{
    LOG_DEBUG("network", "WORLD: CMSG_AUTOSTORE_LOOT_ITEM");
    Player* player = GetPlayer();
    WOWGUID lguid = player->GetLootGUID();
    Loot* loot = nullptr;
    uint8 lootSlot = 0;

    recvData >> lootSlot;

    if (lguid.IsGameObject())
    {
        GameObject* go = player->GetMap()->GetGameObject(lguid);
        // xinef: cheating protection
        //if (player->GetGroup() && player->GetGroup()->GetLootMethod() == MASTER_LOOT && player->GetGUID() != player->GetGroup()->GetMasterLooterGuid())
        //    go = nullptr;

        // not check distance for GO in case owned GO (fishing bobber case, for example) or Fishing hole GO
        if (!go || ((go->GetOwnerGUID() != m_player->GetGUID() && go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE) && !go->IsWithinDistInMap(m_player)))
        {
            player->SendLootRelease(lguid);
            return;
        }

        loot = &go->loot;
    }
    else if (lguid.IsItem())
    {
        Item* pItem = player->GetItemByGuid(lguid);

        if (!pItem)
        {
            player->SendLootRelease(lguid);
            return;
        }

        loot = &pItem->loot;
    }
    else if (lguid.IsCorpse())
    {
        Corpse* bones = ObjectAccessor::GetCorpse(*player, lguid);
        if (!bones)
        {
            player->SendLootRelease(lguid);
            return;
        }

        loot = &bones->loot;
    }
    else
    {
        Creature* creature = GetPlayer()->GetMap()->GetCreature(lguid);

        bool lootAllowed = creature && creature->IsAlive() == (player->IsClass(CLASS_ROGUE, CLASS_CONTEXT_ABILITY) && creature->loot.loot_type == LOOT_PICKPOCKETING);
        if (!lootAllowed || !creature->IsWithinDistInMap(m_player, INTERACTION_DISTANCE))
        {
            player->SendLootError(lguid, lootAllowed ? LOOT_ERROR_TOO_FAR : LOOT_ERROR_DIDNT_KILL);
            return;
        }

        loot = &creature->loot;
    }

    sScriptMgr->OnAfterCreatureLoot(player);

    BAG_RESULT msg;
    LootItem* lootItem = player->StoreLootItem(lootSlot, loot, msg);
    if (msg != BAG_OK && lguid.IsItem() && loot->loot_type != LOOT_CORPSE)
    {
        lootItem->is_looted = true;
        loot->NotifyItemRemoved(lootItem->itemIndex);
        loot->unlootedCount--;

        player->SendItemRetrievalMail(lootItem->itemid, lootItem->count);
    }

    // If player is removing the last LootItem, delete the empty container.
    if (loot->isLooted() && lguid.IsItem())
        DoLootRelease(lguid);
}

void User::HandleLootMoneyOpcode(WDataStore& /*recvData*/)
{
    LOG_DEBUG("network", "WORLD: CMSG_LOOT_MONEY");

    Player* player = GetPlayer();
    WOWGUID guid = player->GetLootGUID();
    if (!guid)
        return;

    Loot* loot = nullptr;
    bool shareMoney = true;

    switch (guid.GetHigh())
    {
        case HighGuid::GameObject:
            {
                GameObject* go = GetPlayer()->GetMap()->GetGameObject(guid);

                // do not check distance for GO if player is the owner of it (ex. fishing bobber)
                if (go && ((go->GetOwnerGUID() == player->GetGUID() || go->IsWithinDistInMap(player))))
                {
                    loot = &go->loot;
                }

                break;
            }
        case HighGuid::Corpse:                               // remove insignia ONLY in BG
            {
                Corpse* bones = ObjectAccessor::GetCorpse(*player, guid);

                if (bones && bones->IsWithinDistInMap(player, INTERACTION_DISTANCE))
                {
                    loot = &bones->loot;
                    shareMoney = false;
                }

                break;
            }
        case HighGuid::Item:
            {
                if (Item* item = player->GetItemByGuid(guid))
                {
                    loot = &item->loot;
                    shareMoney = false;
                }
                break;
            }
        case HighGuid::Unit:
        case HighGuid::Vehicle:
            {
                Creature* creature = player->GetMap()->GetCreature(guid);
                bool lootAllowed = creature && creature->IsAlive() == (player->IsClass(CLASS_ROGUE, CLASS_CONTEXT_ABILITY) && creature->loot.loot_type == LOOT_PICKPOCKETING);
                if (lootAllowed && creature->IsWithinDistInMap(player, INTERACTION_DISTANCE))
                {
                    loot = &creature->loot;
                    if (creature->IsAlive())
                        shareMoney = false;
                }
                else
                    player->SendLootError(guid, lootAllowed ? LOOT_ERROR_TOO_FAR : LOOT_ERROR_DIDNT_KILL);
                break;
            }
        default:
            return;                                         // unlootable type
    }

    if (loot)
    {
        sScriptMgr->OnBeforeLootMoney(player, loot);
        loot->NotifyMoneyRemoved();
        if (shareMoney && player->GetGroup())      //item, pickpocket and players can be looted only single player
        {
            Group* group = player->GetGroup();

            std::vector<Player*> playersNear;
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (!member)
                    continue;

                if (player->IsAtLootRewardDistance(member))
                    playersNear.push_back(member);
            }

            uint32 goldPerPlayer = uint32((loot->gold) / (playersNear.size()));

            for (std::vector<Player*>::const_iterator i = playersNear.begin(); i != playersNear.end(); ++i)
            {
                (*i)->ModifyMoney(goldPerPlayer);
                (*i)->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, goldPerPlayer);

                WDataStore data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
                data << uint32(goldPerPlayer);
                data << uint8(playersNear.size() > 1 ? 0 : 1);     // Controls the text displayed in chat. 0 is "Your share is..." and 1 is "You loot..."
                (*i)->User()->Send(&data);
            }
        }
        else
        {
            sScriptMgr->OnAfterCreatureLootMoney(player);
            player->ModifyMoney(loot->gold);
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, loot->gold);

            WDataStore data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
            data << uint32(loot->gold);
            data << uint8(1);   // "You loot..."
            Send(&data);
        }

        sScriptMgr->OnLootMoney(player, loot->gold);

        loot->gold = 0;

        // Delete the money loot record from the DB
        if (loot->containerGUID)
            sLootItemStorage->RemoveStoredLootMoney(loot->containerGUID, loot);

        // Delete container if empty
        if (loot->isLooted() && guid.IsItem())
            DoLootRelease(guid);
    }
}

void User::HandleLootOpcode(WDataStore& recvData)
{
    LOG_DEBUG("network", "WORLD: CMSG_LOOT");

    WOWGUID guid;
    recvData >> guid;

    // Check possible cheat
    if (!GetPlayer()->IsAlive() || !guid.IsCreatureOrVehicle())
        return;

    // interrupt cast
    if (GetPlayer()->IsNonMeleeSpellCast(false))
        GetPlayer()->InterruptNonMeleeSpells(false);

    GetPlayer()->SendLoot(guid, LOOT_CORPSE);
}

void User::HandleLootReleaseOpcode(WDataStore& recvData)
{
    LOG_DEBUG("network", "WORLD: CMSG_LOOT_RELEASE");

    // cheaters can modify lguid to prevent correct apply loot release code and re-loot
    // use internal stored guid
    WOWGUID guid;
    recvData >> guid;

    if (WOWGUID lguid = GetPlayer()->GetLootGUID())
        if (lguid == guid)
            DoLootRelease(lguid);
}

void User::DoLootRelease(WOWGUID lguid)
{
    Player*  player = GetPlayer();
    Loot*    loot;

    player->SetLootGUID(WOWGUID::Empty);
    player->SendLootRelease(lguid);

    player->RemoveUnitFlag(UNIT_FLAG_LOOTING);

    if (!player->IsInWorld())
        return;

    if (lguid.IsGameObject())
    {
        GameObject* go = GetPlayer()->GetMap()->GetGameObject(lguid);

        // not check distance for GO in case owned GO (fishing bobber case, for example) or Fishing hole GO
        if (!go || ((go->GetOwnerGUID() != m_player->GetGUID() && go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE) && !go->IsWithinDistInMap(m_player)))
        {
            return;
        }

        loot = &go->loot;

        if (go->GetGoType() == GAMEOBJECT_TYPE_DOOR)
        {
            // locked doors are opened with spelleffect openlock, prevent remove its as looted
            go->UseDoorOrButton();
        }
        else if (loot->isLooted() || go->GetGoType() == GAMEOBJECT_TYPE_FISHINGNODE)
        {
            if (go->GetGoType() == GAMEOBJECT_TYPE_FISHINGHOLE)
            {
                // The fishing hole used once more
                go->AddUse();                               // if the max usage is reached, will be despawned in next tick
                if (go->GetUseCount() >= go->GetGOValue()->FishingHole.MaxOpens)
                    go->SetLootState(GO_JUST_DEACTIVATED);
                else
                    go->SetLootState(GO_READY);
            }
            else
            {
                go->SetLootState(GO_JUST_DEACTIVATED);

                // Xinef: moved event execution to loot release (after everything is looted)
                // Xinef: 99% sure that this worked like this on blizz
                // Xinef: prevents exploits with just opening GO and spawning bilions of npcs, which can crash core if you know what you're doin ;)
                if (go->GetGoType() == GAMEOBJECT_TYPE_CHEST && go->GetGOInfo()->chest.eventId)
                {
                    LOG_DEBUG("spells.aura", "Chest ScriptStart id {} for GO {}", go->GetGOInfo()->chest.eventId, go->GetSpawnId());
                    player->GetMap()->ScriptsStart(sEventScripts, go->GetGOInfo()->chest.eventId, player, go);
                }
            }

            loot->clear();
        }
        else
        {
            // not fully looted object
            go->SetLootState(GO_ACTIVATED, player);

            // if the round robin player release, reset it.
            if (player->GetGUID() == loot->roundRobinPlayer)
                loot->roundRobinPlayer.Clear();
        }
    }
    else if (lguid.IsCorpse())        // ONLY remove insignia at BG
    {
        Corpse* corpse = ObjectAccessor::GetCorpse(*player, lguid);
        if (!corpse || !corpse->IsWithinDistInMap(m_player, INTERACTION_DISTANCE))
            return;

        loot = &corpse->loot;

        // Xinef: Buggs client? (Opening loot after closing)
        //if (loot->isLooted())
        {
            loot->clear();
            corpse->RemoveFlag(CORPSE_FIELD_DYNAMIC_FLAGS, CORPSE_DYNFLAG_LOOTABLE);
        }
    }
    else if (lguid.IsItem())
    {
        Item* pItem = player->GetItemByGuid(lguid);
        if (!pItem)
            return;

        loot = &pItem->loot;
        ItemTemplate const* proto = pItem->GetTemplate();

        // destroy only 5 items from stack in case prospecting and milling
        if (proto->Flags & (ITEM_FLAG_IS_PROSPECTABLE | ITEM_FLAG_IS_MILLABLE))
        {
            pItem->m_lootGenerated = false;
            pItem->loot.clear();

            uint32 count = pItem->GetCount();

            // >=5 checked in spell code, but will work for cheating cases also with removing from another stacks.
            if (count > 5)
                count = 5;

            player->DestroyItemCount(pItem, count, true);
        }
        else if (pItem->loot.isLooted() || !(proto->Flags & ITEM_FLAG_HAS_LOOT))
        {
            player->DestroyItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
            return;
        }
    }
    else
    {
        Creature* creature = GetPlayer()->GetMap()->GetCreature(lguid);

        bool lootAllowed = creature && creature->IsAlive() == (player->IsClass(CLASS_ROGUE, CLASS_CONTEXT_ABILITY) && creature->loot.loot_type == LOOT_PICKPOCKETING);
        if (!lootAllowed || !creature->IsWithinDistInMap(m_player, INTERACTION_DISTANCE))
            return;

        loot = &creature->loot;
        if (loot->isLooted())
        {
            // skip pickpocketing loot for speed, skinning timer reduction is no-op in fact
            if (!creature->IsAlive())
                creature->AllLootRemovedFromCorpse();

            creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
            loot->clear();
        }
        else
        {
            // if the round robin player release, reset it.
            if (player->GetGUID() == loot->roundRobinPlayer)
            {
                loot->roundRobinPlayer.Clear();

                if (Group* group = player->GetGroup())
                    group->SendLooter(creature, nullptr);
            }
            // force dynflag update to update looter and lootable info
            creature->ForceValuesUpdateAtIndex(UNIT_DYNAMIC_FLAGS);
        }
    }

    //Player is not looking at loot list, he doesn't need to see updates on the loot list
    if (!lguid.IsItem())
    {
        loot->RemoveLooter(player->GetGUID());
    }
}

void User::HandleLootMasterGiveOpcode(WDataStore& recvData)
{
    uint8 slotid;
    WOWGUID lootguid, target_playerguid;

    recvData >> lootguid >> slotid >> target_playerguid;

    if (!m_player->GetGroup() || m_player->GetGroup()->GetMasterLooterGuid() != m_player->GetGUID() || m_player->GetGroup()->GetLootMethod() != MASTER_LOOT)
    {
        m_player->SendLootError(lootguid, LOOT_ERROR_DIDNT_KILL);
        return;
    }

    Player* target = ObjectAccessor::GetPlayer(*m_player, target_playerguid);
    if (!target)
    {
        m_player->SendLootError(lootguid, LOOT_ERROR_PLAYER_NOT_FOUND);
        return;
    }

    LOG_DEBUG("network", "User::HandleLootMasterGiveOpcode (CMSG_LOOT_MASTER_GIVE, 0x02A3) Target = [{}].", target->GetName());

    if (m_player->GetLootGUID() != lootguid)
    {
        m_player->SendLootError(lootguid, LOOT_ERROR_DIDNT_KILL);
        return;
    }

    if (!m_player->IsInRaidWith(target))
    {
        m_player->SendLootError(lootguid, LOOT_ERROR_MASTER_OTHER);
        //LOG_DEBUG("network", "MasterLootItem: Player {} tried to give an item to ineligible player {} !", GetPlayer()->GetName(), target->GetName());
        return;
    }

    Loot* loot = nullptr;

    if (GetPlayer()->GetLootGUID().IsCreatureOrVehicle())
    {
        Creature* creature = GetPlayer()->GetMap()->GetCreature(lootguid);
        if (!creature)
            return;

        loot = &creature->loot;
    }
    else if (GetPlayer()->GetLootGUID().IsGameObject())
    {
        GameObject* pGO = GetPlayer()->GetMap()->GetGameObject(lootguid);
        if (!pGO)
            return;

        loot = &pGO->loot;
    }

    if (!loot)
        return;

    if (slotid >= loot->items.size() + loot->quest_items.size())
    {
        LOG_DEBUG("loot", "MasterLootItem: Player {} might be using a hack! (slot {}, size {})", GetPlayer()->GetName(), slotid, (unsigned long)loot->items.size());
        return;
    }

    LootItem& item = slotid >= loot->items.size() ? loot->quest_items[slotid - loot->items.size()] : loot->items[slotid];

    ItemPosCountVec dest;
    BAG_RESULT msg = target->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item.itemid, item.count);
    if (!item.AllowedForPlayer(target, loot->sourceWorldObjectGUID))
        msg = EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM;
    if (msg != BAG_OK)
    {
        if (msg == EQUIP_ERR_CANT_CARRY_MORE_OF_THIS)
            m_player->SendLootError(lootguid, LOOT_ERROR_MASTER_UNIQUE_ITEM);
        else if (msg == EQUIP_ERR_INVENTORY_FULL)
            m_player->SendLootError(lootguid, LOOT_ERROR_MASTER_INV_FULL);
        else
            m_player->SendLootError(lootguid, LOOT_ERROR_MASTER_OTHER);

        return;
    }

    // list of players allowed to receive this item in trade
    AllowedLooterSet looters = item.GetAllowedLooters();

    // not move item from loot to target inventory
    Item* newitem = target->StoreNewItem(dest, item.itemid, true, item.randomPropertyId, looters);
    target->SendItemPush(newitem, uint32(item.count), false, false, true);
    target->UpdateLootAchievements(&item, loot);

    // mark as looted
    item.count = 0;
    item.is_looted = true;

    loot->NotifyItemRemoved(slotid);
    --loot->unlootedCount;
}
