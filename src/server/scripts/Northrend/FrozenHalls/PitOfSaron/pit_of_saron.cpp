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

#include "pit_of_saron.h"
#include "AreaTriggerScript.h"
#include "CreatureGroups.h"
#include "CreatureScript.h"
#include "MapMgr.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SmartAI.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"

class npc_pos_leader : public CreatureScript
{
public:
    npc_pos_leader() : CreatureScript("npc_pos_leader") { }

    struct npc_pos_leaderAI: public NullCreatureAI
    {
        npc_pos_leaderAI(Creature* creature) : NullCreatureAI(creature), summons(me)
        {
            pInstance = me->GetInstanceScript();
        }

        EventMap events;
        SummonList summons;
        InstanceScript* pInstance;
        uint8 counter;

        void Reset() override
        {
            counter = 0;
            events.Reset();
            summons.DespawnAll();
            me->SetVisible(true);

            if (pInstance)
                switch (pInstance->GetData(DATA_INSTANCE_PROGRESS))
                {
                    case INSTANCE_PROGRESS_NONE:
                        me->SetVisible(false);
                        break;
                }
        }

        void SetData(uint32 type, uint32  /*val*/) override
        {
            if (type == DATA_START_INTRO && pInstance->GetData(DATA_INSTANCE_PROGRESS) == INSTANCE_PROGRESS_NONE && counter == 0 && !me->IsVisible())
            {
                me->setActive(true);
                events.RescheduleEvent(1, 0ms);
            }
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);
            switch(events.ExecuteEvent())
            {
                case 0:
                    break;
                case 1:
                    {
                        if (counter == 0)
                        {
                            me->SetVisible(true);
                            me->GetMotionMaster()->MovePoint(1, LeaderIntroPos);
                        }

                        uint8 idx = 0;
                        if (pInstance)
                            idx = (pInstance->GetData(DATA_TEAMID_IN_INSTANCE) == TEAM_ALLIANCE ? 0 : 1);
                        if (introPositions[counter].entry[idx] != 0)
                        {
                            if (Creature* summon = me->SummonCreature(introPositions[counter].entry[idx], PortalPos))
                            {
                                summon->RemoveUnitMovementFlag(MOVEFLAG_WALK);
                                summon->SetSpeed(MOVE_RUN, 0.8f);
                                summon->GetMotionMaster()->MovePoint(1, introPositions[counter].endPosition);
                                summon->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_READY1H);
                            }

                            ++counter;
                            events.Repeat(150ms);
                        }
                        else
                        {
                            events.RescheduleEvent(2, 2500ms);
                        }
                    }
                    break;
                case 2:
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_TYRANNUS_EVENT_GUID)))
                        {
                            c->setActive(true);
                            c->AI()->Talk(SAY_TYRANNUS_INTRO_1);
                        }

                    events.RescheduleEvent(3, 7s);
                    break;
                case 3:
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_TYRANNUS_EVENT_GUID)))
                            c->AI()->Talk(SAY_TYRANNUS_INTRO_2);

                    events.RescheduleEvent(4, 14s);
                    break;
                case 4:
                    if (pInstance)
                    {
                        Creature* n1 = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_NECROLYTE_1_GUID));
                        Creature* n2 = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_NECROLYTE_2_GUID));
                        if (n1 && n2)
                        {
                            if (!n1->IsInCombat() && n1->IsAlive())
                            {
                                n1->AddUnitMovementFlag(MOVEFLAG_WALK);
                                n1->GetMotionMaster()->MovePoint(1, NecrolytePos1);
                                n1->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_READY1H);
                            }
                            if (!n2->IsInCombat() && n2->IsAlive())
                            {
                                n2->AddUnitMovementFlag(MOVEFLAG_WALK);
                                n2->GetMotionMaster()->MovePoint(1, NecrolytePos2);
                                n2->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_READY1H);
                            }
                            /// @todo This spell check is invalid
                            //                            if (SPELL_NECROLYTE_CHANNELING)
                            //                            {
                            n1->RemoveAura(SPELL_NECROLYTE_CHANNELING);
                            n2->RemoveAura(SPELL_NECROLYTE_CHANNELING);
                            //                            }

                            for (SummonList::iterator itr = summons.begin(); itr != summons.end(); ++itr)
                                if (Creature* c = pInstance->instance->GetCreature(*itr))
                                {
                                    if (c->GetPositionX() < 440.0f)
                                        continue;
                                    if (c->GetPositionY() > 215.0f)
                                        c->GetMotionMaster()->MoveChase(n2, 0.0f, rand_norm() * 2 * M_PI);
                                    else
                                        c->GetMotionMaster()->MoveChase(n1, 0.0f, rand_norm() * 2 * M_PI);
                                }
                        }
                    }

                    events.RescheduleEvent(5, 1ms);
                    break;
                case 5:
                    Talk(me->GetEntry() == NPC_JAINA_PART1 ? SAY_JAINA_INTRO_1 : SAY_SYLVANAS_INTRO_1);

                    events.RescheduleEvent(6, 1s);
                    break;
                case 6:
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_TYRANNUS_EVENT_GUID)))
                            c->AI()->Talk(SAY_TYRANNUS_INTRO_3);

                    events.RescheduleEvent(7, 5s);
                    break;
                case 7: /// @todo: (Initial RP, when zoning in the instance) is not complete.
                    if (pInstance)
                    {
                        if (Creature* n1 = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_NECROLYTE_1_GUID)))
                            n1->AI()->DoAction(1337); // remove invincibility
                        if (Creature* n2 = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_NECROLYTE_2_GUID)))
                            n2->AI()->DoAction(1337); // remove invincibility

                        for (SummonList::iterator itr = summons.begin(); itr != summons.end(); ++itr)
                            if (Creature* c = pInstance->instance->GetCreature(*itr))
                            {
                                if (c->GetPositionX() < 450.0f)
                                    continue;
                                c->AddUnitState(UNIT_STATE_NO_ENVIRONMENT_UPD);
                                c->GetMotionMaster()->Clear(false);
                                c->GetMotionMaster()->MoveIdle();
                                c->StopMoving();
                                c->CastSpell(c, 69413, true);
                                c->SetCanFly(true);
                                c->SetDisableGravity(true);
                                c->SendMovementFlagUpdate();
                                float dist = rand_norm() * 2.0f;
                                float angle = rand_norm() * 2 * M_PI;
                                c->GetMotionMaster()->MoveTakeoff(0, c->GetPositionX() + dist * cos(angle), c->GetPositionY() + dist * std::sin(angle), c->GetPositionZ() + 6.0f + (float)urand(0, 4), 1.5f + frand(0.0f, 1.5f));
                            }
                    }

                    events.RescheduleEvent(8, 7s);
                    break;
                case 8:
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_TYRANNUS_EVENT_GUID)))
                            c->CastSpell(c, 69753, false);

                    events.RescheduleEvent(9, 400ms);
                    break;
                case 9:
                    if (pInstance)
                        for (SummonList::iterator itr = summons.begin(); itr != summons.end(); ++itr)
                            if (Creature* c = pInstance->instance->GetCreature(*itr))
                            {
                                if (c->GetPositionX() < 450.0f)
                                    continue;
                                Unit::Kill(c, c);
                                c->RemoveAllAuras();
                                c->GetMotionMaster()->MoveFall(0, true);
                            }

                    events.RescheduleEvent(10, 1s);
                    break;
                case 10:
                    Talk(me->GetEntry() == NPC_JAINA_PART1 ? SAY_JAINA_INTRO_2 : SAY_SYLVANAS_INTRO_2);

                    events.RescheduleEvent(11, 1s);
                    break;
                case 11:
                    if (pInstance)
                        for (SummonList::iterator itr = summons.begin(); itr != summons.end(); ++itr)
                            if (Creature* c = pInstance->instance->GetCreature(*itr))
                            {
                                if (c->GetPositionX() < 450.0f)
                                    continue;
                                c->SetCanFly(false);
                                c->SetDisableGravity(false);
                                c->SendMovementFlagUpdate();
                                c->CastSpell(c, 69350, true);
                            }

                    events.RescheduleEvent(12, 2s);
                    break;
                case 12:
                    if (pInstance)
                    {
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_TYRANNUS_EVENT_GUID)))
                            c->AI()->Talk(SAY_TYRANNUS_INTRO_4);

                        for (SummonList::iterator itr = summons.begin(); itr != summons.end(); ++itr)
                            if (Creature* c = pInstance->instance->GetCreature(*itr))
                            {
                                if (c->GetPositionX() < 450.0f)
                                    continue;
                                c->SetHomePosition(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ(), c->GetOrientation());
                                c->Respawn(true);
                                c->UpdateEntry(36796, 0, false);
                                c->SetFacingTo(M_PI);
                                c->SetReactState(REACT_PASSIVE);
                            }
                    }

                    events.RescheduleEvent(13, 3s);
                    break;
                case 13:
                    if (pInstance)
                    {
                        for (SummonList::iterator itr = summons.begin(); itr != summons.end(); ++itr)
                            if (Creature* c = pInstance->instance->GetCreature(*itr))
                            {
                                if (c->GetPositionX() < 450.0f)
                                    continue;
                                c->RemoveUnitMovementFlag(MOVEFLAG_WALK);
                                float dist = rand_norm();
                                float angle = rand_norm() * 2 * M_PI;
                                c->SetSpeed(MOVE_RUN, 0.8f);
                                c->SetInCombatWithZone();
                                c->GetMotionMaster()->MoveChase(me, dist, angle);
                                c->SetHomePosition(me->GetPositionX() + dist * cos(angle), me->GetPositionY() + dist * std::sin(angle), me->GetPositionZ(), 0.0f);
                            }
                    }

                    events.RescheduleEvent(14, 2s);
                    break;
                case 14:
                    if (pInstance)
                    {
                        if (me->GetEntry() == NPC_JAINA_PART1)
                        {
                            Talk(SAY_JAINA_INTRO_3);
                            me->CastSpell(me, 70132, false);
                        }
                        else
                        {
                            me->CastSpell(me, 59514, false);
                            for (uint8 i = 0; i < 2; ++i)
                                if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_GUARD_1_GUID + i)))
                                    c->CastSpell(c, 70513, false);
                        }
                    }

                    events.RescheduleEvent(15, 2s);
                    break;
                case 15:
                    if (pInstance)
                    {
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_TYRANNUS_EVENT_GUID)))
                            c->GetMotionMaster()->MovePoint(0, SBSTyrannusStartPos);

                        if (me->GetEntry() == NPC_JAINA_PART1)
                        {
                            for (uint8 i = 0; i < 2; ++i)
                                if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_GUARD_1_GUID + i)))
                                    c->CastSpell(c, 70464, false);
                        }
                    }

                    events.RescheduleEvent(16, 3s);
                    break;
                case 16:
                    Talk(me->GetEntry() == NPC_JAINA_PART1 ? SAY_JAINA_INTRO_4 : SAY_SYLVANAS_INTRO_3);
                    if (pInstance)
                    {
                        for (uint8 i = 0; i < 2; ++i)
                            if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_GUARD_1_GUID + i)))
                                c->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);

                        for (SummonList::iterator itr = summons.begin(); itr != summons.end(); ++itr)
                            if (Creature* c = pInstance->instance->GetCreature(*itr))
                            {
                                if (c->IsAlive())
                                {
                                    if (c->GetEntry() == NPC_KALIRA || c->GetEntry() == NPC_ELANDRA || c->GetEntry() == NPC_LORALEN || c->GetEntry() == NPC_KORELN)
                                        continue;

                                    Unit::Kill(c, c, false);
                                }
                                c->DespawnOrUnsummon(10000);
                            }
                        pInstance->SetData(DATA_INSTANCE_PROGRESS, INSTANCE_PROGRESS_FINISHED_INTRO);
                    }

                    events.RescheduleEvent(17, 5s);
                    break;
                case 17:
                    me->setActive(false);
                    Talk(me->GetEntry() == NPC_JAINA_PART1 ? SAY_JAINA_INTRO_5 : SAY_SYLVANAS_INTRO_4);

                    break;
            }

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }

        void JustSummoned(Creature* s) override
        {
            summons.Summon(s);
        }

        void SummonedCreatureDespawn(Creature* s) override
        {
            summons.Despawn(s);
        }

        void AttackStart(Unit*  /*who*/) override {}
        void MoveInLineOfSight(Unit*  /*who*/) override {}
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetPitOfSaronAI<npc_pos_leaderAI>(creature);
    }
};

