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

#ifndef SC_FOLLOWERAI_H
#define SC_FOLLOWERAI_H

#include "ScriptSystem.h"
#include "ScriptedCreature.h"

enum eFollowState
{
    STATE_FOLLOW_NONE       = 0x000,
    STATE_FOLLOW_INPROGRESS = 0x001,                        //must always have this state for any follow
    STATE_FOLLOW_RETURNING  = 0x002,                        //when returning to combat start after being in combat
    STATE_FOLLOW_PAUSED     = 0x004,                        //disables following
    STATE_FOLLOW_COMPLETE   = 0x008,                        //follow is completed and may end
    STATE_FOLLOW_PREEVENT   = 0x010,                        //not implemented (allow pre event to run, before follow is initiated)
    STATE_FOLLOW_POSTEVENT  = 0x020                         //can be set at complete and allow post event to run
};

class FollowerAI : public ScriptedAI
{
public:
    explicit FollowerAI(Creature* creature);
    ~FollowerAI() override { }

    //virtual void WaypointReached(uint32 uiPointId) = 0;

    void MovementInform(uint32 motionType, uint32 pointId) override;

    void AttackStart(Unit*) override;

    void MoveInLineOfSight(Unit*) override;

    void EnterEvadeMode(EvadeReason /*why*/ = EVADE_REASON_OTHER) override;

    void JustDied(Unit*) override;

    void JustRespawned() override;

    void UpdateAI(uint32) override;                        //the "internal" update, calls UpdateFollowerAI()
    virtual void UpdateFollowerAI(uint32);        //used when it's needed to add code in update (abilities, scripted events, etc)

    void StartFollow(Player* player, uint32 factionForFollower = 0, const Quest* quest = nullptr);

    void SetFollowPaused(bool bPaused);                 //if special event require follow mode to hold/resume during the follow
    void SetFollowComplete(bool bWithEndEvent = false);

    bool HasFollowState(uint32 uiFollowState) { return (m_uiFollowState & uiFollowState); }

protected:
    Player* GetLeaderForFollower();

private:
    void AddFollowState(uint32 uiFollowState) { m_uiFollowState |= uiFollowState; }
    void RemoveFollowState(uint32 uiFollowState) { m_uiFollowState &= ~uiFollowState; }

    bool AssistPlayerInCombatAgainst(Unit* who);

    WOWGUID m_uiLeaderGUID;
    uint32 m_uiUpdateFollowTimer;
    uint32 m_uiFollowState;

    const Quest* m_pQuestForFollow;                     //normally we have a quest
};

#endif
