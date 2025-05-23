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

#include "Common.h"
#include "CreatureGroups.h"
#include "CreatureScript.h"
#include "GameEventMgr.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"

enum COG_Paths
{
    STORMWIND_PATH = 80500,
    GOLDSHIRE_PATH = 80501,
    WOODS_PATH = 80502,
    HOUSE_PATH = 80503,
    LISA_PATH = 80700
};

enum COG_Sounds
{
    BANSHEE_DEATH = 1171,
    BANSHEEPREAGGRO = 1172,
    CTHUN_YOU_WILL_DIE = 8585,
    CTHUN_DEATH_IS_CLOSE = 8580,
    HUMAN_FEMALE_EMOTE_CRY = 6916,
    GHOSTDEATH = 3416
};

enum COG_Creatures
{
    NPC_DANA = 804,
    NPC_CAMERON = 805,
    NPC_JOHN = 806,
    NPC_LISA = 807,
    NPC_AARON = 810,
    NPC_JOSE = 811
};

enum COG_Events
{
    EVENT_WP_START_GOLDSHIRE = 1,
    EVENT_WP_START_WOODS = 2,
    EVENT_WP_START_HOUSE = 3,
    EVENT_WP_START_LISA = 4,
    EVENT_PLAY_SOUNDS = 5,
    EVENT_BEGIN_EVENT = 6,
    EVENT_RANDOM_MOVEMENT = 7
};

enum COG_GameEvent
{
    GAME_EVENT_CHILDREN_OF_GOLDSHIRE = 74
};

struct npc_cameron : public ScriptedAI
{
    npc_cameron(Creature* creature) : ScriptedAI(creature)
    {
        _started = false;
    }

    static uint32 SoundPicker()
    {
        return RAND(
            BANSHEE_DEATH,
            BANSHEEPREAGGRO,
            CTHUN_YOU_WILL_DIE,
            CTHUN_DEATH_IS_CLOSE,
            HUMAN_FEMALE_EMOTE_CRY,
            GHOSTDEATH
        );
    }

    void MoveTheChildren()
    {
        std::vector<Position> MovePosPositions =
        {
            { -9373.521f, -67.71767f, 69.201965f, 1.117011f },
            { -9374.94f, -62.51654f, 69.201965f, 5.201081f },
            { -9371.013f, -71.20811f, 69.201965f, 1.937315f },
            { -9368.419f, -66.47543f, 69.201965f, 3.141593f },
            { -9372.376f, -65.49946f, 69.201965f, 4.206244f },
            { -9377.477f, -67.8297f, 69.201965f, 0.296706f }
        };

        Acore::Containers::RandomShuffle(MovePosPositions);

        // first we break formation because children will need to move on their own now
        for (auto& guid : _childrenGUIDs)
            if (Creature* child = ObjectAccessor::GetCreature(*me, guid))
                if (child->GetFormation())
                    child->GetFormation()->RemoveMember(child);

        // Move each child to an random position
        for (uint32 i = 0; i < _childrenGUIDs.size(); ++i)
        {
            if (Creature* children = ObjectAccessor::GetCreature(*me, _childrenGUIDs[i]))
            {
                children->SetWalk(true);
                children->GetMotionMaster()->MovePoint(0, MovePosPositions[i], true, MovePosPositions[i].GetOrientation());
            }
        }
        me->SetWalk(true);
        me->GetMotionMaster()->MovePoint(0, MovePosPositions.back(), true, MovePosPositions.back().GetOrientation());
    }