class npc_pos_deathwhisper_necrolyte : public CreatureScript
{
public:
    npc_pos_deathwhisper_necrolyte() : CreatureScript("npc_pos_deathwhisper_necrolyte") { }

    struct npc_pos_deathwhisper_necrolyteAI: public ScriptedAI
    {
        npc_pos_deathwhisper_necrolyteAI(Creature* creature) : ScriptedAI(creature)
        {
            pInstance = me->GetInstanceScript();
            isInvincible = false;
        }

        EventMap events;
        InstanceScript* pInstance;
        bool isInvincible;

        void Reset() override
        {
            events.Reset();
        }

        void InitializeAI() override
        {
            if (pInstance && pInstance->GetData(DATA_INSTANCE_PROGRESS) == INSTANCE_PROGRESS_NONE)
            {
                if ((me->GetPositionX() > 490.0f && me->GetPositionX() < 504.0f && me->GetPositionY() > 192.0f && me->GetPositionY() < 206.0f) ||
                        (me->GetPositionX() > 490.0f && me->GetPositionX() < 504.0f && me->GetPositionY() > 240.0f && me->GetPositionY() < 254.0f))
                {
                    isInvincible = true;

                    /// @todo This spell check is invalid
                    //                    if (SPELL_NECROLYTE_CHANNELING)
                    me->CastSpell(me, SPELL_NECROLYTE_CHANNELING, false);

                    if (me->GetPositionY() < 206.0f)
                    {
                        pInstance->SetGuidData(DATA_NECROLYTE_1_GUID, me->GetGUID());
                    }
                    else
                    {
                        pInstance->SetGuidData(DATA_NECROLYTE_2_GUID, me->GetGUID());
                    }
                }
            }
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type == POINT_MOTION_TYPE && id == 1)
                me->SetFacingTo(M_PI);
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            /// @todo This spell check is invalid
            //            if (SPELL_NECROLYTE_CHANNELING)
            me->RemoveAura(SPELL_NECROLYTE_CHANNELING);
            events.Reset();
            events.RescheduleEvent(1, 0ms);
            events.RescheduleEvent(2, 5s, 9s);

