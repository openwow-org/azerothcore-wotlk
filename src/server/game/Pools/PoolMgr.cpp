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

#include "PoolMgr.h"
#include "Containers.h"
#include "Log.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "Transport.h"

////////////////////////////////////////////////////////////
// template class ActivePoolData

// Method that tell amount spawned objects/subpools
uint32 ActivePoolData::GetActiveObjectCount(uint32 pool_id) const
{
    ActivePoolPools::const_iterator itr = mSpawnedPools.find(pool_id);
    return itr != mSpawnedPools.end() ? itr->second : 0;
}

// Method that tell if a creature is spawned currently
template<>
bool ActivePoolData::IsActiveObject<Creature>(uint32 db_guid) const
{
    return mSpawnedCreatures.find(db_guid) != mSpawnedCreatures.end();
}

// Method that tell if a gameobject is spawned currently
template<>
bool ActivePoolData::IsActiveObject<GameObject>(uint32 db_guid) const
{
    return mSpawnedGameobjects.find(db_guid) != mSpawnedGameobjects.end();
}

// Method that tell if a pool is spawned currently
template<>
bool ActivePoolData::IsActiveObject<Pool>(uint32 sub_pool_id) const
{
    return mSpawnedPools.find(sub_pool_id) != mSpawnedPools.end();
}

// Method that tell if a quest can be started
template<>
bool ActivePoolData::IsActiveObject<Quest>(uint32 quest_id) const
{
    return mActiveQuests.find(quest_id) != mActiveQuests.end();
}

template<>
void ActivePoolData::ActivateObject<Creature>(uint32 db_guid, uint32 pool_id)
{
    mSpawnedCreatures.insert(db_guid);
    ++mSpawnedPools[pool_id];
}

template<>
void ActivePoolData::ActivateObject<GameObject>(uint32 db_guid, uint32 pool_id)
{
    mSpawnedGameobjects.insert(db_guid);
    ++mSpawnedPools[pool_id];
}

template<>
void ActivePoolData::ActivateObject<Pool>(uint32 sub_pool_id, uint32 pool_id)
{
    mSpawnedPools[sub_pool_id] = 0;
    ++mSpawnedPools[pool_id];
}

template<>
void ActivePoolData::ActivateObject<Quest>(uint32 quest_id, uint32 pool_id)
{
    mActiveQuests.insert(quest_id);
    ++mSpawnedPools[pool_id];
}

template<>
void ActivePoolData::RemoveObject<Creature>(uint32 db_guid, uint32 pool_id)
{
    mSpawnedCreatures.erase(db_guid);
    uint32& val = mSpawnedPools[pool_id];
    if (val > 0)
        --val;
}

template<>
void ActivePoolData::RemoveObject<GameObject>(uint32 db_guid, uint32 pool_id)
{
    mSpawnedGameobjects.erase(db_guid);
    uint32& val = mSpawnedPools[pool_id];
    if (val > 0)
        --val;
}

template<>
void ActivePoolData::RemoveObject<Pool>(uint32 sub_pool_id, uint32 pool_id)
{
    mSpawnedPools.erase(sub_pool_id);
    uint32& val = mSpawnedPools[pool_id];
    if (val > 0)
        --val;
}

template<>
void ActivePoolData::RemoveObject<Quest>(uint32 quest_id, uint32 pool_id)
{
    mActiveQuests.erase(quest_id);
    uint32& val = mSpawnedPools[pool_id];
    if (val > 0)
        --val;
}

////////////////////////////////////////////////////////////
// Methods of template class PoolGroup

// Method to add a gameobject/creature guid to the proper list depending on pool type and chance value
template <class T>
void PoolGroup<T>::AddEntry(PoolObject& poolitem, uint32 maxentries)
{
    if (poolitem.chance != 0 && maxentries == 1)
        ExplicitlyChanced.push_back(poolitem);
    else
        EqualChanced.push_back(poolitem);
}

// Method to check the chances are proper in this object pool
template <class T>
bool PoolGroup<T>::CheckPool() const
{
    if (EqualChanced.empty())
    {
        float chance = 0;
        for (uint32 i = 0; i < ExplicitlyChanced.size(); ++i)
            chance += ExplicitlyChanced[i].chance;
        if (chance != 100 && chance != 0)
            return false;
    }
    return true;
}

// Main method to despawn a creature or gameobject in a pool
// If no guid is passed, the pool is just removed (event end case)
// If guid is filled, cache will be used and no removal will occur, it just fill the cache
template<class T>
void PoolGroup<T>::DespawnObject(ActivePoolData& spawns, WOWGUID::LowType guid)
{
    for (std::size_t i = 0; i < EqualChanced.size(); ++i)
    {
        // if spawned
        if (spawns.IsActiveObject<T>(EqualChanced[i].guid))
        {
            if (!guid || EqualChanced[i].guid == guid)
            {
                Despawn1Object(EqualChanced[i].guid);
                spawns.RemoveObject<T>(EqualChanced[i].guid, poolId);
            }
        }
    }

    for (std::size_t i = 0; i < ExplicitlyChanced.size(); ++i)
    {
        // spawned
        if (spawns.IsActiveObject<T>(ExplicitlyChanced[i].guid))
        {
            if (!guid || ExplicitlyChanced[i].guid == guid)
            {
                Despawn1Object(ExplicitlyChanced[i].guid);
                spawns.RemoveObject<T>(ExplicitlyChanced[i].guid, poolId);
            }
        }
    }
}

