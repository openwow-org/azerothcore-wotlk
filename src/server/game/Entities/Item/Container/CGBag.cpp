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

#include "CGBag.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "UpdateData.h"

CGBag::CGBag(): Item()
{
    m_objectType |= TYPEMASK_CONTAINER;
    m_objectTypeId = TYPEID_CONTAINER;

    m_valuesCount = CONTAINER_END;

    memset(m_bagslot, 0, sizeof(Item*) * MAX_BAG_SIZE);
}

CGBag::~CGBag()
{
    for (uint8 i = 0; i < MAX_BAG_SIZE; ++i)
        if (Item* item = m_bagslot[i])
        {
            if (item->IsInWorld())
            {
                LOG_FATAL("entities.item", "Item {} (slot {}, bag slot {}) in bag {} (slot {}, bag slot {}, m_bagslot {}) is to be deleted but is still in world.",
                               item->GetEntry(), (uint32)item->GetSlot(), (uint32)item->GetBagSlot(),
                               GetEntry(), (uint32)GetSlot(), (uint32)GetBagSlot(), (uint32)i);
                item->RemoveFromWorld();
            }
            delete m_bagslot[i];
        }
}

void CGBag::AddToWorld()
{
    Item::AddToWorld();

    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i])
            m_bagslot[i]->AddToWorld();
}

void CGBag::RemoveFromWorld()
{
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i])
            m_bagslot[i]->RemoveFromWorld();

    Item::RemoveFromWorld();
}

bool CGBag::Create(WOWGUID::LowType guidlow, uint32 itemid, Player const* owner)
{
    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(itemid);

    if (!itemProto || itemProto->ContainerSlots > MAX_BAG_SIZE)
        return false;

    Object::_Create(guidlow, 0, HighGuid::Item);

    SetEntry(itemid);
    SetObjectScale(1.0f);

    SetGuidValue(ITEM_FIELD_OWNER, owner ? owner->GetGUID() : WOWGUID::Empty);
    SetGuidValue(ITEM_FIELD_CONTAINED, owner ? owner->GetGUID() : WOWGUID::Empty);

    SetUInt32Value(ITEM_FIELD_MAXDURABILITY, itemProto->MaxDurability);
    SetUInt32Value(ITEM_FIELD_DURABILITY, itemProto->MaxDurability);
    SetUInt32Value(ITEM_FIELD_STACK_COUNT, 1);

    // Setting the number of Slots the Container has
    SetUInt32Value(CONTAINER_FIELD_NUM_SLOTS, itemProto->ContainerSlots);

    // Cleaning 20 slots
    for (uint8 i = 0; i < MAX_BAG_SIZE; ++i)
    {
        SetGuidValue(CONTAINER_FIELD_SLOT_1 + (i * 2), WOWGUID::Empty);
        m_bagslot[i] = nullptr;
    }

    return true;
}

void CGBag::SaveToDB(CharacterDatabaseTransaction trans)
{
    Item::SaveToDB(trans);
}

bool CGBag::LoadFromDB(WOWGUID::LowType guid, WOWGUID owner_guid, Field* fields, uint32 entry)
{
    if (!Item::LoadFromDB(guid, owner_guid, fields, entry))
        return false;

    ItemTemplate const* itemProto = GetTemplate(); // checked in Item::LoadFromDB
    SetUInt32Value(CONTAINER_FIELD_NUM_SLOTS, itemProto->ContainerSlots);
    // cleanup bag content related item value fields (its will be filled correctly from `character_inventory`)
    for (uint8 i = 0; i < MAX_BAG_SIZE; ++i)
    {
        SetGuidValue(CONTAINER_FIELD_SLOT_1 + (i * 2), WOWGUID::Empty);
        delete m_bagslot[i];
        m_bagslot[i] = nullptr;
    }

    return true;
}

void CGBag::DeleteFromDB(CharacterDatabaseTransaction trans)
{
    for (uint8 i = 0; i < MAX_BAG_SIZE; ++i)
        if (m_bagslot[i])
            m_bagslot[i]->DeleteFromDB(trans);

    Item::DeleteFromDB(trans);
}

uint32 CGBag::GetFreeSlots() const
{
    uint32 slots = 0;
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (!m_bagslot[i])
            ++slots;

    return slots;
}

void CGBag::RemoveItem(uint8 slot, bool /*update*/)
{
    ASSERT(slot < MAX_BAG_SIZE);

    if (m_bagslot[slot])
        m_bagslot[slot]->SetContainer(nullptr);

    m_bagslot[slot] = nullptr;
    SetGuidValue(CONTAINER_FIELD_SLOT_1 + (slot * 2), WOWGUID::Empty);
}

void CGBag::StoreItem(uint8 slot, Item* pItem, bool /*update*/)
{
    ASSERT(slot < MAX_BAG_SIZE);

    if (pItem && pItem->GetGUID() != this->GetGUID())
    {
        m_bagslot[slot] = pItem;
        SetGuidValue(CONTAINER_FIELD_SLOT_1 + (slot * 2), pItem->GetGUID());
        pItem->SetGuidValue(ITEM_FIELD_CONTAINED, GetGUID());
        pItem->SetGuidValue(ITEM_FIELD_OWNER, GetOwnerGUID());
        pItem->SetContainer(this);
        pItem->SetSlot(slot);
    }
}

void CGBag::BuildCreateUpdateBlockForPlayer(UpdateData* data, Player* target)
{
    Item::BuildCreateUpdateBlockForPlayer(data, target);

    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i])
            m_bagslot[i]->BuildCreateUpdateBlockForPlayer(data, target);
}

// If the bag is empty returns true
bool CGBag::IsEmpty() const
{
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i])
            return false;

    return true;
}

uint32 CGBag::GetItemCount(uint32 item, Item* eItem) const
{
    Item* pItem;
    uint32 count = 0;
    for (uint32 i = 0; i < GetBagSize(); ++i)
    {
        pItem = m_bagslot[i];
        if (pItem && pItem != eItem && pItem->GetEntry() == item)
            count += pItem->GetCount();
    }

    if (eItem && eItem->GetTemplate()->GemProperties)
    {
        for (uint32 i = 0; i < GetBagSize(); ++i)
        {
            pItem = m_bagslot[i];
            if (pItem && pItem != eItem && pItem->GetTemplate()->Socket[0].Color)
                count += pItem->GetGemCountWithID(item);
        }
    }

    return count;
}

uint32 CGBag::GetItemCountWithLimitCategory(uint32 limitCategory, Item* skipItem) const
{
    uint32 count = 0;
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (Item* pItem = m_bagslot[i])
            if (pItem != skipItem)
                if (ItemTemplate const* pProto = pItem->GetTemplate())
                    if (pProto->ItemLimitCategory == limitCategory)
                        count += m_bagslot[i]->GetCount();

    return count;
}

uint8 CGBag::GetSlotByItemGUID(WOWGUID guid) const
{
    for (uint32 i = 0; i < GetBagSize(); ++i)
        if (m_bagslot[i] != 0)
            if (m_bagslot[i]->GetGUID() == guid)
                return i;

    return NULL_SLOT;
}

Item* CGBag::GetItemByPos(uint8 slot) const
{
    if (slot < GetBagSize())
        return m_bagslot[slot];

    return nullptr;
}

std::string CGBag::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << Item::GetDebugInfo();
    return sstr.str();
}