            if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_LEADER_FIRST_GUID)))
                c->AI()->SetData(DATA_START_INTRO, 0);
        }

        void DamageTaken(Unit* /*doneBy*/, uint32& damage, DamageEffectType, SpellSchoolMask) override
        {
            if (isInvincible && damage >= me->GetHealth())
                damage = me->GetHealth() - 1;
        }

        void DoAction(int32 a) override
        {
            if (a == 1337)
                isInvincible = false;
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            switch(events.ExecuteEvent())
            {
                case 0:
                    break;
                case 1: // Shadow Bolt
                    me->CastSpell(me->GetVictim(), 69577, false);
                    events.Repeat(4s);
                    break;
                case 2: // Conversion Beam
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 30.0f, true))
                        me->CastSpell(target, 69578, false);
                    events.Repeat(20s, 25s);
                    break;
            }

            DoMeleeAttackIfReady();
        }

        void MoveInLineOfSight(Unit*  /*who*/) override {}
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetPitOfSaronAI<npc_pos_deathwhisper_necrolyteAI>(creature);
    }
};

class npc_pos_after_first_boss : public CreatureScript
{
public:
    npc_pos_after_first_boss() : CreatureScript("npc_pos_after_first_boss") { }

    struct npc_pos_after_first_bossAI: public NullCreatureAI
    {
        npc_pos_after_first_bossAI(Creature* creature) : NullCreatureAI(creature)
        {
            pInstance = me->GetInstanceScript();
            me->GetMotionMaster()->MovePoint(1, 695.03f, -149.86f, 527.89f);
        }

        EventMap events;
        InstanceScript* pInstance;

        void Reset() override
        {
            events.Reset();
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type != POINT_MOTION_TYPE)
                return;
            switch(id)
            {
                case 1:
                    events.RescheduleEvent(id, 0ms);
                    break;
            }
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);
            switch(events.ExecuteEvent())
            {
                case 0:
                    break;
                case 1:
                    {
                        if (pInstance)
                            if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_GARFROST_GUID)))
                            {
                                float angle = c->GetAngle(me);
                                float x = c->GetPositionX() + cos(angle) * 12.0f;
                                float y = c->GetPositionY() + std::sin(angle) * 12.0f;
                                me->GetMotionMaster()->MovePoint(2, x, y, c->GetPositionZ());
                            }

                        uint8 i = 0;
                        while (FBSData[i].entry)
                        {
                            if (Creature* c = me->SummonCreature(FBSData[i].entry, 688.69f + i * 1.8f, FBSSpawnPos.GetPositionY() + (float)irand(-2, 2), FBSSpawnPos.GetPositionZ(), 3 * M_PI / 2))
                                c->GetMotionMaster()->MovePath(FBSData[i].pathId, false);
                            ++i;
                        }
                        events.RescheduleEvent(2, 3s);
                        break;
                    }
                case 2:
                    if (Creature* c = me->SummonCreature(NPC_TYRANNUS_VOICE, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() - 10.0f, me->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, 1))
                        c->AI()->Talk(SAY_TYRANNUS_GARFROST);

                    events.RescheduleEvent(3, 4s);
                    break;
                case 3:
                    Talk(SAY_GENERAL_GARFROST);

                    break;
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetPitOfSaronAI<npc_pos_after_first_bossAI>(creature);
    }
};

class npc_pos_tyrannus_events : public CreatureScript
{
public:
    npc_pos_tyrannus_events() : CreatureScript("npc_pos_tyrannus_events") { }

    struct npc_pos_tyrannus_eventsAI: public NullCreatureAI
    {
        npc_pos_tyrannus_eventsAI(Creature* creature) : NullCreatureAI(creature)
        {
            pInstance = me->GetInstanceScript();
            killsLeft = 0;
        }