// Method that is actualy doing the removal job on one creature
template<>
void PoolGroup<Creature>::Despawn1Object(WOWGUID::LowType guid)
{
    if (CreatureData const* data = sObjectMgr->GetCreatureData(guid))
    {
        sObjectMgr->RemoveCreatureFromGrid(guid, data);

        Map* map = sMapMgr->CreateBaseMap(data->mapid);
        if (!map->Instanceable())
        {
            auto creatureBounds = map->GetCreatureBySpawnIdStore().equal_range(guid);
            for (auto itr = creatureBounds.first; itr != creatureBounds.second;)
            {
                Creature* creature = itr->second;
                ++itr;
                creature->AddObjectToRemoveList();
            }
        }
    }
}

// Same on one gameobject
template<>
void PoolGroup<GameObject>::Despawn1Object(WOWGUID::LowType guid)
{
    if (GameObjectData const* data = sObjectMgr->GetGameObjectData(guid))
    {
        sObjectMgr->RemoveGameobjectFromGrid(guid, data);

        Map* map = sMapMgr->CreateBaseMap(data->mapid);
        if (!map->Instanceable())
        {
            auto gameobjectBounds = map->GetGameObjectBySpawnIdStore().equal_range(guid);
            for (auto itr = gameobjectBounds.first; itr != gameobjectBounds.second;)
            {
                GameObject* go = itr->second;
                ++itr;
                go->AddObjectToRemoveList();
            }
        }
    }
}

// Same on one pool
template<>
void PoolGroup<Pool>::Despawn1Object(uint32 child_pool_id)
{
    sPoolMgr->DespawnPool(child_pool_id);
}

// Same on one quest
template<>
void PoolGroup<Quest>::Despawn1Object(uint32 quest_id)
{
    // Creatures
    QuestRelations* questMap = sObjectMgr->GetCreatureQuestRelationMap();
    PooledQuestRelationBoundsNC qr = sPoolMgr->mQuestCreatureRelation.equal_range(quest_id);
    for (PooledQuestRelation::iterator itr = qr.first; itr != qr.second; ++itr)
    {
        QuestRelations::iterator qitr = questMap->find(itr->second);
        if (qitr == questMap->end())
            continue;
        QuestRelations::iterator lastElement = questMap->upper_bound(itr->second);
        for (; qitr != lastElement; ++qitr)
        {
            if (qitr->first == itr->second && qitr->second == itr->first)
            {
                questMap->erase(qitr);                  // iterator is now no more valid
                break;                                  // but we can exit loop since the element is found
            }
        }
    }

    // Gameobjects
    questMap = sObjectMgr->GetGOQuestRelationMap();
    qr = sPoolMgr->mQuestGORelation.equal_range(quest_id);
    for (PooledQuestRelation::iterator itr = qr.first; itr != qr.second; ++itr)
    {
        QuestRelations::iterator qitr = questMap->find(itr->second);
        if (qitr == questMap->end())
            continue;
        QuestRelations::iterator lastElement = questMap->upper_bound(itr->second);
        for (; qitr != lastElement; ++qitr)
        {
            if (qitr->first == itr->second && qitr->second == itr->first)
            {
                questMap->erase(qitr);                  // iterator is now no more valid
                break;                                  // but we can exit loop since the element is found
            }
        }
    }
}

// Method for a pool only to remove any found record causing a circular dependency loop
template<>
void PoolGroup<Pool>::RemoveOneRelation(uint32 child_pool_id)
{
    for (PoolObjectList::iterator itr = ExplicitlyChanced.begin(); itr != ExplicitlyChanced.end(); ++itr)
    {
        if (itr->guid == child_pool_id)
        {
            ExplicitlyChanced.erase(itr);
            break;
        }
    }
    for (PoolObjectList::iterator itr = EqualChanced.begin(); itr != EqualChanced.end(); ++itr)
    {
        if (itr->guid == child_pool_id)
        {
            EqualChanced.erase(itr);
            break;
        }
    }
}

