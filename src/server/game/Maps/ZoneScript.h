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

#ifndef ZONE_SCRIPT_H_
#define ZONE_SCRIPT_H_

#include "Common.h"
#include "Creature.h"

class GameObject;

class ZoneScript
{
public:
    ZoneScript() {}
    virtual ~ZoneScript() {}

    virtual uint32 GetCreatureEntry(WOWGUID::LowType /*guidlow*/, CreatureData const* data) { return data->id1; }
    virtual uint32 GetGameObjectEntry(WOWGUID::LowType /*guidlow*/, uint32 entry) { return entry; }

    virtual void OnCreatureCreate(Creature*) { }
    virtual void OnCreatureRemove(Creature*) { }

    virtual void OnGameObjectCreate(GameObject*) { }
    virtual void OnGameObjectRemove(GameObject*) { }

    virtual void OnUnitDeath(Unit*) { }
    virtual void OnCreatureEvade(Creature*) { }

    //All-purpose data storage 64 bit
    virtual WOWGUID GetGuidData(uint32 /*DataId*/) const { return WOWGUID::Empty; }
    virtual void SetGuidData(uint32 /*DataId*/, WOWGUID /*Value*/) {}

    virtual uint64 GetData64(uint32 /*DataId*/) const { return 0; }
    virtual void SetData64(uint32 /*DataId*/, uint64 /*Value*/) {}

    //All-purpose data storage 32 bit
    virtual uint32 GetData(uint32 /*DataId*/) const { return 0; }
    virtual void SetData(uint32 /*DataId*/, uint32 /*Value*/) {}

    virtual void ProcessEvent(WorldObject* /*obj*/, uint32 /*eventId*/) {}
};

#endif
