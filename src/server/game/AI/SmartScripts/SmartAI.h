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

#ifndef ACORE_SMARTAI_H
#define ACORE_SMARTAI_H

#include "Common.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObjectAI.h"
#include "SmartScript.h"
#include "SmartScriptMgr.h"
#include "Spell.h"
#include "Unit.h"

enum SmartEscortState
{
    SMART_ESCORT_NONE       = 0x000,                        //nothing in progress
    SMART_ESCORT_ESCORTING  = 0x001,                        //escort is in progress
    SMART_ESCORT_RETURNING  = 0x002,                        //escort is returning after being in combat
    SMART_ESCORT_PAUSED     = 0x004                         //will not proceed with waypoints before state is removed
};

enum SmartEscortVars
{
    SMART_ESCORT_MAX_PLAYER_DIST        = 60,
    SMART_MAX_AID_DIST                  = SMART_ESCORT_MAX_PLAYER_DIST / 2,
};

class SmartAI : public CreatureAI
{
public:
    ~SmartAI() override {};
    explicit SmartAI(Creature* c);

    // Check whether we are currently permitted to make the creature take action
    bool IsAIControlled() const;

    // Start moving to the desired MovePoint
    void StartPath(bool run = false, uint32 path = 0, bool repeat = false, Unit* invoker = nullptr);
    bool LoadPath(uint32 entry);
    void PausePath(uint32 delay, bool forced = false);
    void StopPath(uint32 DespawnTime = 0, uint32 quest = 0, bool fail = false);
    void EndPath(bool fail = false);
    void ResumePath();
    WayPoint* GetNextWayPoint();
    void GenerateWayPointArray(Movement::PointsArray* points);
    bool HasEscortState(uint32 uiEscortState) { return (mEscortState & uiEscortState); }
    void AddEscortState(uint32 uiEscortState) { mEscortState |= uiEscortState; }
    bool IsEscorted() override { return (mEscortState & SMART_ESCORT_ESCORTING); }
    void RemoveEscortState(uint32 uiEscortState) { mEscortState &= ~uiEscortState; }
    void SetAutoAttack(bool on) { mCanAutoAttack = on; }
    void SetCombatMove(bool on, float chaseRange = 0.0f);
    bool CanCombatMove() { return mCanCombatMove; }
    void SetFollow(Unit* target, float dist = 0.0f, float angle = 0.0f, uint32 credit = 0, uint32 end = 0, uint32 creditType = 0, bool aliveState = true);
    void StopFollow(bool complete);
    void MoveAway(float distance);

    void SetScript9(SmartScriptHolder& e, uint32 entry, Unit* invoker);
    SmartScript* GetScript() { return &mScript; }
    bool IsEscortInvokerInRange();

    // Called when creature is spawned or respawned
    void JustRespawned() override;

    // Called at reaching home after evade, InitializeAI(), EnterEvadeMode() for resetting variables
    void JustReachedHome() override;

    // Called for reaction at enter to combat if not in combat yet (enemy can be nullptr)
    void JustEngagedWith(Unit* enemy) override;

    // Called for reaction at stopping attack at no attackers or targets
    void EnterEvadeMode(EvadeReason why = EVADE_REASON_OTHER) override;

    // Called when the creature is killed
    void JustDied(Unit* killer) override;

    // Called when the creature kills a unit
    void KilledUnit(Unit* victim) override;

    // Called when the creature summon successfully other creature
    void JustSummoned(Creature* creature) override;

    // Called when a summoned unit dies
    void SummonedCreatureDies(Creature* summon, Unit* killer) override;

    // Called when a summoned unit evades
    void SummonedCreatureEvade(Creature* summon) override;

    // Tell creature to attack and follow the victim
    void AttackStart(Unit* who) override;

    // Called if IsVisible(Unit* who) is true at each *who move, reaction at visibility zone enter
    void MoveInLineOfSight(Unit* who) override;

    // Called when hit by a spell
    void SpellHit(Unit* unit, SpellInfo const* spellInfo) override;

    // Called when spell hits a target
    void SpellHitTarget(Unit* target, SpellInfo const* spellInfo) override;

    // Called at any Damage from any attacker (before damage apply)
    void DamageTaken(Unit* done_by, uint32& damage, DamageEffectType damagetype, SpellSchoolMask damageSchoolMask) override;

    // Called when the creature receives heal
    void HealReceived(Unit* doneBy, uint32& addhealth) override;

    // Called at World update tick
    void UpdateAI(uint32 diff) override;

    // Called at text emote receive from player
    void ReceiveEmote(Player* player, uint32 textEmote) override;

    // Called at waypoint reached or point movement finished
    void MovementInform(uint32 MovementType, uint32 Data) override;

    // Called when creature is summoned by another unit
    void IsSummonedBy(WorldObject* summoner) override;

    // Called at any Damage to any victim (before damage apply)
    void DamageDealt(Unit* doneTo, uint32& damage, DamageEffectType damagetyp) override;

    // Called when a summoned creature dissapears (UnSommoned)
    void SummonedCreatureDespawn(Creature* unit) override;

    // called when the corpse of this creature gets removed
    void CorpseRemoved(uint32& respawnDelay) override;

    // Called when a Player/Creature enters the creature (vehicle)
    void PassengerBoarded(Unit* who, int8 seatId, bool apply) override;

    // Called when gets initialized, when creature is added to world
    void InitializeAI() override;

    // Called when creature gets charmed by another unit
    void OnCharmed(bool apply) override;

    // Called when victim is in line of sight
    bool CanAIAttack(Unit const* who) const override;

    // Used in scripts to share variables
    void DoAction(int32 param = 0) override;