template <class T>
void PoolGroup<T>::SpawnObject(ActivePoolData& spawns, uint32 limit, uint32 triggerFrom)
{
    int count = limit - spawns.GetActiveObjectCount(poolId);

    // If triggered from some object respawn this object is still marked as spawned
    // and also counted into m_SpawnedPoolAmount so we need increase count to be
    // spawned by 1
    if (triggerFrom)
    {
        ++count;
    }

    if (count > 0)
    {
        PoolObjectList rolledObjects;
        rolledObjects.reserve(count);

        // roll objects to be spawned
        if (!ExplicitlyChanced.empty())
        {
            float roll = (float)rand_chance();

            for (PoolObject& obj : ExplicitlyChanced)
            {
                roll -= obj.chance;

                // Triggering object is marked as spawned at this time and can be also rolled (respawn case)
                // so this need explicit check for this case
                if (roll < 0 && (obj.guid == triggerFrom || !spawns.IsActiveObject<T>(obj.guid)))
                {
                    rolledObjects.push_back(obj);
                    break;
                }
            }
        }

        if (!EqualChanced.empty() && rolledObjects.empty())
        {
            std::copy_if(EqualChanced.begin(), EqualChanced.end(), std::back_inserter(rolledObjects), [triggerFrom, &spawns](PoolObject const& object)
            {
                 return object.guid == triggerFrom || !spawns.IsActiveObject<T>(object.guid);
            });

            Acore::Containers::RandomResize(rolledObjects, count);
        }

        // try to spawn rolled objects
        for (PoolObject& obj : rolledObjects)
        {
            if (obj.guid == triggerFrom)
            {
                ReSpawn1Object(&obj);
                triggerFrom = 0;
            }
            else
            {
                spawns.ActivateObject<T>(obj.guid, poolId);
                Spawn1Object(&obj);
            }
        }
    }

    // One spawn one despawn no count increase
    if (triggerFrom)
    {
        DespawnObject(spawns, triggerFrom);
    }
}

// Method that is actualy doing the spawn job on 1 creature
template <>
void PoolGroup<Creature>::Spawn1Object(PoolObject* obj)
{
    if (CreatureData const* data = sObjectMgr->GetCreatureData(obj->guid))
    {
        sObjectMgr->AddCreatureToGrid(obj->guid, data);

        // Spawn if necessary (loaded grids only)
        Map* map = sMapMgr->CreateBaseMap(data->mapid);
        // We use spawn coords to spawn
        if (!map->Instanceable() && map->IsGridLoaded(data->posX, data->posY))
        {
            Creature* creature = new Creature;
            //LOG_DEBUG("pool", "Spawning creature {}", guid);
            if (!creature->LoadCreatureFromDB(obj->guid, map))
            {
                delete creature;
                return;
            }
        }
    }
}

// Same for 1 gameobject
template <>
void PoolGroup<GameObject>::Spawn1Object(PoolObject* obj)
{
    if (GameObjectData const* data = sObjectMgr->GetGameObjectData(obj->guid))
    {
        sObjectMgr->AddGameobjectToGrid(obj->guid, data);
        // Spawn if necessary (loaded grids only)
        // this base map checked as non-instanced and then only existed
        Map* map = sMapMgr->CreateBaseMap(data->mapid);
        // We use current coords to unspawn, not spawn coords since creature can have changed grid
        if (!map->Instanceable() && map->IsGridLoaded(data->posX, data->posY))
        {
            GameObject* pGameobject = sObjectMgr->IsGameObjectStaticTransport(data->id) ? new StaticTransport() : new GameObject();
            //LOG_DEBUG("pool", "Spawning gameobject {}", guid);
            if (!pGameobject->LoadGameObjectFromDB(obj->guid, map, false))
            {
                delete pGameobject;
                return;
            }
            else
            {
                if (pGameobject->isSpawnedByDefault())
                    map->AddToMap(pGameobject);
            }
        }
    }
}

// Same for 1 pool
template <>
void PoolGroup<Pool>::Spawn1Object(PoolObject* obj)
{
    sPoolMgr->SpawnPool(obj->guid);
}

// Same for 1 quest
template<>
void PoolGroup<Quest>::Spawn1Object(PoolObject* obj)
{
    // Creatures
    QuestRelations* questMap = sObjectMgr->GetCreatureQuestRelationMap();
    PooledQuestRelationBoundsNC qr = sPoolMgr->mQuestCreatureRelation.equal_range(obj->guid);
    for (PooledQuestRelation::iterator itr = qr.first; itr != qr.second; ++itr)
    {
        LOG_DEBUG("pool", "PoolGroup<Quest>: Adding quest {} to creature {}", itr->first, itr->second);
        questMap->insert(QuestRelations::value_type(itr->second, itr->first));
    }

    // Gameobjects
    questMap = sObjectMgr->GetGOQuestRelationMap();
    qr = sPoolMgr->mQuestGORelation.equal_range(obj->guid);
    for (PooledQuestRelation::iterator itr = qr.first; itr != qr.second; ++itr)
    {
        LOG_DEBUG("pool", "PoolGroup<Quest>: Adding quest {} to GO {}", itr->first, itr->second);
        questMap->insert(QuestRelations::value_type(itr->second, itr->first));
    }
}