        InstanceScript* pInstance;
        EventMap events;
        uint32 killsLeft;
        WOWGUID deathbringerGUID[2];

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type == POINT_MOTION_TYPE)
            {
                switch (id)
                {
                    case 0:
                        me->setActive(false);
                        break;
                    case 1:
                        events.ScheduleEvent(1, 0ms);
                        break;
                    case 2:
                        events.ScheduleEvent(2, 0ms);
                        break;
                    case 3:
                        events.ScheduleEvent(4, 0ms);
                        break;
                }
            }
            else if (type == EFFECT_MOTION_TYPE && id == 10)
                events.ScheduleEvent(6, 0ms);
        }

        void SetData(uint32 type, uint32 id) override
        {
            if (!me->IsAlive() || pInstance->GetData(DATA_GARFROST) != DONE || pInstance->GetData(DATA_ICK) != DONE)
                return;
            if (type == 2)
            {
                if (id == 1)
                    if (killsLeft) --killsLeft;
                return;
            }
            if (type != 1)
                return;
            switch (id)
            {
                case 1:
                    if (pInstance->GetData(DATA_INSTANCE_PROGRESS) != INSTANCE_PROGRESS_FINISHED_KRICK_SCENE)
                        return;
                    if (me->GetExactDist(&PTSTyrannusWaitPos1) > 3.0f)
                        return;
                    pInstance->SetData(DATA_INSTANCE_PROGRESS, INSTANCE_PROGRESS_AFTER_WARN_1);
                    Talk(SAY_TYRANNUS_AMBUSH_1);
                    killsLeft = 10;
                    events.ScheduleEvent(30, 0ms);
                    events.ScheduleEvent(3, 25s);
                    break;
                case 2:
                    if (pInstance->GetData(DATA_INSTANCE_PROGRESS) != INSTANCE_PROGRESS_AFTER_WARN_1)
                        return;
                    if (killsLeft != 0)
                        return;
                    pInstance->SetData(DATA_INSTANCE_PROGRESS, INSTANCE_PROGRESS_AFTER_WARN_2);
                    Talk(SAY_TYRANNUS_AMBUSH_2);
                    killsLeft = (Difficulty(me->GetMap()->GetSpawnMode()) == DUNGEON_DIFFICULTY_HEROIC ? 12 : 6);
                    events.ScheduleEvent(60, 0ms);
                    events.ScheduleEvent(5, 20s);
                    break;
                    break;
                case 3:
                    if (pInstance->GetData(DATA_INSTANCE_PROGRESS) != INSTANCE_PROGRESS_AFTER_WARN_2)
                        return;
                    if (killsLeft != 0)
                        return;
                    pInstance->SetData(DATA_INSTANCE_PROGRESS, INSTANCE_PROGRESS_AFTER_TUNNEL_WARN);
                    if (Creature* c = me->SummonCreature(NPC_TYRANNUS_VOICE, 950.16f, -102.17f, 594.90f - 10.0f, 5.43f, TEMPSUMMON_TIMED_DESPAWN, 1))
                        c->AI()->Talk(SAY_TYRANNUS_TRAP_TUNNEL);
                    break;
            }
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);
            switch (events.ExecuteEvent())
            {
                case 1:
                    me->GetMotionMaster()->MovePoint(2, PTSTyrannusWaitPos1, false);
                    break;
                case 2:
                    me->SetFacingTo(PTSTyrannusWaitPos1.GetOrientation());
                    me->setActive(false);
                    break;
                case 3:
                    me->GetMotionMaster()->MovePoint(3, PTSTyrannusWaitPos2, false);
                    break;
                case 4:
                    me->SetFacingTo(PTSTyrannusWaitPos2.GetOrientation());
                    break;
                case 5:
                    me->GetMotionMaster()->MoveTakeoff(10, me->GetPositionX() + 2.0f * cos(me->GetOrientation()), me->GetPositionY() + 2.0f * std::sin(me->GetOrientation()), me->GetPositionZ() + 30.0f, 7.0f);
                    break;
                case 6:
                    me->GetMotionMaster()->MovePoint(4, PTSTyrannusWaitPos3, false);
                    break;
                case 30:
                    {
                        Movement::PointsArray path;
                        path.push_back(G3D::Vector3(950.61f, 50.91f, 567.85f));
                        path.push_back(G3D::Vector3(946.48f, 73.25f, 565.89f));
                        path.push_back(G3D::Vector3(934.87f, 78.56f, 563.97f));
                        path.push_back(G3D::Vector3(915.10f, 75.31f, 553.81f));
                        if (Creature* c = me->SummonCreature(NPC_YMIRJAR_DEATHBRINGER, 950.61f, 50.91f, 567.85f, 1.82f))
                        {
                            deathbringerGUID[0] = c->GetGUID();
                            c->SetReactState(REACT_PASSIVE);
                            c->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                            c->SetHomePosition(915.10f, 75.31f, 553.81f, 3.75f);
                            c->SetWalk(false);
                            c->GetMotionMaster()->MoveSplinePath(&path);
                        }
                        if (Creature* c = me->SummonCreature(NPC_YMIRJAR_DEATHBRINGER, 949.05f, 61.18f, 566.60f, 1.73f))
                        {
                            deathbringerGUID[1] = c->GetGUID();
                            c->SetReactState(REACT_PASSIVE);
                            c->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                            c->SetHomePosition(883.15f, 54.6254f, 528.5f, 3.75f);
                            c->SetWalk(false);
                            path.push_back(G3D::Vector3(883.15f, 54.6254f, 528.5f));
                            c->GetMotionMaster()->MoveSplinePath(&path);
                        }
                        events.ScheduleEvent(31, 500ms);
                        events.ScheduleEvent(32, 500ms);
                    }
                    break;
                case 31:
                    if (Creature* c = pInstance->instance->GetCreature(deathbringerGUID[0]))
                        if (c->GetMotionMaster()->GetCurrentMovementGeneratorType() != ESCORT_MOTION_TYPE)
                        {
                            c->CastSpell(c, 69516, false);
                            events.ScheduleEvent(33, 3s);
                            break;
                        }
                    events.ScheduleEvent(31, 500ms);
                    break;
                case 32:
                    if (Creature* c = pInstance->instance->GetCreature(deathbringerGUID[1]))
                        if (c->GetMotionMaster()->GetCurrentMovementGeneratorType() != ESCORT_MOTION_TYPE)
                        {
                            c->CastSpell(c, 69516, false);
                            events.ScheduleEvent(34, 3s);
                            break;
                        }
                    events.ScheduleEvent(32, 500ms);
                    break;
                case 33:
                    me->SummonCreature(NPC_YMIRJAR_WRATHBRINGER, 919.733f, 89.0972f, 558.959f, 3.85718f);
                    me->SummonCreature(NPC_YMIRJAR_WRATHBRINGER, 911.936f, 63.3542f, 547.698f, 3.735f);
                    me->SummonCreature(NPC_YMIRJAR_FLAMEBEARER, 909.356f, 83.1684f, 551.717f, 3.57792f);
                    me->SummonCreature(NPC_YMIRJAR_FLAMEBEARER, 920.946f, 69.1667f, 557.594f, 3.1765f);
                    events.ScheduleEvent(35, 3500ms);
                    break;
                case 34:
                    me->SummonCreature(NPC_YMIRJAR_WRATHBRINGER, 879.464f, 41.1997f, 521.394f, 3.735f);
                    me->SummonCreature(NPC_YMIRJAR_WRATHBRINGER, 885.715f, 65.5156f, 533.631f, 3.85718f);
                    me->SummonCreature(NPC_YMIRJAR_FLAMEBEARER, 876.884f, 61.0139f, 527.715f, 3.57792f);
                    me->SummonCreature(NPC_YMIRJAR_FLAMEBEARER, 889.49f, 45.2865f, 527.233f, 3.97935f);
                    events.ScheduleEvent(36, 3500ms);
                    break;
                case 35:
                    if (Creature* c = pInstance->instance->GetCreature(deathbringerGUID[0]))
                    {
                        c->SetReactState(REACT_AGGRESSIVE);
                        c->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    }
                    break;
                case 36:
                    if (Creature* c = pInstance->instance->GetCreature(deathbringerGUID[1]))
                    {
                        c->SetReactState(REACT_AGGRESSIVE);
                        c->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    }
                    break;
                case 60:
                    {
                        Position startPos[6] = { {927.11f, -72.60f, 592.2f, 1.52f}, {922.92f, -72.64f, 592.3f, 1.52f}, {930.46f, -72.57f, 592.1f, 1.52f}, {934.52f, -72.52f, 592.1f, 1.52f}, {934.57f, -77.66f, 592.20f, 1.52f}, {927.15f, -77.07f, 592.20f, 1.52f} };
                        Position endPos = {926.10f, -46.63f, 591.2f, 1.52f};
                        for (uint8 i = 0; i < 6; ++i)
                            if (Creature* s = me->SummonCreature(i < 4 ? NPC_FALLEN_WARRIOR : NPC_WRATHBONE_COLDWRAITH, startPos[i]))
                            {
                                s->RemoveUnitMovementFlag(MOVEFLAG_WALK);
                                Position finalPos = endPos;
                                s->MovePosition(finalPos, startPos[i].GetExactDist(&startPos[0]), Position::NormalizeOrientation(startPos[i].GetAngle(&startPos[0]) + 1.52f));

                                Movement::PointsArray path;
                                path.push_back(G3D::Vector3(s->GetPositionX(), s->GetPositionY(), s->GetPositionZ()));
                                path.push_back(G3D::Vector3(finalPos.GetPositionX(), finalPos.GetPositionY(), finalPos.GetPositionZ()));

                                s->SetHomePosition(finalPos);
                                s->GetMotionMaster()->MoveSplinePath(&path);
                            }

                        if (Difficulty(me->GetMap()->GetSpawnMode()) == DUNGEON_DIFFICULTY_HEROIC)
                        {
                            Position startPos[6] = { {925.485f, -65.67f, 592.5f, 1.4f}, {921.77f, -65.10f, 592.5f, 1.4f}, {929.19f, -66.24f, 592.5f, 1.4f}, {932.46f, -66.74f, 592.5f, 1.4f}, {924.66f, -71.03f, 592.5f, 1.4f}, {928.81f, -71.66f, 592.5f, 1.4f} };
                            Position middlePos = {928.43f, -29.31f, 589.0f, 1.4f};
                            Position endPos = {937.8f, 21.20f, 574.6f, 1.4f};
                            for (uint8 i = 0; i < 6; ++i)
                                if (Creature* s = me->SummonCreature(i < 4 ? NPC_FALLEN_WARRIOR : NPC_WRATHBONE_COLDWRAITH, startPos[i]))
                                {
                                    s->RemoveUnitMovementFlag(MOVEFLAG_WALK);
                                    Position midPos = middlePos;
                                    Position finalPos = endPos;
                                    s->MovePosition(midPos, startPos[i].GetExactDist(&startPos[0]), Position::NormalizeOrientation(startPos[i].GetAngle(&startPos[0]) + 1.4f));
                                    s->MovePosition(finalPos, startPos[i].GetExactDist(&startPos[0]), Position::NormalizeOrientation(startPos[i].GetAngle(&startPos[0]) + 1.4f));

                                    Movement::PointsArray path;
                                    path.push_back(G3D::Vector3(s->GetPositionX(), s->GetPositionY(), s->GetPositionZ()));
                                    path.push_back(G3D::Vector3(midPos.GetPositionX(), midPos.GetPositionY(), midPos.GetPositionZ()));
                                    path.push_back(G3D::Vector3(finalPos.GetPositionX(), finalPos.GetPositionY(), finalPos.GetPositionZ()));

                                    s->SetHomePosition(finalPos);
                                    s->GetMotionMaster()->MoveSplinePath(&path);
                                }
                        }
                    }
                    break;
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetPitOfSaronAI<npc_pos_tyrannus_eventsAI>(creature);
    }
};