    void PathEndReached(uint32 pathId) override
    {
        switch (pathId)
        {
            case STORMWIND_PATH:
            {
                _events.ScheduleEvent(EVENT_RANDOM_MOVEMENT, 2s);
                _events.ScheduleEvent(EVENT_WP_START_GOLDSHIRE, 11min);
                break;
            }
            case GOLDSHIRE_PATH:
            {
                _events.ScheduleEvent(EVENT_RANDOM_MOVEMENT, 2s);
                _events.ScheduleEvent(EVENT_WP_START_WOODS, 15min);
                break;
            }
            case WOODS_PATH:
            {
                _events.ScheduleEvent(EVENT_RANDOM_MOVEMENT, 2s);
                _events.ScheduleEvent(EVENT_WP_START_HOUSE, 6min); // 6 minutes
                _events.ScheduleEvent(EVENT_WP_START_LISA, 362s);
                break;
            }
            case HOUSE_PATH:
            {
                // Move childeren at last point
                MoveTheChildren();
                // After 30 seconds a random sound should play
                _events.ScheduleEvent(EVENT_PLAY_SOUNDS, 30s);
                break;
            }
        }
    }

    void sOnGameEvent(bool start, uint16 eventId) override
    {
        if (start && eventId == GAME_EVENT_CHILDREN_OF_GOLDSHIRE)
        {
            // Start event at 7 am
            // Begin pathing
            _events.ScheduleEvent(EVENT_BEGIN_EVENT, 2s);
            _started = true;
        }
        else if (!start && eventId == GAME_EVENT_CHILDREN_OF_GOLDSHIRE)
        {
            // Reset event at 8 am
            _started = false;
            _events.Reset();
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_started)
            return;

        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
            case EVENT_WP_START_GOLDSHIRE:
                me->GetMotionMaster()->MovePath(GOLDSHIRE_PATH, false);
                break;
            case EVENT_WP_START_WOODS:
                me->GetMotionMaster()->MovePath(WOODS_PATH, false);
                break;
            case EVENT_WP_START_HOUSE:
                me->GetMotionMaster()->MovePath(HOUSE_PATH, false);
                break;
            case EVENT_WP_START_LISA:
                for (uint32 i = 0; i < _childrenGUIDs.size(); ++i)
                {
                    if (Creature* lisa = ObjectAccessor::GetCreature(*me, _childrenGUIDs[i]))
                    {
                        if (lisa->GetEntry() == NPC_LISA)
                        {
                            lisa->GetMotionMaster()->MovePath(LISA_PATH, false);
                            break;
                        }
                    }
                }
                break;
            case EVENT_PLAY_SOUNDS:
                me->PlayDistanceSound(SoundPicker());
                break;
            case EVENT_BEGIN_EVENT:
            {
                _childrenGUIDs.clear();

                // Get all childeren's guid's.
                if (Creature* dana = me->FindNearestCreature(NPC_DANA, 25.0f))
                    _childrenGUIDs.push_back(dana->GetGUID());

                if (Creature* john = me->FindNearestCreature(NPC_JOHN, 25.0f))
                    _childrenGUIDs.push_back(john->GetGUID());

                if (Creature* lisa = me->FindNearestCreature(NPC_LISA, 25.0f))
                    _childrenGUIDs.push_back(lisa->GetGUID());

                if (Creature* aaron = me->FindNearestCreature(NPC_AARON, 25.0f))
                    _childrenGUIDs.push_back(aaron->GetGUID());

                if (Creature* jose = me->FindNearestCreature(NPC_JOSE, 25.0f))
                    _childrenGUIDs.push_back(jose->GetGUID());

                // If Formation was disbanded, remake.
                if (!me->GetFormation()->IsFormed())
                    for (auto& guid : _childrenGUIDs)
                        if (Creature* child = ObjectAccessor::GetCreature(*me, guid))
                            child->SearchFormation();

                // Start movement
                me->GetMotionMaster()->MovePath(STORMWIND_PATH, false);

                break;
            }
            case EVENT_RANDOM_MOVEMENT:
            {
                me->GetMotionMaster()->MoveRandom(10.0f);
                break;
            }
            default:
                break;
            }
        }
    }

private:
    EventMap _events;
    bool _started;
    GuidVector _childrenGUIDs;
};

/*######
## npc_supervisor_raelen
######*/

enum SupervisorRaelen
{
    EVENT_FIND_PEASENTS  = 8,
    EVENT_NEXT_PEASENT   = 9,
    NPC_EASTVALE_PEASENT = 11328
};

struct npc_supervisor_raelen : public ScriptedAI
{
    npc_supervisor_raelen(Creature* creature) : ScriptedAI(creature) {}