template <>
void PoolGroup<Quest>::SpawnObject(ActivePoolData& spawns, uint32 limit, uint32 triggerFrom)
{
    LOG_DEBUG("pool", "PoolGroup<Quest>: Spawning pool {}", poolId);
    // load state from db
    if (!triggerFrom)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_POOL_QUEST_SAVE);

        stmt->SetData(0, poolId);

        PreparedQueryResult result = CharacterDatabase.Query(stmt);

        if (result)
        {
            do
            {
                uint32 questId = result->Fetch()[0].Get<uint32>();
                spawns.ActivateObject<Quest>(questId, poolId);
                PoolObject tempObj(questId, 0.0f);
                Spawn1Object(&tempObj);
                --limit;
            } while (result->NextRow() && limit);
            return;
        }
    }

    ActivePoolObjects currentQuests = spawns.GetActiveQuests();
    ActivePoolObjects newQuests;

    // always try to select different quests
    for (PoolObjectList::iterator itr = EqualChanced.begin(); itr != EqualChanced.end(); ++itr)
    {
        if (spawns.IsActiveObject<Quest>(itr->guid))
            continue;
        newQuests.insert(itr->guid);
    }

    // clear the pool
    DespawnObject(spawns);

    // recycle minimal amount of quests if possible count is lower than limit
    if (limit > newQuests.size() && !currentQuests.empty())
    {
        do
        {
            uint32 questId = Acore::Containers::SelectRandomContainerElement(currentQuests);
            newQuests.insert(questId);
            currentQuests.erase(questId);
        } while (newQuests.size() < limit && !currentQuests.empty()); // failsafe
    }

    if (newQuests.empty())
        return;

    // activate <limit> random quests
    do
    {
        uint32 questId = Acore::Containers::SelectRandomContainerElement(newQuests);
        spawns.ActivateObject<Quest>(questId, poolId);
        PoolObject tempObj(questId, 0.0f);
        Spawn1Object(&tempObj);
        newQuests.erase(questId);
        --limit;
    } while (limit && !newQuests.empty());

    // if we are here it means the pool is initialized at startup and did not have previous saved state
    if (!triggerFrom)
        sPoolMgr->SaveQuestsToDB(false, false, true);
}

// Method that does the respawn job on the specified creature
template <>
void PoolGroup<Creature>::ReSpawn1Object(PoolObject* obj)
{
    Despawn1Object(obj->guid);
    Spawn1Object(obj);
}

// Method that does the respawn job on the specified gameobject
template <>
void PoolGroup<GameObject>::ReSpawn1Object(PoolObject* obj)
{
    Despawn1Object(obj->guid);
    Spawn1Object(obj);
}

// Nothing to do for a child Pool
template <>
void PoolGroup<Pool>::ReSpawn1Object(PoolObject* /*obj*/)
{
}

// Nothing to do for a quest
template <>
void PoolGroup<Quest>::ReSpawn1Object(PoolObject* /*obj*/)
{
}

////////////////////////////////////////////////////////////
// Methods of class PoolMgr

PoolMgr::PoolMgr()
{
}

PoolMgr* PoolMgr::instance()
{
    static PoolMgr instance;
    return &instance;
}

void PoolMgr::Initialize()
{
    mQuestSearchMap.clear();
    mGameobjectSearchMap.clear();
    mCreatureSearchMap.clear();
}

