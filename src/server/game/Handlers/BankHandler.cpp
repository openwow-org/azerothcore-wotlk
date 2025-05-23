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

#include "BankPackets.h"
#include "DBCStores.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "WDataStore.h"
#include "User.h"

bool User::CanUseBank(WOWGUID bankerGUID) const
{
    // bankerGUID parameter is optional, set to 0 by default.
    if (!bankerGUID)
        bankerGUID = m_currentBankerGUID;

    bool isUsingBankCommand = (bankerGUID == GetPlayer()->GetGUID() && bankerGUID == m_currentBankerGUID);

    if (!isUsingBankCommand)
    {
        Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(bankerGUID, UNIT_NPC_FLAG_BANKER);
        if (!creature)
            return false;
    }

    return true;
}

void User::HandleBankerActivateOpcode(WDataStore& recvData)
{
    WOWGUID guid;

    recvData >> guid;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_BANKER);
    if (!unit)
    {
        LOG_DEBUG("network", "WORLD: HandleBankerActivateOpcode - Unit ({}) not found or you can not interact with him.", guid.ToString());
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendShowBank(guid);
}

void User::HandleAutoBankItemOpcode(WorldPackets::Bank::AutoBankItem& packet)
{
    LOG_DEBUG("network", "STORAGE: receive bag = {}, slot = {}", packet.Bag, packet.Slot);

    if (!CanUseBank())
    {
        LOG_DEBUG("network", "WORLD: HandleAutoBankItemOpcode - Unit ({}) not found or you can't interact with him.", m_currentBankerGUID.ToString());
        return;
    }

    Item* item = m_player->GetItemByPos(packet.Bag, packet.Slot);
    if (!item)
        return;

    ItemPosCountVec dest;
    BAG_RESULT msg = m_player->CanBankItem(NULL_BAG, NULL_SLOT, dest, item, false);
    if (msg != BAG_OK)
    {
        m_player->SendInventoryChangeFailure(msg, item, nullptr);
        return;
    }

    if (dest.size() == 1 && dest[0].pos == item->GetPos())
    {
        m_player->SendInventoryChangeFailure(EQUIP_ERR_NONE, item, nullptr);
        return;
    }

    m_player->RemoveItem(packet.Bag, packet.Slot, true);
    m_player->ItemRemovedQuestCheck(item->GetEntry(), item->GetCount());
    m_player->BankItem(dest, item, true);
    m_player->UpdateTitansGrip();
}

void User::HandleAutoStoreBankItemOpcode(WorldPackets::Bank::AutoStoreBankItem& packet)
{
    LOG_DEBUG("network", "STORAGE: receive bag = {}, slot = {}", packet.Bag, packet.Slot);

    if (!CanUseBank())
    {
        LOG_DEBUG("network", "WORLD: HandleAutoStoreBankItemOpcode - Unit ({}) not found or you can't interact with him.", m_currentBankerGUID.ToString());
        return;
    }

    Item* item = m_player->GetItemByPos(packet.Bag, packet.Slot);
    if (!item)
        return;

    if (m_player->IsBankPos(packet.Bag, packet.Slot))                    // moving from bank to inventory
    {
        ItemPosCountVec dest;
        BAG_RESULT msg = m_player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false);
        if (msg != BAG_OK)
        {
            m_player->SendInventoryChangeFailure(msg, item, nullptr);
            return;
        }

        m_player->RemoveItem(packet.Bag, packet.Slot, true);
        if (Item const* storedItem = m_player->StoreItem(dest, item, true))
            m_player->ItemAddedQuestCheck(storedItem->GetEntry(), storedItem->GetCount());
    }
    else                                                    // moving from inventory to bank
    {
        ItemPosCountVec dest;
        BAG_RESULT msg = m_player->CanBankItem(NULL_BAG, NULL_SLOT, dest, item, false);
        if (msg != BAG_OK)
        {
            m_player->SendInventoryChangeFailure(msg, item, nullptr);
            return;
        }

        m_player->RemoveItem(packet.Bag, packet.Slot, true);
        m_player->ItemRemovedQuestCheck(item->GetEntry(), item->GetCount());
        m_player->BankItem(dest, item, true);
        m_player->UpdateTitansGrip();
    }
}

void User::HandleBuyBankSlotOpcode(WorldPackets::Bank::BuyBankSlot& buyBankSlot)
{
    WorldPackets::Bank::BuyBankSlotResult packet;
    if (!CanUseBank(buyBankSlot.Banker))
    {
        packet.Result = ERR_BANKSLOT_NOTBANKER;
        Send(packet.Write());
        LOG_DEBUG("network", "WORLD: HandleBuyBankSlotOpcode - {} not found or you can't interact with him.", buyBankSlot.Banker.ToString());
        return;
    }

    uint32 slot = m_player->GetBankBagSlotCount();

    // next slot
    ++slot;

    LOG_INFO("network", "PLAYER: Buy bank bag slot, slot number = {}", slot);

    BankBagSlotPricesEntry const* slotEntry = sBankBagSlotPricesStore.LookupEntry(slot);

    if (!slotEntry)
    {
        packet.Result = ERR_BANKSLOT_FAILED_TOO_MANY;
        Send(packet.Write());
        return;
    }

    uint32 price = slotEntry->price;

    if (!m_player->HasEnoughMoney(price))
    {
        packet.Result = ERR_BANKSLOT_INSUFFICIENT_FUNDS;
        Send(packet.Write());
        return;
    }

    m_player->SetBankBagSlotCount(slot);
    m_player->ModifyMoney(-int32(price));

    packet.Result = ERR_BANKSLOT_OK;
    Send(packet.Write());

    m_player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BUY_BANK_SLOT);
}

void User::SendShowBank(WOWGUID guid)
{
    m_currentBankerGUID = guid;
    WorldPackets::Bank::ShowBank packet;
    packet.Banker = guid;
    Send(packet.Write());
}