class npc_pos_icicle_trigger : public CreatureScript
{
public:
    npc_pos_icicle_trigger() : CreatureScript("npc_pos_icicle_trigger") { }

    struct npc_pos_icicle_triggerAI: public NullCreatureAI
    {
        npc_pos_icicle_triggerAI(Creature* creature) : NullCreatureAI(creature)
        {
            pInstance = me->GetInstanceScript();
            timer = urand(0, 16000);
        }

        InstanceScript* pInstance;
        uint16 timer;

        void UpdateAI(uint32 diff) override
        {
            if (!pInstance)
                return;
            if (timer <= diff)
            {
                if (pInstance->GetData(DATA_INSTANCE_PROGRESS) == INSTANCE_PROGRESS_AFTER_TUNNEL_WARN)
                    me->CastSpell(me, SPELL_TUNNEL_ICICLE, false);
                timer = urand(16000, 24000);
            }
            else
                timer -= diff;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetPitOfSaronAI<npc_pos_icicle_triggerAI>(creature);
    }
};

class npc_pos_collapsing_icicle : public CreatureScript
{
public:
    npc_pos_collapsing_icicle() : CreatureScript("npc_pos_collapsing_icicle") { }

    struct npc_pos_collapsing_icicleAI: public NullCreatureAI
    {
        npc_pos_collapsing_icicleAI(Creature* creature) : NullCreatureAI(creature)
        {
            pInstance = me->GetInstanceScript();
            timer1 = 2500;
            timer2 = 7000;
        }

        InstanceScript* pInstance;
        uint16 timer1;
        uint16 timer2;

        void SpellHitTarget(Unit* target, SpellInfo const* spell) override
        {
            if (target && spell && target->IsPlayer() && spell->Id == 70827 && pInstance)
                pInstance->SetData(DATA_ACHIEV_DONT_LOOK_UP, 0);
        }