void PoolMgr::LoadFromDB()
{
    // Pool templates
    {
        uint32 oldMSTime = getMSTime();

        QueryResult result = WorldDatabase.Query("SELECT entry, max_limit FROM pool_template");
        if (!result)
        {
            mPoolTemplate.clear();
            LOG_WARN("server.loading", ">> Loaded 0 object pools. DB table `pool_template` is empty.");
            LOG_INFO("server.loading", " ");
            return;
        }

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();

            uint32 pool_id = fields[0].Get<uint32>();

            PoolTemplateData& pPoolTemplate = mPoolTemplate[pool_id];
            pPoolTemplate.MaxLimit  = fields[1].Get<uint32>();

            ++count;
        } while (result->NextRow());

        LOG_INFO("server.loading", ">> Loaded {} Objects Pools In {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        LOG_INFO("server.loading", " ");
    }

    // Creatures

    LOG_INFO("server.loading", "Loading Creatures Pooling Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                 1       2         3
        QueryResult result = WorldDatabase.Query("SELECT guid, pool_entry, chance FROM pool_creature");

        if (!result)
        {
            LOG_WARN("server.loading", ">> Loaded 0 creatures in  pools. DB table `pool_creature` is empty.");
            LOG_INFO("server.loading", " ");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                WOWGUID::LowType guid = fields[0].Get<uint32>();
                uint32 pool_id = fields[1].Get<uint32>();
                float chance = fields[2].Get<float>();

                CreatureData const* data = sObjectMgr->GetCreatureData(guid);
                if (!data)
                {
                    LOG_ERROR("sql.sql", "`pool_creature` has a non existing creature spawn (GUID: {}) defined for pool id ({}), skipped.", guid, pool_id);
                    continue;
                }
                auto it = mPoolTemplate.find(pool_id);
                if (it == mPoolTemplate.end())
                {
                    LOG_ERROR("sql.sql", "`pool_creature` pool id ({}) is not in `pool_template`, skipped.", pool_id);
                    continue;
                }
                if (chance < 0 || chance > 100)
                {
                    LOG_ERROR("sql.sql", "`pool_creature` has an invalid chance ({}) for creature guid ({}) in pool id ({}), skipped.", chance, guid, pool_id);
                    continue;
                }
                PoolTemplateData* pPoolTemplate = &mPoolTemplate[pool_id];
                PoolObject plObject = PoolObject(guid, chance);
                PoolGroup<Creature>& cregroup = mPoolCreatureGroups[pool_id];
                cregroup.SetPoolId(pool_id);
                cregroup.AddEntry(plObject, pPoolTemplate->MaxLimit);
                SearchPair p(guid, pool_id);
                mCreatureSearchMap.insert(p);

                ++count;
            } while (result->NextRow());

            LOG_INFO("server.loading", ">> Loaded {} Creatures In Pools in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
            LOG_INFO("server.loading", " ");
        }
    }

    // Gameobjects

    LOG_INFO("server.loading", "Loading Gameobjects Pooling Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                 1        2         3
        QueryResult result = WorldDatabase.Query("SELECT guid, pool_entry, chance FROM pool_gameobject");

        if (!result)
        {
            LOG_WARN("server.loading", ">> Loaded 0 gameobjects in  pools. DB table `pool_gameobject` is empty.");
            LOG_INFO("server.loading", " ");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                WOWGUID::LowType guid = fields[0].Get<uint32>();
                uint32 pool_id = fields[1].Get<uint32>();
                float chance = fields[2].Get<float>();

                GameObjectData const* data = sObjectMgr->GetGameObjectData(guid);
                if (!data)
                {
                    LOG_ERROR("sql.sql", "`pool_gameobject` has a non existing gameobject spawn (GUID: {}) defined for pool id ({}), skipped.", guid, pool_id);
                    continue;
                }

                GameObjectTemplate const* goinfo = sObjectMgr->GetGameObjectTemplate(data->id);
                if (goinfo->type != GAMEOBJECT_TYPE_CHEST &&
                        goinfo->type != GAMEOBJECT_TYPE_GOOBER &&
                        goinfo->type != GAMEOBJECT_TYPE_FISHINGHOLE)
                {
                    LOG_ERROR("sql.sql", "`pool_gameobject` has a not lootable gameobject spawn (GUID: {}, type: {}) defined for pool id ({}), skipped.", guid, goinfo->type, pool_id);
                    continue;
                }

                auto it = mPoolTemplate.find(pool_id);
                if (it == mPoolTemplate.end())
                {
                    LOG_ERROR("sql.sql", "`pool_gameobject` pool id ({}) is not in `pool_template`, skipped.", pool_id);
                    continue;
                }

                if (chance < 0 || chance > 100)
                {
                    LOG_ERROR("sql.sql", "`pool_gameobject` has an invalid chance ({}) for gameobject guid ({}) in pool id ({}), skipped.", chance, guid, pool_id);
                    continue;
                }

                PoolTemplateData* pPoolTemplate = &mPoolTemplate[pool_id];
                PoolObject plObject = PoolObject(guid, chance);
                PoolGroup<GameObject>& gogroup = mPoolGameobjectGroups[pool_id];
                gogroup.SetPoolId(pool_id);
                gogroup.AddEntry(plObject, pPoolTemplate->MaxLimit);
                SearchPair p(guid, pool_id);
                mGameobjectSearchMap.insert(p);

                ++count;
            } while (result->NextRow());

            LOG_INFO("server.loading", ">> Loaded {} Gameobjects In Pools in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
            LOG_INFO("server.loading", " ");
        }
    }

    // Pool of pools

    LOG_INFO("server.loading", "Loading Mother Pooling Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                  1        2            3
        QueryResult result = WorldDatabase.Query("SELECT pool_id, mother_pool, chance FROM pool_pool");

        if (!result)
        {
            LOG_WARN("server.loading", ">> Loaded 0 pools in pools");
            LOG_INFO("server.loading", " ");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint32 child_pool_id  = fields[0].Get<uint32>();
                uint32 mother_pool_id = fields[1].Get<uint32>();
                float chance          = fields[2].Get<float>();

                {
                    auto it = mPoolTemplate.find(mother_pool_id);
                    if (it == mPoolTemplate.end())
                    {
                        LOG_ERROR("sql.sql", "`pool_pool` mother_pool id ({}) is not in `pool_template`, skipped.", mother_pool_id);
                        continue;
                    }
                }
                {
                    auto it = mPoolTemplate.find(child_pool_id);
                    if (it == mPoolTemplate.end())
                    {
                        LOG_ERROR("sql.sql", "`pool_pool` included pool_id ({}) is not in `pool_template`, skipped.", child_pool_id);
                        continue;
                    }
                }
                if (mother_pool_id == child_pool_id)
                {
                    LOG_ERROR("sql.sql", "`pool_pool` pool_id ({}) includes itself, dead-lock detected, skipped.", child_pool_id);
                    continue;
                }
                if (chance < 0 || chance > 100)
                {
                    LOG_ERROR("sql.sql", "`pool_pool` has an invalid chance ({}) for pool id ({}) in mother pool id ({}), skipped.", chance, child_pool_id, mother_pool_id);
                    continue;
                }
                PoolTemplateData* pPoolTemplateMother = &mPoolTemplate[mother_pool_id];
                PoolObject plObject = PoolObject(child_pool_id, chance);
                PoolGroup<Pool>& plgroup = mPoolPoolGroups[mother_pool_id];
                plgroup.SetPoolId(mother_pool_id);
                plgroup.AddEntry(plObject, pPoolTemplateMother->MaxLimit);
                SearchPair p(child_pool_id, mother_pool_id);
                mPoolSearchMap.insert(p);

                ++count;
            } while (result->NextRow());

            // Now check for circular reference
            // All pool_ids are in pool_template
            for (auto const& it : mPoolTemplate)
            {
                std::set<uint32> checkedPools;
                for (SearchMap::iterator poolItr = mPoolSearchMap.find(it.first); poolItr != mPoolSearchMap.end(); poolItr = mPoolSearchMap.find(poolItr->second))
                {
                    checkedPools.insert(poolItr->first);
                    if (checkedPools.find(poolItr->second) != checkedPools.end())
                    {
                        std::ostringstream ss;
                        ss << "The pool(s) ";
                        for (std::set<uint32>::const_iterator itr = checkedPools.begin(); itr != checkedPools.end(); ++itr)
                            ss << *itr << ' ';
                        ss << "create(s) a circular reference, which can cause the server to freeze.\nRemoving the last link between mother pool "
                           << poolItr->first << " and child pool " << poolItr->second;
                        LOG_ERROR("sql.sql", "{}", ss.str());
                        mPoolPoolGroups[poolItr->second].RemoveOneRelation(poolItr->first);
                        mPoolSearchMap.erase(poolItr);
                        --count;
                        break;
                    }
                }
            }

            LOG_INFO("server.loading", ">> Loaded {} Pools In Mother Pools in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
            LOG_INFO("server.loading", " ");
        }
    }

    LOG_INFO("server.loading", "Loading Quest Pooling Data...");
    {
        uint32 oldMSTime = getMSTime();

        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_QUEST_POOLS);
        PreparedQueryResult result = WorldDatabase.Query(stmt);

        if (!result)
        {
            LOG_WARN("server.loading", ">> Loaded 0 quests in pools");
            LOG_INFO("server.loading", " ");
        }
        else
        {
            PooledQuestRelationBounds creBounds;
            PooledQuestRelationBounds goBounds;

            enum eQuestTypes
            {
                QUEST_NONE   = 0,
                QUEST_DAILY  = 1,
                QUEST_WEEKLY = 2
            };

            std::map<uint32, int32> poolTypeMap;
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint32 entry   = fields[0].Get<uint32>();
                uint32 pool_id = fields[1].Get<uint32>();

                Quest const* quest = sObjectMgr->GetQuestTemplate(entry);
                if (!quest)
                {
                    LOG_ERROR("sql.sql", "`pool_quest` has a non existing quest template (Entry: {}) defined for pool id ({}), skipped.", entry, pool_id);
                    continue;
                }

                auto it = mPoolTemplate.find(pool_id);
                if (it == mPoolTemplate.end())
                {
                    LOG_ERROR("sql.sql", "`pool_quest` pool id ({}) is not in `pool_template`, skipped.", pool_id);
                    continue;
                }

                if (!quest->IsDailyOrWeekly())
                {
                    LOG_ERROR("sql.sql", "`pool_quest` has an quest ({}) which is not daily or weekly in pool id ({}), use ExclusiveGroup instead, skipped.", entry, pool_id);
                    continue;
                }

                if (poolTypeMap[pool_id] == QUEST_NONE)
                    poolTypeMap[pool_id] = quest->IsDaily() ? QUEST_DAILY : QUEST_WEEKLY;

                int32 currType = quest->IsDaily() ? QUEST_DAILY : QUEST_WEEKLY;

                if (poolTypeMap[pool_id] != currType)
                {
                    LOG_ERROR("sql.sql", "`pool_quest` quest {} is {} but pool ({}) is specified for {}, mixing not allowed, skipped.",
                                     entry, currType == QUEST_DAILY ? "QUEST_DAILY" : "QUEST_WEEKLY", pool_id, poolTypeMap[pool_id] == QUEST_DAILY ? "QUEST_DAILY" : "QUEST_WEEKLY");
                    continue;
                }

                creBounds = mQuestCreatureRelation.equal_range(entry);
                goBounds = mQuestGORelation.equal_range(entry);

                if (creBounds.first == creBounds.second && goBounds.first == goBounds.second)
                {
                    LOG_ERROR("sql.sql", "`pool_quest` lists entry ({}) as member of pool ({}) but is not started anywhere, skipped.", entry, pool_id);
                    continue;
                }

                PoolTemplateData* pPoolTemplate = &mPoolTemplate[pool_id];
                PoolObject plObject = PoolObject(entry, 0.0f);
                PoolGroup<Quest>& questgroup = mPoolQuestGroups[pool_id];
                questgroup.SetPoolId(pool_id);
                questgroup.AddEntry(plObject, pPoolTemplate->MaxLimit);
                SearchPair p(entry, pool_id);
                mQuestSearchMap.insert(p);

                ++count;
            } while (result->NextRow());

            LOG_INFO("server.loading", ">> Loaded {} Quests In Pools in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
            LOG_INFO("server.loading", " ");
        }
    }

    // The initialize method will spawn all pools not in an event and not in another pool, this is why there is 2 left joins with 2 null checks
    LOG_INFO("server.loading", "Starting Objects Pooling System...");
    {
        uint32 oldMSTime = getMSTime();

        QueryResult result = WorldDatabase.Query("SELECT DISTINCT pool_template.entry, pool_pool.pool_id, pool_pool.mother_pool FROM pool_template"
                             " LEFT JOIN game_event_pool ON pool_template.entry=game_event_pool.pool_entry"
                             " LEFT JOIN pool_pool ON pool_template.entry=pool_pool.pool_id WHERE game_event_pool.pool_entry IS NULL");

        if (!result)
        {
            LOG_INFO("server.loading", ">> Pool handling system initialized, 0 pools spawned.");
            LOG_INFO("server.loading", " ");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32 pool_entry = fields[0].Get<uint32>();
                uint32 pool_pool_id = fields[1].Get<uint32>();

                if (!CheckPool(pool_entry))
                {
                    if (pool_pool_id)
                        // The pool is a child pool in pool_pool table. Ideally we should remove it from the pool handler to ensure it never gets spawned,
                        // however that could recursively invalidate entire chain of mother pools. It can be done in the future but for now we'll do nothing.
                        LOG_ERROR("sql.sql", "Pool Id {} has no equal chance pooled entites defined and explicit chance sum is not 100. This broken pool is a child pool of Id {} and cannot be safely removed.", pool_entry, fields[2].Get<uint32>());
                    else
                        LOG_ERROR("sql.sql", "Pool Id {} has no equal chance pooled entites defined and explicit chance sum is not 100. The pool will not be spawned.", pool_entry);
                    continue;
                }

                // Don't spawn child pools, they are spawned recursively by their parent pools
                if (!pool_pool_id)
                {
                    SpawnPool(pool_entry);
                    count++;
                }
            } while (result->NextRow());

            LOG_INFO("pool", "Pool handling system initialized, {} pools spawned in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
            LOG_INFO("server.loading", " ");
        }
    }
}