    // Used in scripts to share variables
    uint32 GetData(uint32 id = 0) const override;

    // Used in scripts to share variables
    void SetData(uint32 id, uint32 value) override { SetData(id, value, nullptr); }
    void SetData(uint32 id, uint32 value, Unit* invoker);

    // Used in scripts to share variables
    void SetGUID(WOWGUID guid, int32 id = 0) override;

    // Used in scripts to share variables
    WOWGUID GetGUID(int32 id = 0) const override;

    //core related
    static int32 Permissible(Creature const* /*creature*/) { return PERMIT_BASE_NO; }

    // Called at movepoint reached
    void MovepointReached(uint32 id);

    // Makes the creature run/walk
    void SetRun(bool run = true);

    void SetFly(bool fly = true);

    void SetSwim(bool swim = true);

    void SetEvadeDisabled(bool disable = true);

    void SetInvincibilityHpLevel(uint32 level) { mInvincibilityHpLevel = level; }

    void sGossipHello(Player* player) override;
    void sGossipSelect(Player* player, uint32 sender, uint32 action) override;
    void sGossipSelectCode(Player* player, uint32 sender, uint32 action, const char* code) override;
    void sQuestAccept(Player* player, Quest const* quest) override;
    //void sQuestSelect(Player* player, Quest const* quest);
    //void sQuestComplete(Player* player, Quest const* quest);
    void sQuestReward(Player* player, Quest const* quest, uint32 opt) override;
    void sOnGameEvent(bool start, uint16 eventId) override;

    uint32 mEscortQuestID;

    void SetDespawnTime (uint32 t)
    {
        mDespawnTime = t;
        mDespawnState = t ? 1 : 0;
    }
    void StartDespawn() { mDespawnState = 2; }

    void OnSpellClick(Unit* clicker, bool& result) override;

    void PathEndReached(uint32 pathId) override;

    bool CanRespawn() override { return mcanSpawn; };
    void SetCanRespawn(bool canSpawn) { mcanSpawn = canSpawn; }

    // Xinef
    void SetWPPauseTimer(uint32 time) { mWPPauseTimer = time; }

private:
    bool mIsCharmed;
    uint32 mFollowCreditType;
    uint32 mFollowArrivedTimer;
    uint32 mFollowCredit;
    uint32 mFollowArrivedEntry;
    bool   mFollowArrivedAlive;
    WOWGUID mFollowGuid;
    float mFollowDist;
    float mFollowAngle;

    void ReturnToLastOOCPos();
    void UpdatePath(const uint32 diff);
    SmartScript mScript;
    WPPath* mWayPoints;
    uint32 mEscortState;
    uint32 mCurrentWPID;
    bool mWPReached;
    bool mOOCReached;
    uint32 mWPPauseTimer;
    WayPoint* mLastWP;
    uint32 mEscortNPCFlags;
    uint32 GetWPCount() { return mWayPoints ? mWayPoints->size() : 0; }
    bool mCanRepeatPath;
    bool mRun;
    bool mEvadeDisabled;
    bool mCanAutoAttack;
    bool mCanCombatMove;
    bool mForcedPaused;
    uint32 mInvincibilityHpLevel;

    bool AssistPlayerInCombatAgainst(Unit* who);

    uint32 mDespawnTime;
    uint32 mDespawnState;
    void UpdateDespawn(const uint32 diff);
    uint32 mEscortInvokerCheckTimer;
    bool mJustReset;

    bool mcanSpawn;

    // Xinef: Vehicle conditions
    void CheckConditions(const uint32 diff);
    ConditionList conditions;
    uint32 m_ConditionsTimer;
};

class SmartGameObjectAI : public GameObjectAI
{
public:
    SmartGameObjectAI(GameObject* g) : GameObjectAI(g) {}
    ~SmartGameObjectAI() override {}

    void UpdateAI(uint32 diff) override;
    void InitializeAI() override;
    void Reset() override;
    SmartScript* GetScript() { return &mScript; }
    static int32 Permissible(GameObject const* /*go*/) { return PERMIT_BASE_NO; }

    bool GossipHello(Player* player, bool reportUse) override;
    bool GossipSelect(Player* player, uint32 sender, uint32 action) override;
    bool GossipSelectCode(Player* /*player*/, uint32 /*sender*/, uint32 /*action*/, const char* /*code*/) override;
    bool QuestAccept(Player* player, Quest const* quest) override;
    bool QuestReward(Player* player, Quest const* quest, uint32 opt) override;
    void Destroyed(Player* player, uint32 eventId) override;
    void SetData(uint32 id, uint32 value) override { SetData(id, value, nullptr); }
    void SetData(uint32 id, uint32 value, Unit* invoker);
    void SetScript9(SmartScriptHolder& e, uint32 entry, Unit* invoker);
    void OnGameEvent(bool start, uint16 eventId) override;
    void OnStateChanged(uint32 state, Unit* unit) override;
    void EventInform(uint32 eventId) override;
    void SpellHit(Unit* unit, SpellInfo const* spellInfo) override;

    // Called when the gameobject summon successfully other creature
    void JustSummoned(Creature* creature) override;

    // Called when a summoned creature dissapears (UnSummoned)
    void SummonedCreatureDespawn(Creature* unit) override;

    // Called when a summoned unit dies
    void SummonedCreatureDies(Creature* summon, Unit* killer) override;

    // Called when a summoned unit evades
    void SummonedCreatureEvade(Creature* summon) override;

protected:
    SmartScript mScript;
};

/// Registers scripts required by the SAI scripting system
void AddSC_SmartScripts();

#endif