    void Reset() override
    {
        _PeasentId = 0;
        peasentGUIDs.clear();
        _events.ScheduleEvent(EVENT_FIND_PEASENTS, 4s);
    }

    void SetData(uint32 /*type*/, uint32 data) override
    {
        if (data == 1)
        {
            ++_PeasentId;
            if (_PeasentId == 5) _PeasentId = 0;
            _events.ScheduleEvent(EVENT_NEXT_PEASENT, 2s, 6s);
        }
    }

    void CallPeasent()
    {
        if (Creature* peasent = ObjectAccessor::GetCreature(*me, peasentGUIDs[_PeasentId]))
            peasent->AI()->SetData(1, 1);
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        if (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FIND_PEASENTS:
                {
                    GuidVector tempGUIDs;
                    std::list<Creature*> peasents;
                    GetCreatureListWithEntryInGrid(peasents, me, NPC_EASTVALE_PEASENT, 100.f);
                    for (Creature* peasent : peasents)
                    {
                        tempGUIDs.push_back(peasent->GetGUID());
                    }
                    peasentGUIDs.push_back(tempGUIDs[2]);
                    peasentGUIDs.push_back(tempGUIDs[3]);
                    peasentGUIDs.push_back(tempGUIDs[0]);
                    peasentGUIDs.push_back(tempGUIDs[1]);
                    peasentGUIDs.push_back(tempGUIDs[4]);
                    _events.ScheduleEvent(EVENT_NEXT_PEASENT, 1s);
                    break;
                }
                case EVENT_NEXT_PEASENT:
                    CallPeasent();
                    break;
            }
        }

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }
private:
    EventMap _events;
    uint8 _PeasentId;
    GuidVector peasentGUIDs;
};

/*######
## npc_eastvale_peasent
######*/

enum EastvalePeasent
{
    EVENT_MOVETORAELEN                = 10,
    EVENT_TALKTORAELEN1               = 11,
    EVENT_TALKTORAELEN2               = 12,
    EVENT_RAELENTALK                  = 13,
    EVENT_TALKTORAELEN3               = 14,
    EVENT_TALKTORAELEN4               = 15,
    EVENT_PATHBACK                    = 16,
    NPC_SUPERVISOR_RAELEN             = 10616,
    PATH_PEASENT_0                    = 813490,
    PATH_PEASENT_1                    = 813480,
    PATH_PEASENT_2                    = 812520,
    PATH_PEASENT_3                    = 812490,
    PATH_PEASENT_4                    = 812500,
    SAY_RAELEN                        = 0,
    SOUND_PEASENT_GREETING_1          = 6289,
    SOUND_PEASENT_GREETING_2          = 6288,
    SOUND_PEASENT_GREETING_3          = 6290,
    SOUND_PEASENT_LEAVING_1           = 6242,
    SOUND_PEASENT_LEAVING_2           = 6282,
    SOUND_PEASENT_LEAVING_3           = 6284,
    SOUND_PEASENT_LEAVING_4           = 6285,
    SOUND_PEASENT_LEAVING_5           = 6286,
    SPELL_TRANSFORM_PEASENT_WITH_WOOD = 9127
};

struct npc_eastvale_peasent : public ScriptedAI
{
    npc_eastvale_peasent(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _path = me->GetSpawnId() * 10;
    }

    void Reset() override {}

    void SetData(uint32 /*type*/, uint32 data) override
    {
        if (data == 1)
        {
            me->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
            me->CastSpell(me, SPELL_TRANSFORM_PEASENT_WITH_WOOD);
            me->SetSpeed(MOVE_WALK, 1.0f);
            me->GetMotionMaster()->MovePath(_path, false);
        }
    }