        void UpdateAI(uint32 diff) override
        {
            if (timer1 <= diff)
            {
                me->CastSpell(me, 69428, false);
                me->CastSpell(me, 69426, true);
                timer1 = 60000;
            }
            else
                timer1 -= diff;

            if (timer2 <= diff)
            {
                me->SetDisplayId(11686);
                timer2 = 60000;
            }
            else
                timer2 -= diff;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetPitOfSaronAI<npc_pos_collapsing_icicleAI>(creature);
    }
};

class npc_pos_martin_or_gorkun_second : public CreatureScript
{
public:
    npc_pos_martin_or_gorkun_second() : CreatureScript("npc_pos_martin_or_gorkun_second") { }

    struct npc_pos_martin_or_gorkun_secondAI: public NullCreatureAI
    {
        npc_pos_martin_or_gorkun_secondAI(Creature* creature) : NullCreatureAI(creature), summons(me)
        {
            pInstance = me->GetInstanceScript();
            me->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_READY1H);
            i = 0;
            events.Reset();
            events.RescheduleEvent(1, 500ms);
            events.RescheduleEvent(2, 15s);

            if (pInstance)
                if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_TYRANNUS_GUID)))
                {
                    c->AI()->Talk(SAY_BOSS_TYRANNUS_INTRO_1);
                    c->SetImmuneToPC(false);
                    c->SetReactState(REACT_AGGRESSIVE);
                    //c->ClearUnitState(UNIT_STATE_ONVEHICLE);
                    if (Player* plr = c->SelectNearestPlayer(100.0f))
                    {
                        c->AI()->AttackStart(plr);
                        DoZoneInCombat(c);
                    }
                }
        }

        InstanceScript* pInstance;
        EventMap events;
        SummonList summons;
        uint8 i;

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type == POINT_MOTION_TYPE && id == 2)
            {
                events.RescheduleEvent(5, 1s);
            }
        }

        void DoAction(int32 p) override
        {
            if (p == 1)
                summons.DespawnAll();
            else if (p == 2)
            {
                events.Reset();
                summons.DespawnEntry(NPC_FALLEN_WARRIOR);

                if (!pInstance)
                    return;

                me->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
                me->GetMotionMaster()->MovePoint(2, TSCenterPos);

                TSSpawnPos.GetAngle(&TSMidPos);

                for (WOWGUID const& guid : summons)
                    if (Creature* c = pInstance->instance->GetCreature(guid))
                    {
                        float hx, hy, hz, ho;
                        c->GetHomePosition(hx, hy, hz, ho);
                        c->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_CHEER);
                        float ang = frand(1.92f, 2.36f);
                        float dist = urand(50, 85);
                        c->GetMotionMaster()->MovePoint(0, TSSpawnPos.GetPositionX() + cos(ang)*dist, TSSpawnPos.GetPositionY() + std::sin(ang)*dist, 628.2f);
                    }
            }
            else if (p == 3)
            {
                if (pInstance)
                    if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_SINDRAGOSA_GUID)))
                    {
                        for (WOWGUID const& guid : summons)
                            if (Creature* s = pInstance->instance->GetCreature(guid))
                                if (s->IsAlive())
                                    Unit::Kill(c, s);
                        if (me->IsAlive())
                            Unit::Kill(c, me);
                    }
            }
        }

        void JustSummoned(Creature* s) override
        {
            summons.Summon(s);
        }

        void SummonedCreatureDespawn(Creature* s) override
        {
            summons.Despawn(s);
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);

            switch(events.ExecuteEvent())
            {
                case 0:
                    break;
                case 1:
                    if (TSData[i].entry)
                    {
                        if (Creature* c = me->SummonCreature(TSData[i].entry, TSSpawnPos))
                        {
                            c->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_READY1H);
                            c->GetMotionMaster()->MovePoint(0, TSData[i].x, TSData[i].y, TSHeight);
                        }
                        ++i;
                        events.ScheduleEvent(1, 150ms);
                    }
                    break;
                case 2:
                    Talk(me->GetEntry() == NPC_MARTIN_VICTUS_2 ? SAY_GENERAL_ALLIANCE_TRASH : SAY_GENERAL_HORDE_TRASH);
                    events.RescheduleEvent(3, 8s);
                    break;
                case 3:
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_TYRANNUS_GUID)))
                            c->AI()->Talk(SAY_BOSS_TYRANNUS_INTRO_2);

                    me->SetFacingTo(5.26f);
                    me->SetOrientation(5.26f);
                    me->SetHomePosition(*me);
                    for (WOWGUID const& guid : summons)
                        if (Creature* c = pInstance->instance->GetCreature(guid))
                        {
                            c->SetFacingTo(5.26f);
                            c->SetOrientation(5.26f);
                            c->SetHomePosition(*c);
                        }
                    events.RescheduleEvent(10, 15s);

                    events.RescheduleEvent(4, 15s);
                    break;
                case 4:
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_TYRANNUS_GUID)))
                            c->AI()->DoAction(1);
                    break;
                case 5:
                    me->SetFacingTo(TSCenterPos.GetOrientation());
                    Talk(me->GetEntry() == NPC_MARTIN_VICTUS_2 ? SAY_GENERAL_ALLIANCE_OUTRO_1 : SAY_GENERAL_HORDE_OUTRO_1);
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_LEADER_SECOND_GUID)))
                            c->AI()->DoAction(1);
                    break;
                case 10:
                    if (summons.GetEntryCount(NPC_FALLEN_WARRIOR) < 3)
                        if (Creature* c = me->SummonCreature(NPC_FALLEN_WARRIOR, 1060.95f, 102.79f, 630.2f, 2.01f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5000))
                        {
                            float offset = frand(0.0f, 10.0f);
                            c->GetMotionMaster()->MovePoint(0, 1047.0f + offset, 118.0f + offset, 628.2f);
                            c->SetHomePosition(*me);
                            for (WOWGUID const& guid : summons)
                                if (Creature* s = pInstance->instance->GetCreature(guid))
                                {
                                    if (s->GetEntry() == NPC_FALLEN_WARRIOR)
                                        continue;
                                    c->SetInCombatWith(s);
                                    s->SetInCombatWith(c);
                                    c->AddThreat(s, 0.0f);
                                    s->AddThreat(c, 0.0f);
                                }
                        }
                    events.RescheduleEvent(10, 3000);
                    break;
            }

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetPitOfSaronAI<npc_pos_martin_or_gorkun_secondAI>(creature);
    }
};

class npc_pos_freed_slave : public CreatureScript
{
public:
    npc_pos_freed_slave() : CreatureScript("npc_pos_freed_slave") { }

