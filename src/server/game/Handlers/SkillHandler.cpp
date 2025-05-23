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

#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "SpellMgr.h"
#include "WDataStore.h"
#include "User.h"

void User::HandleLearnTalentOpcode(WDataStore& recvData)
{
    uint32 talent_id, requested_rank;
    recvData >> talent_id >> requested_rank;

    m_player->LearnTalent(talent_id, requested_rank);
    m_player->SendTalentsInfoData(false);
}

void User::HandleLearnPreviewTalents(WDataStore& recvPacket)
{
    LOG_DEBUG("network", "CMSG_LEARN_PREVIEW_TALENTS");

    uint32 talentsCount;
    recvPacket >> talentsCount;

    uint32 talentId, talentRank;

    // Client has max 44 talents for tree for 3 trees, rounded up : 150
    uint32 const MaxTalentsCount = 150;

    for (uint32 i = 0; i < talentsCount && i < MaxTalentsCount; ++i)
    {
        recvPacket >> talentId >> talentRank;

        m_player->LearnTalent(talentId, talentRank);
    }

    m_player->SendTalentsInfoData(false);

    recvPacket.rfinish();
}

void User::HandleTalentWipeConfirmOpcode(WDataStore& recvData)
{
    LOG_DEBUG("network", "MSG_TALENT_WIPE_CONFIRM");
    WOWGUID guid;
    recvData >> guid;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_TRAINER);
    if (!unit)
    {
        LOG_DEBUG("network", "WORLD: HandleTalentWipeConfirmOpcode - Unit ({}) not found or you can't interact with him.", guid.ToString());
        return;
    }

    if (!unit->isCanTrainingAndResetTalentsOf(m_player))
        return;

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    if (!(m_player->resetTalents()))
    {
        WDataStore data(MSG_TALENT_WIPE_CONFIRM, 8 + 4);  //you have not any talent
        data << uint64(0);
        data << uint32(0);
        Send(&data);
        return;
    }

    m_player->SendTalentsInfoData(false);
    unit->CastSpell(m_player, 14867, true);                  //spell: "Untalent Visual Effect"
}

void User::HandleUnlearnSkillOpcode(WDataStore& recvData)
{
    uint32 skillId;
    recvData >> skillId;

    if (!IsPrimaryProfessionSkill(skillId))
        return;

    GetPlayer()->SetSkill(skillId, 0, 0, 0);
}
