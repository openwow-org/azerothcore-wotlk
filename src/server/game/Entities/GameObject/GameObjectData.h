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

#ifndef GAMEOBJECTDATA_H
#define GAMEOBJECTDATA_H

#include "SharedDefines.h"
#include "WDataStore.h"
#include <string>
#include <vector>

#define MAX_GAMEOBJECT_QUEST_ITEMS 6
#define MAX_GO_STATE               3

 // from `gameobject_template`
struct GameObjectTemplate
{
    uint32  entry;
    uint32  type;
    uint32  displayId;
    std::string name;
    std::string IconName;
    std::string castBarCaption;
    std::string unk1;
    float   size;
    union                                                   // different GO types have different data field
    {
        //0 GAMEOBJECT_TYPE_DOOR
        struct
        {
            uint32 startOpen;                               //0 used client side to determine GO_ACTIVATED means open/closed
            uint32 lockId;                                  //1 -> Lock.dbc
            uint32 autoCloseTime;                           //2 secs till autoclose = autoCloseTime / 0x10000
            uint32 noDamageImmune;                          //3 break opening whenever you recieve damage?
            uint32 openTextID;                              //4 can be used to replace castBarCaption?
            uint32 closeTextID;                             //5
            uint32 ignoredByPathing;                        //6
        } door;
        //1 GAMEOBJECT_TYPE_BUTTON
        struct
        {
            uint32 startOpen;                               //0
            uint32 lockId;                                  //1 -> Lock.dbc
            uint32 autoCloseTime;                           //2 secs till autoclose = autoCloseTime / 0x10000
            uint32 linkedTrap;                              //3
            uint32 noDamageImmune;                          //4 isBattlegroundObject
            uint32 large;                                   //5
            uint32 openTextID;                              //6 can be used to replace castBarCaption?
            uint32 closeTextID;                             //7
            uint32 losOK;                                   //8
        } button;
        //2 GAMEOBJECT_TYPE_QUESTGIVER
        struct
        {
            uint32 lockId;                                  //0 -> Lock.dbc
            uint32 questList;                               //1
            uint32 pageMaterial;                            //2
            uint32 gossipID;                                //3
            uint32 customAnim;                              //4
            uint32 noDamageImmune;                          //5
            uint32 openTextID;                              //6 can be used to replace castBarCaption?
            uint32 losOK;                                   //7
            uint32 allowMounted;                            //8
            uint32 large;                                   //9
        } questgiver;
        //3 GAMEOBJECT_TYPE_CHEST
        struct
        {
            uint32 lockId;                                  //0 -> Lock.dbc
            uint32 lootId;                                  //1
            uint32 chestRestockTime;                        //2
            uint32 consumable;                              //3
            uint32 minSuccessOpens;                         //4 Deprecated, pre 3.0 was used for mining nodes but since WotLK all mining nodes are usable once and grant all loot with a single use
            uint32 maxSuccessOpens;                         //5 Deprecated, pre 3.0 was used for mining nodes but since WotLK all mining nodes are usable once and grant all loot with a single use
            uint32 eventId;                                 //6 lootedEvent
            uint32 linkedTrapId;                            //7
            uint32 questId;                                 //8 not used currently but store quest required for GO activation for player
            uint32 level;                                   //9
            uint32 losOK;                                   //10
            uint32 leaveLoot;                               //11
            uint32 notInCombat;                             //12
            uint32 logLoot;                                 //13
            uint32 openTextID;                              //14 can be used to replace castBarCaption?
            uint32 groupLootRules;                          //15
            uint32 floatingTooltip;                         //16
        } chest;
        //4 GAMEOBJECT_TYPE_BINDER - empty
        //5 GAMEOBJECT_TYPE_GENERIC
        struct
        {
            uint32 floatingTooltip;                         //0
            uint32 highlight;                               //1
            uint32 serverOnly;                              //2
            uint32 large;                                   //3
            uint32 floatOnWater;                            //4
            int32 questID;                                  //5
        } _generic;
        //6 GAMEOBJECT_TYPE_TRAP
        struct
        {
            uint32 lockId;                                  //0 -> Lock.dbc
            uint32 level;                                   //1
            uint32 diameter;                                //2 diameter for trap activation
            uint32 spellId;                                 //3
            uint32 type;                                    //4 0 trap with no despawn after cast. 1 trap despawns after cast. 2 bomb casts on spawn.
            uint32 cooldown;                                //5 time in secs
            int32 autoCloseTime;                            //6
            uint32 startDelay;                              //7
            uint32 serverOnly;                              //8
            uint32 stealthed;                               //9
            uint32 large;                                   //10
            uint32 invisible;                               //11
            uint32 openTextID;                              //12 can be used to replace castBarCaption?
            uint32 closeTextID;                             //13
            uint32 ignoreTotems;                            //14
        } trap;
        //7 GAMEOBJECT_TYPE_CHAIR
        struct
        {
            uint32 slots;                                   //0
            uint32 height;                                  //1
            uint32 onlyCreatorUse;                          //2
            uint32 triggeredEvent;                          //3
        } chair;
        //8 GAMEOBJECT_TYPE_SPELL_FOCUS
        struct
        {
            uint32 focusId;                                 //0
            uint32 dist;                                    //1
            uint32 linkedTrapId;                            //2
            uint32 serverOnly;                              //3
            uint32 questID;                                 //4
            uint32 large;                                   //5
            uint32 floatingTooltip;                         //6
        } spellFocus;
        //9 GAMEOBJECT_TYPE_TEXT
        struct
        {
            uint32 pageID;                                  //0
            uint32 language;                                //1
            uint32 pageMaterial;                            //2
            uint32 allowMounted;                            //3
        } text;
        //10 GAMEOBJECT_TYPE_GOOBER
        struct
        {
            uint32 lockId;                                  //0 -> Lock.dbc
            int32 questId;                                  //1
            uint32 eventId;                                 //2
            uint32 autoCloseTime;                           //3
            uint32 customAnim;                              //4
            uint32 consumable;                              //5
            uint32 cooldown;                                //6
            uint32 pageId;                                  //7
            uint32 language;                                //8
            uint32 pageMaterial;                            //9
            uint32 spellId;                                 //10
            uint32 noDamageImmune;                          //11
            uint32 linkedTrapId;                            //12
            uint32 large;                                   //13
            uint32 openTextID;                              //14 can be used to replace castBarCaption?
            uint32 closeTextID;                             //15
            uint32 losOK;                                   //16 isBattlegroundObject
            uint32 allowMounted;                            //17
            uint32 floatingTooltip;                         //18
            uint32 gossipID;                                //19
            uint32 WorldStateSetsState;                     //20
        } goober;
        //11 GAMEOBJECT_TYPE_TRANSPORT
        struct
        {
            uint32 pauseAtTime;                             //0
            uint32 startOpen;                               //1
            uint32 autoCloseTime;                           //2 secs till autoclose = autoCloseTime / 0x10000
            uint32 pause1EventID;                           //3
            uint32 pause2EventID;                           //4
        } transport;
        //12 GAMEOBJECT_TYPE_AREADAMAGE
        struct
        {
            uint32 lockId;                                  //0
            uint32 radius;                                  //1
            uint32 damageMin;                               //2
            uint32 damageMax;                               //3
            uint32 damageSchool;                            //4
            uint32 autoCloseTime;                           //5 secs till autoclose = autoCloseTime / 0x10000
            uint32 openTextID;                              //6
            uint32 closeTextID;                             //7
        } areadamage;
        //13 GAMEOBJECT_TYPE_CAMERA
        struct
        {
            uint32 lockId;                                  //0 -> Lock.dbc
            uint32 cinematicId;                             //1
            uint32 eventID;                                 //2
            uint32 openTextID;                              //3 can be used to replace castBarCaption?
        } camera;
        //14 GAMEOBJECT_TYPE_MAPOBJECT - empty
        //15 GAMEOBJECT_TYPE_MO_TRANSPORT
        struct
        {
            uint32 taxiPathId;                              //0
            uint32 moveSpeed;                               //1
            uint32 accelRate;                               //2
            uint32 startEventID;                            //3
            uint32 stopEventID;                             //4
            uint32 transportPhysics;                        //5
            uint32 mapID;                                   //6
            uint32 worldState1;                             //7
            uint32 canBeStopped;                            //8
        } moTransport;
        //16 GAMEOBJECT_TYPE_DUELFLAG - empty
        //17 GAMEOBJECT_TYPE_FISHINGNODE - empty
        //18 GAMEOBJECT_TYPE_SUMMONING_RITUAL
        struct
        {
            uint32 reqParticipants;                         //0
            uint32 spellId;                                 //1
            uint32 animSpell;                               //2
            uint32 ritualPersistent;                        //3
            uint32 casterTargetSpell;                       //4
            uint32 casterTargetSpellTargets;                //5
            uint32 castersGrouped;                          //6
            uint32 ritualNoTargetCheck;                     //7
        } summoningRitual;
        //19 GAMEOBJECT_TYPE_MAILBOX - empty
        //20 GAMEOBJECT_TYPE_DONOTUSE - empty
        //21 GAMEOBJECT_TYPE_GUARDPOST
        struct
        {
            uint32 creatureID;                              //0
            uint32 charges;                                 //1
        } guardpost;
        //22 GAMEOBJECT_TYPE_SPELLCASTER
        struct
        {
            uint32 spellId;                                 //0
            uint32 charges;                                 //1
            uint32 partyOnly;                               //2
            uint32 allowMounted;                            //3
            uint32 large;                                   //4
        } spellcaster;
        //23 GAMEOBJECT_TYPE_MEETINGSTONE
        struct
        {
            uint32 minLevel;                                //0
            uint32 maxLevel;                                //1
            uint32 areaID;                                  //2
        } meetingstone;
        //24 GAMEOBJECT_TYPE_FLAGSTAND
        struct
        {
            uint32 lockId;                                  //0
            uint32 pickupSpell;                             //1
            uint32 radius;                                  //2
            uint32 returnAura;                              //3
            uint32 returnSpell;                             //4
            uint32 noDamageImmune;                          //5
            uint32 openTextID;                              //6
            uint32 losOK;                                   //7
        } flagstand;
        //25 GAMEOBJECT_TYPE_FISHINGHOLE
        struct
        {
            uint32 radius;                                  //0 how close bobber must land for sending loot
            uint32 lootId;                                  //1
            uint32 minSuccessOpens;                         //2
            uint32 maxSuccessOpens;                         //3
            uint32 lockId;                                  //4 -> Lock.dbc; possibly 1628 for all?
        } fishinghole;
        //26 GAMEOBJECT_TYPE_FLAGDROP
        struct
        {
            uint32 lockId;                                  //0
            uint32 eventID;                                 //1
            uint32 pickupSpell;                             //2
            uint32 noDamageImmune;                          //3
            uint32 openTextID;                              //4
        } flagdrop;
        //27 GAMEOBJECT_TYPE_MINI_GAME
        struct
        {
            uint32 gameType;                                //0
        } miniGame;
        //29 GAMEOBJECT_TYPE_CAPTURE_POINT
        struct
        {
            uint32 radius;                                  //0
            uint32 spell;                                   //1
            uint32 worldState1;                             //2
            uint32 worldstate2;                             //3
            uint32 winEventID1;                             //4
            uint32 winEventID2;                             //5
            uint32 contestedEventID1;                       //6
            uint32 contestedEventID2;                       //7
            uint32 progressEventID1;                        //8
            uint32 progressEventID2;                        //9
            uint32 neutralEventID1;                         //10
            uint32 neutralEventID2;                         //11
            uint32 neutralPercent;                          //12
            uint32 worldstate3;                             //13
            uint32 minSuperiority;                          //14
            uint32 maxSuperiority;                          //15
            uint32 minTime;                                 //16
            uint32 maxTime;                                 //17
            uint32 large;                                   //18
            uint32 highlight;                               //19
            uint32 startingValue;                           //20
            uint32 unidirectional;                          //21
        } capturePoint;
        //30 GAMEOBJECT_TYPE_AURA_GENERATOR
        struct
        {
            uint32 startOpen;                               //0
            uint32 radius;                                  //1
            uint32 auraID1;                                 //2
            uint32 conditionID1;                            //3
            uint32 auraID2;                                 //4
            uint32 conditionID2;                            //5
            uint32 serverOnly;                              //6
        } auraGenerator;
        //31 GAMEOBJECT_TYPE_DUNGEON_DIFFICULTY
        struct
        {
            uint32 mapID;                                   //0
            uint32 difficulty;                              //1
        } dungeonDifficulty;
        //32 GAMEOBJECT_TYPE_BARBER_CHAIR
        struct
        {
            uint32 chairheight;                             //0
            uint32 heightOffset;                            //1
        } barberChair;
        //33 GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING
        struct
        {
            uint32 intactNumHits;                           //0
            uint32 creditProxyCreature;                     //1
            uint32 state1Name;                              //2
            uint32 intactEvent;                             //3
            uint32 damagedDisplayId;                        //4
            uint32 damagedNumHits;                          //5
            uint32 empty3;                                  //6
            uint32 empty4;                                  //7
            uint32 empty5;                                  //8
            uint32 damagedEvent;                            //9
            uint32 destroyedDisplayId;                      //10
            uint32 empty7;                                  //11
            uint32 empty8;                                  //12
            uint32 empty9;                                  //13
            uint32 destroyedEvent;                          //14
            uint32 empty10;                                 //15
            uint32 debuildingTimeSecs;                      //16
            uint32 empty11;                                 //17
            uint32 destructibleData;                        //18
            uint32 rebuildingEvent;                         //19
            uint32 empty12;                                 //20
            uint32 empty13;                                 //21
            uint32 damageEvent;                             //22
            uint32 empty14;                                 //23
        } building;
        //34 GAMEOBJECT_TYPE_GUILDBANK - empty
        //35 GAMEOBJECT_TYPE_TRAPDOOR
        struct
        {
            uint32 whenToPause;                             // 0
            uint32 startOpen;                               // 1
            uint32 autoClose;                               // 2
        } trapDoor;