    struct npc_pos_freed_slaveAI: public SmartAI
    {
        npc_pos_freed_slaveAI(Creature* creature) : SmartAI(creature)
        {
            me->SetUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED);
            // immune to falling icicles
            me->ApplySpellImmune(0, IMMUNITY_ID, 69425, true);
            me->ApplySpellImmune(0, IMMUNITY_ID, 70827, true);
        }

        bool CanAIAttack(Unit const* who) const override
        {
            return who->GetEntry() == NPC_FALLEN_WARRIOR;
        }

        void EnterEvadeMode(EvadeReason /* why */) override
        {
            if (!me->IsAlive() || me->IsInEvadeMode())
                return;

            me->RemoveEvadeAuras();
            me->GetThreatMgr().ClearAllThreat();
            me->CombatStop(true);
            me->LoadCreaturesAddon(true);
            me->SetLootRecipient(nullptr);
            me->ResetPlayerDamageReq();
            me->SetLastDamagedTime(0);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetPitOfSaronAI<npc_pos_freed_slaveAI>(creature);
    }
};

class npc_pos_leader_second : public CreatureScript
{
public:
    npc_pos_leader_second() : CreatureScript("npc_pos_leader_second") { }

    struct npc_pos_leader_secondAI: public NullCreatureAI
    {
        npc_pos_leader_secondAI(Creature* creature) : NullCreatureAI(creature)
        {
            pInstance = me->GetInstanceScript();
            barrierGUID.Clear();
            events.Reset();
            me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);

            if (pInstance)
            {
                if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_RIMEFANG_GUID)))
                {
                    c->RemoveAllAuras();
                    c->GetMotionMaster()->Clear();
                    c->GetMotionMaster()->MoveIdle();
                    c->SetVisible(false);
                }
                if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_MARTIN_OR_GORKUN_GUID)))
                {
                    c->AI()->DoAction(2);
                }
            }
        }

        InstanceScript* pInstance;
        EventMap events;
        WOWGUID barrierGUID;

        void DoAction(int32 p) override
        {
            if (p == 1)
            {
                events.RescheduleEvent(1, me->GetEntry() == NPC_JAINA_PART2 ? 15s + 500ms : 18s);
                events.RescheduleEvent(2, me->GetEntry() == NPC_JAINA_PART2 ? 16s + 500ms : 19s);
            }
        }

        void SpellHitTarget(Unit* target, SpellInfo const* spell) override
        {
            if ((spell->Id == SPELL_TELEPORT_JAINA || spell->Id == SPELL_TELEPORT_SYLVANAS) && target && target->IsPlayer())
            {
                float angle = rand_norm() * 2 * M_PI;
                float dist = urand(1, 4);
                target->ToPlayer()->NearTeleportTo(me->GetPositionX() + cos(angle)*dist, me->GetPositionY() + std::sin(angle)*dist, me->GetPositionZ(), me->GetOrientation());
            }
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type != WAYPOINT_MOTION_TYPE)
                return;

            switch(id)
            {
                case 0:
                    Talk(me->GetEntry() == NPC_JAINA_PART2 ? SAY_JAINA_OUTRO_2 : SAY_SYLVANAS_OUTRO_2);
                    break;
                case 1:
                    if (me->GetEntry() == NPC_JAINA_PART2)
                    {
                        Talk(SAY_JAINA_OUTRO_3);
                    }
                    break;
                case 6:
                    me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                    if (GameObject* g = me->FindNearestGameObject(GO_HOR_PORTCULLIS, 50.0f))
                        g->SetGoState(GO_STATE_ACTIVE);
                    break;
            }
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);

            switch(events.ExecuteEvent())
            {
                case 0:
                    break;
                case 1:
                    if (pInstance)
                        if (Creature* c = me->SummonCreature(NPC_SINDRAGOSA, TSSindragosaPos1))
                        {
                            c->SetCanFly(true);
                            c->SetDisableGravity(true);
                            c->GetMotionMaster()->MovePoint(0, TSSindragosaPos2);
                        }

                    break;
                case 2:
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_MARTIN_OR_GORKUN_GUID)))
                            c->AI()->Talk(SAY_GENERAL_OUTRO_2);

                    events.RescheduleEvent(3, me->GetEntry() == NPC_JAINA_PART2 ? 7s : 8s);
                    break;
                case 3:
                    Talk(me->GetEntry() == NPC_JAINA_PART2 ? SAY_JAINA_OUTRO_1 : SAY_SYLVANAS_OUTRO_1);
                    me->CastSpell(me, me->GetEntry() == NPC_JAINA_PART2 ? SPELL_TELEPORT_JAINA_VISUAL : SPELL_TELEPORT_SYLVANAS_VISUAL, true);

                    events.RescheduleEvent(4, 2s);
                    break;
                case 4:
                    me->CastSpell(me, me->GetEntry() == NPC_JAINA_PART2 ? SPELL_TELEPORT_JAINA : SPELL_TELEPORT_SYLVANAS, true);
                    if (GameObject* barrier = me->SummonGameObject(203005, 1055.49f, 115.03f, 628.15f, 2.08f, 0.0f, 0.0f, 0.0f, 0.0f, 86400, false))
                        barrierGUID = barrier->GetGUID();

                    events.RescheduleEvent(5, 1500ms);
                    break;
                case 5:
                    if (pInstance)
                        if (Creature* x = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_MARTIN_OR_GORKUN_GUID)))
                        {
                            if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_SINDRAGOSA_GUID)))
                                c->CastSpell(x->GetPositionX(), x->GetPositionY(), x->GetPositionZ(), SPELL_SINDRAGOSA_FROST_BOMB_POS, true);
                        }

                    events.RescheduleEvent(6, 5s);
                    events.RescheduleEvent(10, 2s);
                    break;
                case 6:
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_SINDRAGOSA_GUID)))
                            c->GetMotionMaster()->MovePoint(0, TSSindragosaPos1);

                    events.RescheduleEvent(7, 4500ms);
                    break;
                case 7:
                    if (pInstance)
                        if (Creature* c = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_SINDRAGOSA_GUID)))
                            c->SetVisible(false);
                    if (GameObject* barrier = pInstance->instance->GetGameObject(barrierGUID))
                        barrier->Delete();
                    barrierGUID.Clear();

                    events.RescheduleEvent(8, 2s);
                    break;
                case 8:
                    me->GetMotionMaster()->MovePath(me->GetEntry() == NPC_JAINA_PART2 ? PATH_BEGIN_VALUE + 16 : PATH_BEGIN_VALUE + 17, false);
                    break;
                case 10:
                    if (Creature* x = pInstance->instance->GetCreature(pInstance->GetGuidData(DATA_MARTIN_OR_GORKUN_GUID)))
                        x->AI()->DoAction(3);

                    break;
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetPitOfSaronAI<npc_pos_leader_secondAI>(creature);
    }
};