void PoolMgr::LoadQuestPools()
{
}

void PoolMgr::SaveQuestsToDB(bool daily, bool weekly, bool other)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // pussywizard: mysql thread races, change only what is meant to be changed
    std::set<uint32> deletedPools;
    for (PoolGroupQuestMap::iterator itr = mPoolQuestGroups.begin(); itr != mPoolQuestGroups.end(); ++itr)
    {
        if (itr->second.IsEmpty())
            continue;
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(itr->second.GetFirstEqualChancedObjectId()))
        {
            if (!daily && quest->IsDaily())
                continue;
            if (!weekly && quest->IsWeekly())
                continue;
            if (!other && !quest->IsDaily() && !quest->IsWeekly())
                continue;
        }
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_QUEST_POOL_SAVE);
        stmt->SetData(0, itr->second.GetPoolId());
        trans->Append(stmt);
        deletedPools.insert(itr->second.GetPoolId());
    }

    for (SearchMap::iterator itr = mQuestSearchMap.begin(); itr != mQuestSearchMap.end(); ++itr)
        if (deletedPools.find(itr->second) != deletedPools.end())
            if (IsSpawnedObject<Quest>(itr->first))
            {
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_QUEST_POOL_SAVE);
                stmt->SetData(0, itr->second);
                stmt->SetData(1, itr->first);
                trans->Append(stmt);
            }

    CharacterDatabase.CommitTransaction(trans);
}