        // not use for specific field access (only for output with loop by all filed), also this determinate max union size
        struct
        {
            uint32 data[MAX_GAMEOBJECT_DATA];
        } raw;
    };

    std::string AIName;
    uint32 ScriptId;
    bool IsForQuests; // pussywizard

    // helpers
    [[nodiscard]] bool IsDespawnAtAction() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_CHEST:
            return chest.consumable;
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.consumable;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsUsableMounted() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_QUESTGIVER:
            return questgiver.allowMounted;
        case GAMEOBJECT_TYPE_TEXT:
            return text.allowMounted;
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.allowMounted;
        case GAMEOBJECT_TYPE_SPELLCASTER:
            return spellcaster.allowMounted;
        default:
            return false;
        }
    }

    [[nodiscard]] uint32 GetLockId() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_DOOR:
            return door.lockId;
        case GAMEOBJECT_TYPE_BUTTON:
            return button.lockId;
        case GAMEOBJECT_TYPE_QUESTGIVER:
            return questgiver.lockId;
        case GAMEOBJECT_TYPE_CHEST:
            return chest.lockId;
        case GAMEOBJECT_TYPE_TRAP:
            return trap.lockId;
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.lockId;
        case GAMEOBJECT_TYPE_AREADAMAGE:
            return areadamage.lockId;
        case GAMEOBJECT_TYPE_CAMERA:
            return camera.lockId;
        case GAMEOBJECT_TYPE_FLAGSTAND:
            return flagstand.lockId;
        case GAMEOBJECT_TYPE_FISHINGHOLE:
            return fishinghole.lockId;
        case GAMEOBJECT_TYPE_FLAGDROP:
            return flagdrop.lockId;
        default:
            return 0;
        }
    }

    [[nodiscard]] bool GetDespawnPossibility() const                      // despawn at targeting of cast?
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_DOOR:
            return door.noDamageImmune;
        case GAMEOBJECT_TYPE_BUTTON:
            return button.noDamageImmune;
        case GAMEOBJECT_TYPE_QUESTGIVER:
            return questgiver.noDamageImmune;
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.noDamageImmune;
        case GAMEOBJECT_TYPE_FLAGSTAND:
            return flagstand.noDamageImmune;
        case GAMEOBJECT_TYPE_FLAGDROP:
            return flagdrop.noDamageImmune;
        default:
            return true;
        }
    }

    [[nodiscard]] uint32 GetCharges() const                               // despawn at uses amount
    {
        switch (type)
        {
            //case GAMEOBJECT_TYPE_TRAP:        return trap.charges;
        case GAMEOBJECT_TYPE_GUARDPOST:
            return guardpost.charges;
        case GAMEOBJECT_TYPE_SPELLCASTER:
            return spellcaster.charges;
        default:
            return 0;
        }
    }

    [[nodiscard]] uint32 GetLinkedGameObjectEntry() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_BUTTON:
            return button.linkedTrap;
        case GAMEOBJECT_TYPE_CHEST:
            return chest.linkedTrapId;
        case GAMEOBJECT_TYPE_SPELL_FOCUS:
            return spellFocus.linkedTrapId;
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.linkedTrapId;
        default:
            return 0;
        }
    }

    [[nodiscard]] uint32 GetAutoCloseTime() const
    {
        uint32 autoCloseTime = 0;
        switch (type)
        {
        case GAMEOBJECT_TYPE_DOOR:
            autoCloseTime = door.autoCloseTime;
            break;
        case GAMEOBJECT_TYPE_BUTTON:
            autoCloseTime = button.autoCloseTime;
            break;
        case GAMEOBJECT_TYPE_TRAP:
            autoCloseTime = trap.autoCloseTime;
            break;
        case GAMEOBJECT_TYPE_GOOBER:
            autoCloseTime = goober.autoCloseTime;
            break;
        case GAMEOBJECT_TYPE_TRANSPORT:
            autoCloseTime = transport.autoCloseTime;
            break;
        case GAMEOBJECT_TYPE_AREADAMAGE:
            autoCloseTime = areadamage.autoCloseTime;
            break;
        default:
            break;
        }
        return autoCloseTime /* xinef: changed to milliseconds/ IN_MILLISECONDS*/;              // prior to 3.0.3, conversion was / 0x10000;
    }

    [[nodiscard]] uint32 GetLootId() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_CHEST:
            return chest.lootId;
        case GAMEOBJECT_TYPE_FISHINGHOLE:
            return fishinghole.lootId;
        default:
            return 0;
        }
    }

    [[nodiscard]] uint32 GetGossipMenuId() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_QUESTGIVER:
            return questgiver.gossipID;
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.gossipID;
        default:
            return 0;
        }
    }

    [[nodiscard]] uint32 GetEventScriptId() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.eventId;
        case GAMEOBJECT_TYPE_CHEST:
            return chest.eventId;
        case GAMEOBJECT_TYPE_CAMERA:
            return camera.eventID;
        default:
            return 0;
        }
    }

    [[nodiscard]] uint32 GetCooldown() const                              // Cooldown preventing goober and traps to cast spell
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_TRAP:
            return trap.cooldown;
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.cooldown;
        default:
            return 0;
        }
    }

    [[nodiscard]] bool IsLargeGameObject() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_BUTTON:
            return button.large != 0;
        case GAMEOBJECT_TYPE_QUESTGIVER:
            return questgiver.large != 0;
        case GAMEOBJECT_TYPE_GENERIC:
            return _generic.large != 0;
        case GAMEOBJECT_TYPE_TRAP:
            return trap.large != 0;
        case GAMEOBJECT_TYPE_SPELL_FOCUS:
            return spellFocus.large != 0;
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.large != 0;
        case GAMEOBJECT_TYPE_SPELLCASTER:
            return spellcaster.large != 0;
        case GAMEOBJECT_TYPE_CAPTURE_POINT:
            return capturePoint.large != 0;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsInfiniteGameObject() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_DOOR:
            return true;
        case GAMEOBJECT_TYPE_FLAGSTAND:
            return true;
        case GAMEOBJECT_TYPE_FLAGDROP:
            return true;
        case GAMEOBJECT_TYPE_DUNGEON_DIFFICULTY:
            return true;
        case GAMEOBJECT_TYPE_TRAPDOOR:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsGameObjectForQuests() const
    {
        return IsForQuests;
    }

    [[nodiscard]] bool IsIgnoringLOSChecks() const
    {
        switch (type)
        {
        case GAMEOBJECT_TYPE_BUTTON:
            return button.losOK == 0;
        case GAMEOBJECT_TYPE_QUESTGIVER:
            return questgiver.losOK == 0;
        case GAMEOBJECT_TYPE_CHEST:
            return chest.losOK == 0;
        case GAMEOBJECT_TYPE_GOOBER:
            return goober.losOK == 0;
        case GAMEOBJECT_TYPE_FLAGSTAND:
            return flagstand.losOK == 0;
        default:
            return false;
        }
    }
};