enum EmpoweredBlizzard
{
    SPELL_EMPOWERED_BLIZZARD = 70131
};

class spell_pos_empowered_blizzard_aura : public AuraScript
{
    PrepareAuraScript(spell_pos_empowered_blizzard_aura);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_EMPOWERED_BLIZZARD });
    }

    void HandleEffectPeriodic(AuraEffect const*   /*aurEff*/)
    {
        PreventDefaultAction();
        if (Unit* caster = GetCaster())
            caster->CastSpell((float)urand(447, 480), (float)urand(200, 235), 528.71f, SPELL_EMPOWERED_BLIZZARD, true);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pos_empowered_blizzard_aura::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

const Position slaveFreePos[4] =
{
    {699.82f, -82.68f, 512.6f, 0.0f},
    {643.51f, 79.20f, 511.57f, 0.0f},
    {800.09f, 78.66f, 510.2f, 0.0f},
    {528.26f, 187.04f, 528.75f, 0.0f}
};

class SlaveRunEvent : public BasicEvent
{
public:
    SlaveRunEvent(Creature& owner) : _owner(owner) { }

    bool Execute(uint64 /*eventTime*/, uint32 /*updateTime*/) override
    {
        uint32 pointId = 0;
        float minDist = _owner.GetExactDist2dSq(&slaveFreePos[pointId]);
        for (uint32 i = 1; i < 4; ++i)
        {
            float dist = _owner.GetExactDist2dSq(&slaveFreePos[i]);
            if (dist < minDist)
            {
                minDist = dist;
                pointId = i;
            }
        }
        if (minDist < 200.0f * 200.0f)
            _owner.GetMotionMaster()->MovePoint(0, slaveFreePos[pointId], true, false);
        return true;
    }

private:
    Creature& _owner;
};

class spell_pos_slave_trigger_closest : public SpellScript
{
    PrepareSpellScript(spell_pos_slave_trigger_closest);

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            if (target->GetUInt32Value(UNIT_NPC_EMOTESTATE)) // prevent using multiple times
            {
                if (Unit* caster = GetCaster())
                    if (Player* p = caster->ToPlayer())
                    {
                        p->RewardPlayerAndGroupAtEvent(36764, caster); // alliance
                        p->RewardPlayerAndGroupAtEvent(36770, caster); // horde

                        target->SetUInt32Value(UNIT_NPC_EMOTESTATE, 0);
                        if (Creature* c = target->ToCreature())
                        {
                            c->DespawnOrUnsummon(7000);
                            c->AI()->Talk(0, p);
                            c->m_Events.AddEvent(new SlaveRunEvent(*c), c->m_Events.CalculateTime(3000));
                        }
                    }
            }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_pos_slave_trigger_closest::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_pos_rimefang_frost_nova : public SpellScript
{
    PrepareSpellScript(spell_pos_rimefang_frost_nova);

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            if (Unit* caster = GetCaster())
            {
                Unit::Kill(caster, target);
                if (target->GetTypeId() == TYPEID_UNIT)
                    target->ToCreature()->DespawnOrUnsummon(30000);
            }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_pos_rimefang_frost_nova::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_pos_blight_aura : public AuraScript
{
    PrepareAuraScript(spell_pos_blight_aura);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ 69604 });
    }

    void HandleEffectPeriodic(AuraEffect const* aurEff)
    {
        if (aurEff->GetTotalTicks() >= 0 && aurEff->GetTickNumber() == uint32(aurEff->GetTotalTicks()))
            if (Unit* target = GetTarget())
                target->CastSpell(target, 69604, true);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pos_blight_aura::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
    }
};

class spell_pos_glacial_strike_aura : public AuraScript
{
    PrepareAuraScript(spell_pos_glacial_strike_aura);

    void HandleEffectPeriodic(AuraEffect const* aurEff)
    {
        if (Unit* target = GetTarget())
            if (target->GetHealth() == target->GetMaxHealth())
            {
                PreventDefaultAction();
                aurEff->GetBase()->Remove(AURA_REMOVE_BY_EXPIRE);
                return;
            }
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_pos_glacial_strike_aura::HandleEffectPeriodic, EFFECT_2, SPELL_AURA_PERIODIC_DAMAGE_PERCENT);
    }
};

class at_tyrannus_event_starter : public AreaTriggerScript
{
public:
    at_tyrannus_event_starter() : AreaTriggerScript("at_tyrannus_event_starter") { }

    bool OnTrigger(Player* player, const AreaTrigger* /*at*/) override
    {
        InstanceScript* inst = player->GetInstanceScript();
        if (!inst)
            return false;

        if (inst->GetData(DATA_INSTANCE_PROGRESS) < INSTANCE_PROGRESS_AFTER_TUNNEL_WARN)
            return false;

        if (inst->GetData(DATA_GARFROST) == DONE && inst->GetData(DATA_ICK) == DONE && inst->GetData(DATA_TYRANNUS) != DONE && !inst->GetGuidData(DATA_MARTIN_OR_GORKUN_GUID))
        {
            if (Creature* c = inst->instance->SummonCreature(NPC_GORKUN_IRONSKULL_2, TSSpawnPos))
                c->GetMotionMaster()->MovePoint(0, TSMidPos);

            inst->SetData(DATA_INSTANCE_PROGRESS, INSTANCE_PROGRESS_TYRANNUS_INTRO);
        }

        return false;
    }
};

void AddSC_pit_of_saron()
{
    new npc_pos_leader();
    new npc_pos_deathwhisper_necrolyte();
    new npc_pos_after_first_boss();
    new npc_pos_tyrannus_events();
    new npc_pos_icicle_trigger();
    new npc_pos_collapsing_icicle();
    new npc_pos_martin_or_gorkun_second();
    new npc_pos_freed_slave();
    new npc_pos_leader_second();

    RegisterSpellScript(spell_pos_empowered_blizzard_aura);
    RegisterSpellScript(spell_pos_slave_trigger_closest);
    RegisterSpellScript(spell_pos_rimefang_frost_nova);
    RegisterSpellScript(spell_pos_blight_aura);
    RegisterSpellScript(spell_pos_glacial_strike_aura);

    new at_tyrannus_event_starter();
}
