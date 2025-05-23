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

#ifndef __UPDATEDATA_H
#define __UPDATEDATA_H

#include "ByteBuffer.h"
#include "GUID.h"

class WDataStore;

enum OBJECT_UPDATE_TYPE
{
    UPDATETYPE_VALUES               = 0,
    UPDATETYPE_MOVEMENT             = 1,
    UPDATETYPE_CREATE_OBJECT        = 2,
    UPDATETYPE_CREATE_OBJECT2       = 3,
    UPDATETYPE_OUT_OF_RANGE_OBJECTS = 4,
    UPDATETYPE_NEAR_OBJECTS         = 5
};

enum OBJECT_UPDATE_FLAGS
{
    UPDATEFLAG_NONE                 = 0x0000,
    UPDATEFLAG_SELF                 = 0x0001,
    UPDATEFLAG_TRANSPORT            = 0x0002,
    UPDATEFLAG_HAS_TARGET           = 0x0004,
    UPDATEFLAG_UNKNOWN              = 0x0008,
    UPDATEFLAG_LOWGUID              = 0x0010,
    UPDATEFLAG_LIVING               = 0x0020,
    UPDATEFLAG_STATIONARY_POSITION  = 0x0040,
    UPDATEFLAG_VEHICLE              = 0x0080,
    UPDATEFLAG_POSITION             = 0x0100,
    UPDATEFLAG_ROTATION             = 0x0200
};

class UpdateData
{
public:
    UpdateData();

    void AddOutOfRangeGUID(WOWGUID guid);
    void AddUpdateBlock(const ByteBuffer& block);
    void AddUpdateBlock(const UpdateData& block);
    bool BuildPacket(WDataStore& packet);
    [[nodiscard]] bool HasData() const { return m_blockCount > 0 || !m_outOfRangeGUIDs.empty(); }
    void Clear();

protected:
    uint32 m_blockCount;
    GuidVector m_outOfRangeGUIDs;
    ByteBuffer m_data;
};
#endif