void PoolMgr::ChangeDailyQuests()
{
    for (PoolGroupQuestMap::iterator itr = mPoolQuestGroups.begin(); itr != mPoolQuestGroups.end(); ++itr)
    {
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(itr->second.GetFirstEqualChancedObjectId()))
        {
            if (quest->IsWeekly())
                continue;

            UpdatePool<Quest>(itr->second.GetPoolId(), 1);    // anything non-zero means don't load from db
        }
    }

    SaveQuestsToDB(true, false, false);
}

void PoolMgr::ChangeWeeklyQuests()
{
    for (PoolGroupQuestMap::iterator itr = mPoolQuestGroups.begin(); itr != mPoolQuestGroups.end(); ++itr)
    {
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(itr->second.GetFirstEqualChancedObjectId()))
        {
            if (quest->IsDaily())
                continue;

            UpdatePool<Quest>(itr->second.GetPoolId(), 1);
        }
    }

    SaveQuestsToDB(false, true, false);
}

// Call to spawn a pool, if cache if true the method will spawn only if cached entry is different
// If it's same, the creature is respawned only (added back to map)
template<>
void PoolMgr::SpawnPool<Creature>(uint32 pool_id, uint32 db_guid)
{
    auto it = mPoolCreatureGroups.find(pool_id);
    if (it != mPoolCreatureGroups.end() && !it->second.IsEmpty())
        it->second.SpawnObject(mSpawnedData, mPoolTemplate[pool_id].MaxLimit, db_guid);
}