// From `gameobject_template_addon`
struct GameObjectTemplateAddon
{
    uint32  entry;
    uint32  faction;
    uint32  flags;
    uint32  mingold;
    uint32  maxgold;
    std::array<uint32, 4> artKits = {};
};

struct GameObjectLocale
{
    std::vector<std::string> Name;
    std::vector<std::string> CastBarCaption;
};

// `gameobject_addon` table
struct GameObjectAddon
{
    InvisibilityType invisibilityType;
    uint32 InvisibilityValue;
};

// client side GO show states
enum GOState
{
    GO_STATE_ACTIVE = 0,                        // show in world as used and not reset (closed door open)
    GO_STATE_READY = 1,                        // show in world as ready (closed door close)
    GO_STATE_ACTIVE_ALTERNATIVE = 2                         // show in world as used in alt way and not reset (closed door open by cannon fire)
};

// from `gameobject`
struct GameObjectData
{
    explicit GameObjectData() = default;
    uint32 id{ 0 };                                              // entry in gamobject_template
    uint16 mapid{ 0 };
    uint32 phaseMask{ 0 };
    float posX{ 0.0f };
    float posY{ 0.0f };
    float posZ{ 0.0f };
    float orientation{ 0.0f };
    G3D::Quat rotation;
    int32  spawntimesecs{ 0 };
    uint32 ScriptId;
    uint32 animprogress{ 0 };
    GOState go_state{ GO_STATE_ACTIVE };
    uint8 spawnMask{ 0 };
    uint8 artKit{ 0 };
    bool dbData{ true };
};

#endif // GameObjectData_h__