    void PathEndReached(uint32 pathId) override
    {
        if (pathId == _path)
        {
            CreatureTemplate const* cinfo = sObjectMgr->GetCreatureTemplate(me->GetEntry());
            me->SetSpeed(MOVE_WALK, cinfo->speed_walk);
            me->RemoveAura(SPELL_TRANSFORM_PEASENT_WITH_WOOD);
            _events.ScheduleEvent(EVENT_MOVETORAELEN, 3s);
        }
        else if (pathId == _path + 1)
        {
            _events.ScheduleEvent(EVENT_TALKTORAELEN1, 1s);
        }
        else if (pathId == _path + 2)
        {
            me->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_WORK_CHOPWOOD);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        if (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
            case EVENT_MOVETORAELEN:
                me->GetMotionMaster()->MovePath(_path + 1, false);
                break;
            case EVENT_TALKTORAELEN1:
                if (Creature* realen = me->FindNearestCreature(NPC_SUPERVISOR_RAELEN, 2.0f, true))
                {
                    _realenGUID = realen->GetGUID();
                    me->SetFacingToObject(realen);

                    switch (_path)
                    {
                    case PATH_PEASENT_0:
                        me->PlayDirectSound(SOUND_PEASENT_GREETING_1);
                        _events.ScheduleEvent(EVENT_TALKTORAELEN2, 2s);
                        break;
                    case PATH_PEASENT_1:
                    case PATH_PEASENT_3:
                        me->PlayDirectSound(SOUND_PEASENT_GREETING_3);
                        _events.ScheduleEvent(EVENT_RAELENTALK, 2s);
                        break;
                    case PATH_PEASENT_2:
                    case PATH_PEASENT_4:
                        me->PlayDirectSound(SOUND_PEASENT_GREETING_2);
                        _events.ScheduleEvent(EVENT_RAELENTALK, 2s);
                        break;
                    }
                }
                else
                {
                    // Path back if realen cannot be found alive
                    _events.ScheduleEvent(EVENT_PATHBACK, 2s);
                }
                break;
            case EVENT_TALKTORAELEN2:
                me->PlayDirectSound(SOUND_PEASENT_GREETING_2);
                _events.ScheduleEvent(EVENT_RAELENTALK, 2s);
                break;
            case EVENT_RAELENTALK:
                if (Creature* realen = ObjectAccessor::GetCreature(*me, _realenGUID))
                {
                    realen->AI()->Talk(SAY_RAELEN);
                    _events.ScheduleEvent(EVENT_TALKTORAELEN3, 5s);
                }
                break;
            case EVENT_TALKTORAELEN3:
                {
                    switch (_path)
                    {
                    case PATH_PEASENT_0:
                        me->PlayDirectSound(SOUND_PEASENT_LEAVING_1);
                        _events.ScheduleEvent(EVENT_PATHBACK, 2s);
                        break;
                    case PATH_PEASENT_1:
                    case PATH_PEASENT_3:
                        me->PlayDirectSound(SOUND_PEASENT_LEAVING_4);
                        _events.ScheduleEvent(EVENT_TALKTORAELEN4, 2s);
                        break;
                    case PATH_PEASENT_2:
                        me->PlayDirectSound(SOUND_PEASENT_LEAVING_3);
                        _events.ScheduleEvent(EVENT_PATHBACK, 2s);
                        break;
                    case PATH_PEASENT_4:
                        me->PlayDirectSound(SOUND_PEASENT_LEAVING_2);
                        _events.ScheduleEvent(EVENT_PATHBACK, 2s);
                        break;
                    }
                }
                break;
            case EVENT_TALKTORAELEN4:
                me->PlayDirectSound(SOUND_PEASENT_LEAVING_5);
                _events.ScheduleEvent(EVENT_PATHBACK, 2s);
                break;
            case EVENT_PATHBACK:
                if (Creature* realen = ObjectAccessor::GetCreature(*me, _realenGUID))
                    realen->AI()->SetData(1, 1);
                me->GetMotionMaster()->MovePath(_path + 2, false);
                break;
            }
        }

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    WOWGUID _realenGUID;
    uint32 _path;
};

void AddSC_elwynn_forest()
{
    RegisterCreatureAI(npc_cameron);
    RegisterCreatureAI(npc_supervisor_raelen);
    RegisterCreatureAI(npc_eastvale_peasent);
}