// Call to spawn a pool, if cache if true the method will spawn only if cached entry is different
// If it's same, the gameobject is respawned only (added back to map)
template<>
void PoolMgr::SpawnPool<GameObject>(uint32 pool_id, uint32 db_guid)
{
    auto it = mPoolGameobjectGroups.find(pool_id);
    if (it != mPoolGameobjectGroups.end() && !it->second.IsEmpty())
        it->second.SpawnObject(mSpawnedData, mPoolTemplate[pool_id].MaxLimit, db_guid);
}

// Call to spawn a pool, if cache if true the method will spawn only if cached entry is different
// If it's same, the pool is respawned only
template<>
void PoolMgr::SpawnPool<Pool>(uint32 pool_id, uint32 sub_pool_id)
{
    auto it = mPoolPoolGroups.find(pool_id);
    if (it != mPoolPoolGroups.end() && !it->second.IsEmpty())
        it->second.SpawnObject(mSpawnedData, mPoolTemplate[pool_id].MaxLimit, sub_pool_id);
}

// Call to spawn a pool
template<>
void PoolMgr::SpawnPool<Quest>(uint32 pool_id, uint32 quest_id)
{
    auto it = mPoolQuestGroups.find(pool_id);
    if (it != mPoolQuestGroups.end() && !it->second.IsEmpty())
        it->second.SpawnObject(mSpawnedData, mPoolTemplate[pool_id].MaxLimit, quest_id);
}

void PoolMgr::SpawnPool(uint32 pool_id)
{
    SpawnPool<Pool>(pool_id, 0);
    SpawnPool<GameObject>(pool_id, 0);
    SpawnPool<Creature>(pool_id, 0);
    SpawnPool<Quest>(pool_id, 0);
}

// Call to despawn a pool, all gameobjects/creatures in this pool are removed
void PoolMgr::DespawnPool(uint32 pool_id)
{
    {
        auto it = mPoolCreatureGroups.find(pool_id);
        if (it != mPoolCreatureGroups.end() && !it->second.IsEmpty())
            it->second.DespawnObject(mSpawnedData);
    }
    {
        auto it = mPoolGameobjectGroups.find(pool_id);
        if (it != mPoolGameobjectGroups.end() && !it->second.IsEmpty())
            it->second.DespawnObject(mSpawnedData);
    }
    {
        auto it = mPoolPoolGroups.find(pool_id);
        if (it != mPoolPoolGroups.end() && !it->second.IsEmpty())
            it->second.DespawnObject(mSpawnedData);
    }
    {
        auto it = mPoolQuestGroups.find(pool_id);
        if (it != mPoolQuestGroups.end() && !it->second.IsEmpty())
            it->second.DespawnObject(mSpawnedData);
    }
}

// Method that check chance integrity of the creatures and gameobjects in this pool
bool PoolMgr::CheckPool(uint32 pool_id) const
{
    {
        auto it = mPoolGameobjectGroups.find(pool_id);
        if (it != mPoolGameobjectGroups.end() && !it->second.CheckPool())
            return false;
    }
    {
        auto it = mPoolCreatureGroups.find(pool_id);
        if (it != mPoolCreatureGroups.end() && !it->second.CheckPool())
            return false;
    }
    {
        auto it = mPoolPoolGroups.find(pool_id);
        if (it != mPoolPoolGroups.end() && !it->second.CheckPool())
            return false;
    }
    {
        auto it = mPoolQuestGroups.find(pool_id);
        if (it != mPoolQuestGroups.end() && !it->second.CheckPool())
            return false;
    }
    return true;
}

// Call to update the pool when a gameobject/creature part of pool [pool_id] is ready to respawn
// Here we cache only the creature/gameobject whose guid is passed as parameter
// Then the spawn pool call will use this cache to decide
template<typename T>
void PoolMgr::UpdatePool(uint32 pool_id, uint32 db_guid_or_pool_id)
{
    if (uint32 motherpoolid = IsPartOfAPool<Pool>(pool_id))
        SpawnPool<Pool>(motherpoolid, pool_id);
    else
        SpawnPool<T>(pool_id, db_guid_or_pool_id);
}

template void PoolMgr::UpdatePool<Pool>(uint32 pool_id, uint32 db_guid_or_pool_id);
template void PoolMgr::UpdatePool<GameObject>(uint32 pool_id, uint32 db_guid_or_pool_id);
template void PoolMgr::UpdatePool<Creature>(uint32 pool_id, uint32 db_guid_or_pool_id);
template void PoolMgr::UpdatePool<Quest>(uint32 pool_id, uint32 db_guid_or_pool_id);
