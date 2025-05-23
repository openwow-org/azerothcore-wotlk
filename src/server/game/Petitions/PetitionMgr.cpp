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

#include "PetitionMgr.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "QueryResult.h"
#include "Timer.h"

PetitionMgr::PetitionMgr()
{
}

PetitionMgr::~PetitionMgr()
{
}

PetitionMgr* PetitionMgr::instance()
{
    static PetitionMgr instance;
    return &instance;
}

void PetitionMgr::LoadPetitions()
{
    uint32 oldMSTime = getMSTime();
    PetitionStore.clear();

    QueryResult result = CharacterDatabase.Query("SELECT ownerguid, petitionguid, name, type FROM petition");
    if (!result)
    {
        LOG_WARN("server.loading", ">> Loaded 0 Petitions!");
        LOG_INFO("server.loading", " ");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        AddPetition(WOWGUID::Create<HighGuid::Item>(fields[1].Get<uint32>()), WOWGUID::Create<HighGuid::Player>(fields[0].Get<uint32>()), fields[2].Get<std::string>(), fields[3].Get<uint8>());
        ++count;
    } while (result->NextRow());

    LOG_INFO("server.loading", ">> Loaded {} Petitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    LOG_INFO("server.loading", " ");
}

void PetitionMgr::LoadSignatures()
{
    uint32 oldMSTime = getMSTime();
    SignatureStore.clear();

    QueryResult result = CharacterDatabase.Query("SELECT petitionguid, playerguid, player_account FROM petition_sign");
    if (!result)
    {
        LOG_WARN("server.loading", ">> Loaded 0 Petition signs!");
        LOG_INFO("server.loading", " ");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        AddSignature(WOWGUID::Create<HighGuid::Item>(fields[0].Get<uint32>()), fields[2].Get<uint32>(), WOWGUID::Create<HighGuid::Player>(fields[1].Get<uint32>()));
        ++count;
    } while (result->NextRow());

    LOG_INFO("server.loading", ">> Loaded {} Petition signs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    LOG_INFO("server.loading", " ");
}

void PetitionMgr::AddPetition(WOWGUID petitionGUID, WOWGUID ownerGuid, std::string const& name, uint8 type)
{
    Petition& p = PetitionStore[petitionGUID];
    p.petitionGuid = petitionGUID;
    p.ownerGuid = ownerGuid;
    p.petitionName = name;
    p.petitionType = type;

    Signatures& s = SignatureStore[petitionGUID];
    s.petitionGuid = petitionGUID;
    s.signatureMap.clear();
}

void PetitionMgr::RemovePetition(WOWGUID petitionGUID)
{
    PetitionStore.erase(petitionGUID);

    // remove signatures
    SignatureStore.erase(petitionGUID);
}

void PetitionMgr::RemovePetitionByOwnerAndType(WOWGUID ownerGuid, uint8 type)
{
    for (PetitionContainer::iterator itr = PetitionStore.begin(); itr != PetitionStore.end();)
    {
        if (itr->second.ownerGuid == ownerGuid && (!type || type == itr->second.petitionType))
        {
            // Remove invalid charter item
            if (type == itr->second.petitionType)
            {
                if (Player* owner = ObjectAccessor::FindConnectedPlayer(ownerGuid))
                {
                    if (Item* item = owner->GetItemByGuid(itr->first))
                    {
                        owner->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
                    }
                }
            }

            // remove signatures
            SignatureStore.erase(itr->first);
            PetitionStore.erase(itr++);
        }
        else
            ++itr;
    }
}

Petition const* PetitionMgr::GetPetition(WOWGUID petitionGUID) const
{
    PetitionContainer::const_iterator itr = PetitionStore.find(petitionGUID);
    if (itr != PetitionStore.end())
        return &itr->second;
    return nullptr;
}

Petition const* PetitionMgr::GetPetitionByOwnerWithType(WOWGUID ownerGuid, uint8 type) const
{
    for (PetitionContainer::const_iterator itr = PetitionStore.begin(); itr != PetitionStore.end(); ++itr)
        if (itr->second.ownerGuid == ownerGuid && itr->second.petitionType == type)
            return &itr->second;

    return nullptr;
}

void PetitionMgr::AddSignature(WOWGUID petitionGUID, uint32 accountId, WOWGUID playerGuid)
{
    Signatures& s = SignatureStore[petitionGUID];
    s.signatureMap[playerGuid] = accountId;
}

Signatures const* PetitionMgr::GetSignature(WOWGUID petitionGUID) const
{
    SignatureContainer::const_iterator itr = SignatureStore.find(petitionGUID);
    if (itr != SignatureStore.end())
        return &itr->second;
    return nullptr;
}

void PetitionMgr::RemoveSignaturesByPlayer(WOWGUID playerGuid)
{
    for (SignatureContainer::iterator itr = SignatureStore.begin(); itr != SignatureStore.end(); ++itr)
    {
        SignatureMap::iterator signItr = itr->second.signatureMap.find(playerGuid);
        if (signItr != itr->second.signatureMap.end())
            itr->second.signatureMap.erase(signItr);
    }
}

void PetitionMgr::RemoveSignaturesByPlayerAndType(WOWGUID playerGuid, uint8 type)
{
    for (SignatureContainer::iterator itr = SignatureStore.begin(); itr != SignatureStore.end(); ++itr)
    {
        Petition const* petition = sPetitionMgr->GetPetition(itr->first);
        if (!petition || petition->petitionType != type)
            continue;

        SignatureMap::iterator signItr = itr->second.signatureMap.find(playerGuid);
        if (signItr != itr->second.signatureMap.end())
            itr->second.signatureMap.erase(signItr);
    }
}
