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

#ifndef __UNIT_H
#define __UNIT_H

#include "EnumFlag.h"
#include "EventProcessor.h"
#include "FollowerRefMgr.h"
#include "FollowerReference.h"
#include "HostileRefMgr.h"
#include "ItemTemplate.h"
#include "MotionMaster.h"
#include "Object.h"
#include "SpellAuraDefines.h"
#include "SpellDefines.h"
#include "ThreatMgr.h"
#include "UnitDefines.h"
#include "UnitUtils.h"
#include <functional>
#include <utility>

#define WORLD_TRIGGER   12999

#define BASE_MINDAMAGE 1.0f
#define BASE_MAXDAMAGE 2.0f
#define BASE_ATTACK_TIME 2000

#define MAX_AGGRO_RADIUS 45.0f  // yards
static constexpr uint32 MAX_CREATURE_SPELLS = 8;
static constexpr uint32 infinityCooldownDelay = 0x9A7EC800; // used for set "infinity cooldowns" for spells and check, MONTH*IN_MILLISECONDS
static constexpr uint32 infinityCooldownDelayCheck = 0x4D3F6400; // MONTH*IN_MILLISECONDS/2;

struct CharmInfo;
struct FactionTemplateEntry;
struct SpellValue;

class AuraApplication;
class Aura;
class UnitAura;
class AuraEffect;
class Creature;
class Spell;
class SpellInfo;
class DynamicObject;
class GameObject;
class Item;
class Pet;
class PetAura;
class Minion;
class Guardian;
class UnitAI;
class Totem;
class Transport;
class StaticTransport;
class MotionTransport;
class Vehicle;
class TransportBase;
class SpellCastTargets;

typedef std::list<Unit*> UnitList;
typedef std::list< std::pair<Aura*, uint8> > DispelChargesList;

enum CharmType : uint8;

enum VictimState
{
    VICTIMSTATE_INTACT         = 0, // set when attacker misses
    VICTIMSTATE_HIT            = 1, // victim got clear/blocked hit
    VICTIMSTATE_DODGE          = 2,
    VICTIMSTATE_PARRY          = 3,
    VICTIMSTATE_INTERRUPT      = 4,
    VICTIMSTATE_BLOCKS         = 5, // unused? not set when blocked, even on full block
    VICTIMSTATE_EVADES         = 6,
    VICTIMSTATE_IS_IMMUNE      = 7,
    VICTIMSTATE_DEFLECTS       = 8
};

enum HitInfo
{
    HITINFO_NORMALSWING         = 0x00000000,
    HITINFO_UNK1                = 0x00000001,               // req correct packet structure
    HITINFO_AFFECTS_VICTIM      = 0x00000002,
    HITINFO_OFFHAND             = 0x00000004,
    HITINFO_UNK2                = 0x00000008,
    HITINFO_MISS                = 0x00000010,
    HITINFO_FULL_ABSORB         = 0x00000020,
    HITINFO_PARTIAL_ABSORB      = 0x00000040,
    HITINFO_FULL_RESIST         = 0x00000080,
    HITINFO_PARTIAL_RESIST      = 0x00000100,
    HITINFO_CRITICALHIT         = 0x00000200,               // critical hit
    HITINFO_UNK10               = 0x00000400,
    HITINFO_UNK11               = 0x00000800,
    HITINFO_UNK12               = 0x00001000,
    HITINFO_BLOCK               = 0x00002000,               // blocked damage
    HITINFO_UNK14               = 0x00004000,               // set only if meleespellid is present//  no world text when victim is hit for 0 dmg(HideWorldTextForNoDamage?)
    HITINFO_UNK15               = 0x00008000,               // player victim?// something related to blod sprut visual (BloodSpurtInBack?)
    HITINFO_GLANCING            = 0x00010000,
    HITINFO_CRUSHING            = 0x00020000,
    HITINFO_NO_ANIMATION        = 0x00040000,
    HITINFO_UNK19               = 0x00080000,
    HITINFO_UNK20               = 0x00100000,
    HITINFO_SWINGNOHITSOUND     = 0x00200000,               // unused?
    HITINFO_UNK22               = 0x00400000,
    HITINFO_RAGE_GAIN           = 0x00800000,
    HITINFO_FAKE_DAMAGE         = 0x01000000                // enables damage animation even if no damage done, set only if no damage
};

enum UnitModifierType
{
    BASE_VALUE = 0,
    BASE_PCT = 1,
    TOTAL_VALUE = 2,
    TOTAL_PCT = 3,
    MODIFIER_TYPE_END = 4
};

enum WeaponDamageRange
{
    MINDAMAGE,
    MAXDAMAGE,

    MAX_WEAPON_DAMAGE_RANGE
};

enum UnitMods
{
    UNIT_MOD_STAT_STRENGTH,                                 // UNIT_MOD_STAT_STRENGTH..UNIT_MOD_STAT_SPIRIT must be in existed order, it's accessed by index values of Stats enum.
    UNIT_MOD_STAT_AGILITY,
    UNIT_MOD_STAT_STAMINA,
    UNIT_MOD_STAT_INTELLECT,
    UNIT_MOD_STAT_SPIRIT,
    UNIT_MOD_HEALTH,
    UNIT_MOD_MANA,                                          // UNIT_MOD_MANA..UNIT_MOD_RUNIC_POWER must be in existed order, it's accessed by index values of POWER_TYPE enum.
    UNIT_MOD_RAGE,
    UNIT_MOD_FOCUS,
    UNIT_MOD_ENERGY,
    UNIT_MOD_HAPPINESS,
    UNIT_MOD_RUNE,
    UNIT_MOD_RUNIC_POWER,
    UNIT_MOD_ARMOR,                                         // UNIT_MOD_ARMOR..UNIT_MOD_RESISTANCE_ARCANE must be in existed order, it's accessed by index values of SpellSchools enum.
    UNIT_MOD_RESISTANCE_HOLY,
    UNIT_MOD_RESISTANCE_FIRE,
    UNIT_MOD_RESISTANCE_NATURE,
    UNIT_MOD_RESISTANCE_FROST,
    UNIT_MOD_RESISTANCE_SHADOW,
    UNIT_MOD_RESISTANCE_ARCANE,
    UNIT_MOD_ATTACK_POWER,
    UNIT_MOD_ATTACK_POWER_RANGED,
    UNIT_MOD_DAMAGE_MAINHAND,
    UNIT_MOD_DAMAGE_OFFHAND,
    UNIT_MOD_DAMAGE_RANGED,
    UNIT_MOD_END,
    // synonyms
    UNIT_MOD_STAT_START = UNIT_MOD_STAT_STRENGTH,
    UNIT_MOD_STAT_END = UNIT_MOD_STAT_SPIRIT + 1,
    UNIT_MOD_RESISTANCE_START = UNIT_MOD_ARMOR,
    UNIT_MOD_RESISTANCE_END = UNIT_MOD_RESISTANCE_ARCANE + 1,
    UNIT_MOD_POWER_START = UNIT_MOD_MANA,
    UNIT_MOD_POWER_END = UNIT_MOD_RUNIC_POWER + 1
};

enum BaseModGroup
{
    CRIT_PERCENTAGE,
    RANGED_CRIT_PERCENTAGE,
    OFFHAND_CRIT_PERCENTAGE,
    SHIELD_BLOCK_VALUE,
    BASEMOD_END
};

enum BaseModType
{
    FLAT_MOD,
    PCT_MOD
};

#define MOD_END (PCT_MOD+1)

enum class DeathState : uint8
{
    Alive         = 0,
    JustDied      = 1,
    Corpse        = 2,
    Dead          = 3,
    JustRespawned = 4,
};
extern float baseMoveSpeed[MAX_MOVE_TYPE];
extern float playerBaseMoveSpeed[MAX_MOVE_TYPE];

enum WeaponAttackType : uint8
{
    BASE_ATTACK   = 0,
    OFF_ATTACK    = 1,
    RANGED_ATTACK = 2,
    MAX_ATTACK
};

enum CombatRating
{
    CR_WEAPON_SKILL             = 0,
    CR_DEFENSE_SKILL            = 1,
    CR_DODGE                    = 2,
    CR_PARRY                    = 3,
    CR_BLOCK                    = 4,
    CR_HIT_MELEE                = 5,
    CR_HIT_RANGED               = 6,
    CR_HIT_SPELL                = 7,
    CR_CRIT_MELEE               = 8,
    CR_CRIT_RANGED              = 9,
    CR_CRIT_SPELL               = 10,
    CR_HIT_TAKEN_MELEE          = 11,
    CR_HIT_TAKEN_RANGED         = 12,
    CR_HIT_TAKEN_SPELL          = 13,
    CR_CRIT_TAKEN_MELEE         = 14,
    CR_CRIT_TAKEN_RANGED        = 15,
    CR_CRIT_TAKEN_SPELL         = 16,
    CR_HASTE_MELEE              = 17,
    CR_HASTE_RANGED             = 18,
    CR_HASTE_SPELL              = 19,
    CR_WEAPON_SKILL_MAINHAND    = 20,
    CR_WEAPON_SKILL_OFFHAND     = 21,
    CR_WEAPON_SKILL_RANGED      = 22,
    CR_EXPERTISE                = 23,
    CR_ARMOR_PENETRATION        = 24
};

#define MAX_COMBAT_RATING         25

enum DamageEffectType : uint8
{
    DIRECT_DAMAGE           = 0,                            // used for normal weapon damage (not for class abilities or spells)
    SPELL_DIRECT_DAMAGE     = 1,                            // spell/class abilities damage
    DOT                     = 2,
    HEAL                    = 3,
    NODAMAGE                = 4,                            // used also in case when damage applied to health but not applied to spell channelInterruptFlags/etc
    SELF_DAMAGE             = 5
};
namespace Movement
{
    class MoveSpline;
}

enum DiminishingLevels
{
    DIMINISHING_LEVEL_1             = 0,
    DIMINISHING_LEVEL_2             = 1,
    DIMINISHING_LEVEL_3             = 2,
    DIMINISHING_LEVEL_IMMUNE        = 3,
    DIMINISHING_LEVEL_4             = 3,
    DIMINISHING_LEVEL_TAUNT_IMMUNE  = 4,
};

struct DiminishingReturn
{
    DiminishingReturn(DiminishingGroup group, uint32 t, uint32 count)
        : DRGroup(group), stack(0), hitTime(t), hitCount(count)
    {}

    DiminishingGroup        DRGroup: 16;
    uint16                  stack: 16;
    uint32                  hitTime;
    uint32                  hitCount;
};

enum MeleeHitOutcome
{
    MELEE_HIT_EVADE, MELEE_HIT_MISS, MELEE_HIT_DODGE, MELEE_HIT_BLOCK, MELEE_HIT_PARRY,
    MELEE_HIT_GLANCING, MELEE_HIT_CRIT, MELEE_HIT_CRUSHING, MELEE_HIT_NORMAL
};

enum ExtraAttackSpells
{
    SPELL_SWORD_SPECIALIZATION   = 16459,
    SPELL_HACK_AND_SLASH         = 66923
};

class DispelInfo
{
public:
    explicit DispelInfo(Unit* dispeller, uint32 dispellerSpellId, uint8 chargesRemoved) :
        _dispellerUnit(dispeller), _dispellerSpell(dispellerSpellId), _chargesRemoved(chargesRemoved) {}

    Unit* GetDispeller() const { return _dispellerUnit; }
    uint32 GetDispellerSpellId() const { return _dispellerSpell; }
    uint8 GetRemovedCharges() const { return _chargesRemoved; }
    void SetRemovedCharges(uint8 amount)
    {
        _chargesRemoved = amount;
    }
private:
    Unit* _dispellerUnit;
    uint32 _dispellerSpell;
    uint8 _chargesRemoved;
};

struct CleanDamage
{
    CleanDamage(uint32 mitigated, uint32 absorbed, WeaponAttackType _attackType, MeleeHitOutcome _hitOutCome) :
        absorbed_damage(absorbed), mitigated_damage(mitigated), attackType(_attackType), hitOutCome(_hitOutCome) {}

    uint32 absorbed_damage;
    uint32 mitigated_damage;

    WeaponAttackType attackType;
    MeleeHitOutcome hitOutCome;
};

struct CalcDamageInfo;
struct SpellNonMeleeDamage;

class DamageInfo
{
private:
    Unit* const m_attacker;
    Unit* const m_victim;
    uint32 m_damage;
    SpellInfo const* const m_spellInfo;
    SpellSchoolMask const m_schoolMask;
    DamageEffectType const m_damageType;
    WeaponAttackType m_attackType;
    uint32 m_absorb;
    uint32 m_resist;
    uint32 m_block;
    uint32 m_cleanDamage;

    // amalgamation constructor (used for proc)
    DamageInfo(DamageInfo const& dmg1, DamageInfo const& dmg2);

public:
    explicit DamageInfo(Unit* _attacker, Unit* _victim, uint32 _damage, SpellInfo const* _spellInfo, SpellSchoolMask _schoolMask, DamageEffectType _damageType, uint32 cleanDamage = 0);
    explicit DamageInfo(CalcDamageInfo const& dmgInfo); // amalgamation wrapper
    DamageInfo(CalcDamageInfo const& dmgInfo, uint8 damageIndex);
    DamageInfo(SpellNonMeleeDamage const& spellNonMeleeDamage, DamageEffectType damageType);

    void ModifyDamage(int32 amount);
    void AbsorbDamage(uint32 amount);
    void ResistDamage(uint32 amount);
    void BlockDamage(uint32 amount);

    Unit* GetAttacker() const { return m_attacker; };
    Unit* GetVictim() const { return m_victim; };
    SpellInfo const* GetSpellInfo() const { return m_spellInfo; };
    SpellSchoolMask GetSchoolMask() const { return m_schoolMask; };
    DamageEffectType GetDamageType() const { return m_damageType; };
    WeaponAttackType GetAttackType() const { return m_attackType; };
    uint32 GetDamage() const { return m_damage; };
    uint32 GetAbsorb() const { return m_absorb; };
    uint32 GetResist() const { return m_resist; };
    uint32 GetBlock() const { return m_block; };

    uint32 GetUnmitigatedDamage() const;
};

class HealInfo
{
private:
    Unit* const m_healer;
    Unit* const m_target;
    uint32 m_heal;
    uint32 m_effectiveHeal;
    uint32 m_absorb;
    SpellInfo const* const m_spellInfo;
    SpellSchoolMask const m_schoolMask;
public:
    explicit HealInfo(Unit* _healer, Unit* _target, uint32 _heal, SpellInfo const* _spellInfo, SpellSchoolMask _schoolMask)
        : m_healer(_healer), m_target(_target), m_heal(_heal), m_spellInfo(_spellInfo), m_schoolMask(_schoolMask)
    {
        m_absorb = 0;
        m_effectiveHeal = 0;
    }

    void AbsorbHeal(uint32 amount)
    {
        amount = std::min(amount, GetHeal());
        m_absorb += amount;
        m_heal -= amount;

        amount = std::min(amount, GetEffectiveHeal());
        m_effectiveHeal -= amount;
    }

    void SetHeal(uint32 amount)
    {
        m_heal = amount;
    }

    void SetEffectiveHeal(uint32 amount)
    {
        m_effectiveHeal = amount;
    }

    Unit* GetHealer() const { return m_healer; }
    Unit* GetTarget() const { return m_target; }
    uint32 GetHeal() const { return m_heal; }
    uint32 GetEffectiveHeal() const { return m_effectiveHeal; }
    uint32 GetAbsorb() const { return m_absorb; }
    SpellInfo const* GetSpellInfo() const { return m_spellInfo; };
    SpellSchoolMask GetSchoolMask() const { return m_schoolMask; };
};

class ProcEventInfo
{
private:
    Unit* const _actor;
    Unit* const _actionTarget;
    Unit* const _procTarget;
    uint32 _typeMask;
    uint32 _spellTypeMask;
    uint32 _spellPhaseMask;
    uint32 _hitMask;
    uint32 _cooldown;
    Spell const* _spell;
    DamageInfo* _damageInfo;
    HealInfo* _healInfo;
    SpellInfo const* const _triggeredByAuraSpell;
    int8 _procAuraEffectIndex;
    std::optional<float> _chance;

public:
    explicit ProcEventInfo(Unit* actor, Unit* actionTarget, Unit* procTarget, uint32 typeMask, uint32 spellTypeMask, uint32 spellPhaseMask, uint32 hitMask, Spell const* spell, DamageInfo* damageInfo, HealInfo* healInfo, SpellInfo const* triggeredByAuraSpell = nullptr, int8 procAuraEffectIndex = -1);
    Unit* GetActor() { return _actor; };
    Unit* GetActionTarget() const { return _actionTarget; }
    Unit* GetProcTarget() const { return _procTarget; }
    uint32 GetTypeMask() const { return _typeMask; }
    uint32 GetSpellTypeMask() const { return _spellTypeMask; }
    uint32 GetSpellPhaseMask() const { return _spellPhaseMask; }
    uint32 GetHitMask() const { return _hitMask; }
    SpellInfo const* GetSpellInfo() const;
    SpellSchoolMask GetSchoolMask() const { return SPELL_SCHOOL_MASK_NONE; }
    Spell const* GetProcSpell() const { return _spell; }
    DamageInfo* GetDamageInfo() const { return _damageInfo; }
    HealInfo* GetHealInfo() const { return _healInfo; }
    SpellInfo const* GetTriggerAuraSpell() const { return _triggeredByAuraSpell; }
    int8 GetTriggerAuraEffectIndex() const { return _procAuraEffectIndex; }
    uint32 GetProcCooldown() const { return _cooldown; }
    void SetProcCooldown(uint32 cooldown) { _cooldown = cooldown; }
    std::optional<float> GetProcChance() const { return _chance; }
    void SetProcChance(float chance) { _chance = chance; }
    void ResetProcChance() { _chance.reset(); }
};

// Struct for use in Unit::CalculateMeleeDamage
// Need create structure like in SMSG_ATTACKERSTATEUPDATE opcode
struct CalcDamageInfo
{
    Unit*  attacker;             // Attacker
    Unit*  target;               // Target for damage

    struct
    {
        uint32 damageSchoolMask;
        uint32 damage;
        uint32 absorb;
        uint32 resist;
    } damages[MAX_ITEM_PROTO_DAMAGES];

    uint32 blocked_amount;
    uint32 HitInfo;
    uint32 TargetState;
    // Helper
    WeaponAttackType attackType; //
    uint32 procAttacker;
    uint32 procVictim;
    uint32 procEx;
    uint32 cleanDamage;          // Used only for rage calculation
    MeleeHitOutcome hitOutCome;  /// @todo: remove this field (need use TargetState)
};

// Spell damage info structure based on structure sending in SMSG_SPELLNONMELEEDAMAGELOG opcode
struct SpellNonMeleeDamage
{
    SpellNonMeleeDamage(Unit* _attacker, Unit* _target, SpellInfo const* _spellInfo, uint32 _schoolMask)
        : target(_target), attacker(_attacker), spellInfo(_spellInfo), damage(0), overkill(0), schoolMask(_schoolMask),
          absorb(0), resist(0), physicalLog(false), unused(false), blocked(0), HitInfo(0), cleanDamage(0)
    {}

    Unit* target;
    Unit* attacker;
    SpellInfo const* spellInfo;
    uint32 damage;
    uint32 overkill;
    uint32 schoolMask;
    uint32 absorb;
    uint32 resist;
    bool physicalLog;
    bool unused;
    uint32 blocked;
    uint32 HitInfo;
    // Used for help
    uint32 cleanDamage;
};

struct SpellPeriodicAuraLogInfo
{
    SpellPeriodicAuraLogInfo(AuraEffect const* _auraEff, uint32 _damage, uint32 _overDamage, uint32 _absorb, uint32 _resist, float _multiplier, bool _critical)
        : auraEff(_auraEff), damage(_damage), overDamage(_overDamage), absorb(_absorb), resist(_resist), multiplier(_multiplier), critical(_critical) {}

    AuraEffect const* auraEff;
    uint32 damage;
    uint32 overDamage;                                      // overkill/overheal
    uint32 absorb;
    uint32 resist;
    float  multiplier;
    bool   critical;
};

void createProcFlags(SpellInfo const* spellInfo, WeaponAttackType attackType, bool positive, uint32& procAttacker, uint32& procVictim);
uint32 createProcExtendMask(SpellNonMeleeDamage* damageInfo, SpellMissInfo missCondition);
#define MAX_DECLINED_NAME_CASES 5

struct DeclinedName
{
    std::string name[MAX_DECLINED_NAME_CASES];
};

enum CurrentSpellTypes
{
    CURRENT_MELEE_SPELL             = 0,
    CURRENT_GENERIC_SPELL           = 1,
    CURRENT_CHANNELED_SPELL         = 2,
    CURRENT_AUTOREPEAT_SPELL        = 3
};

#define CURRENT_FIRST_NON_MELEE_SPELL 1
#define CURRENT_MAX_SPELL             4

enum ReactStates : uint8
{
    REACT_PASSIVE    = 0,
    REACT_DEFENSIVE  = 1,
    REACT_AGGRESSIVE = 2
};

enum CommandStates : uint8
{
    COMMAND_STAY    = 0,
    COMMAND_FOLLOW  = 1,
    COMMAND_ATTACK  = 2,
    COMMAND_ABANDON = 3
};

typedef std::list<Player*> SharedVisionList;
struct AttackPosition {
    AttackPosition(Position pos) : _pos(std::move(pos)), _taken(false) {}
    bool operator==(const int val)
    {
        return !val;
    };
    int operator=(const int val)
    {
        if (!val)
        {
            // _pos = nullptr;
            _taken = false;
            return 0; // nullptr
        }
        return 0; // nullptr
    };
    Position _pos;
    bool _taken;
};

// for clearing special attacks
#define REACTIVE_TIMER_START 5000

enum ReactiveType
{
    REACTIVE_DEFENSE        = 0,
    REACTIVE_HUNTER_PARRY   = 1,
    REACTIVE_OVERPOWER      = 2,
    REACTIVE_WOLVERINE_BITE = 3,

    MAX_REACTIVE
};

#define SUMMON_SLOT_PET     0
#define SUMMON_SLOT_TOTEM   1
#define MAX_TOTEM_SLOT      5
#define SUMMON_SLOT_MINIPET 5
#define SUMMON_SLOT_QUEST   6
#define MAX_SUMMON_SLOT     7

#define MAX_GAMEOBJECT_SLOT 4

enum PlayerTotemType
{
    SUMMON_TYPE_TOTEM_FIRE  = 63,
    SUMMON_TYPE_TOTEM_EARTH = 81,
    SUMMON_TYPE_TOTEM_WATER = 82,
    SUMMON_TYPE_TOTEM_AIR   = 83,
};

/// Spell cooldown flags sent in SMSG_SPELL_COOLDOWN
enum SpellCooldownFlags
{
    SPELL_COOLDOWN_FLAG_NONE                    = 0x0,
    SPELL_COOLDOWN_FLAG_INCLUDE_GCD             = 0x1,  ///< Starts GCD in addition to normal cooldown specified in the packet
    SPELL_COOLDOWN_FLAG_INCLUDE_EVENT_COOLDOWNS = 0x2   ///< Starts GCD for spells that should start their cooldown on events, requires SPELL_COOLDOWN_FLAG_INCLUDE_GCD set
};

typedef std::unordered_map<uint32, uint32> PacketCooldowns;

// delay time next attack to prevent client attack animation problems
#define ATTACK_DISPLAY_DELAY 200
#define MAX_PLAYER_STEALTH_DETECT_RANGE 30.0f               // max distance for detection targets by player

struct SpellProcEventEntry;                                 // used only privately

class Unit : public WorldObject
{
public:
    typedef std::unordered_set<Unit*> AttackerSet;
    typedef std::set<Unit*> ControlSet;

    typedef std::multimap<uint32,  Aura*> AuraMap;
    typedef std::pair<AuraMap::const_iterator, AuraMap::const_iterator> AuraMapBounds;
    typedef std::pair<AuraMap::iterator, AuraMap::iterator> AuraMapBoundsNonConst;

    typedef std::multimap<uint32,  AuraApplication*> AuraApplicationMap;
    typedef std::pair<AuraApplicationMap::const_iterator, AuraApplicationMap::const_iterator> AuraApplicationMapBounds;
    typedef std::pair<AuraApplicationMap::iterator, AuraApplicationMap::iterator> AuraApplicationMapBoundsNonConst;

    typedef std::multimap<AuraStateType,  AuraApplication*> AuraStateAurasMap;
    typedef std::pair<AuraStateAurasMap::const_iterator, AuraStateAurasMap::const_iterator> AuraStateAurasMapBounds;

    typedef std::list<AuraEffect*> AuraEffectList;
    typedef std::list<Aura*> AuraList;
    typedef std::list<AuraApplication*> AuraApplicationList;
    typedef std::list<DiminishingReturn> Diminishing;
    typedef GuidUnorderedSet ComboPointHolderSet;

    typedef std::map<uint8, AuraApplication*> VisibleAuraMap;

    ~Unit() override;

    UnitAI* GetAI() { return i_AI; }
    void SetAI(UnitAI* newAI) { i_AI = newAI; }

    void AddToWorld() override;
    void RemoveFromWorld() override;

    void CleanupBeforeRemoveFromMap(bool finalCleanup);
    void CleanupsBeforeDelete(bool finalCleanup = true) override;                        // used in ~Creature/~Player (or before mass creature delete to remove cross-references to already deleted units)

    uint32 GetDynamicFlags() const override { return GetUInt32Value(UNIT_DYNAMIC_FLAGS); }
    void ReplaceAllDynamicFlags(uint32 flag) override { SetUInt32Value(UNIT_DYNAMIC_FLAGS, flag); }

    DiminishingLevels GetDiminishing(DiminishingGroup group);
    void IncrDiminishing(DiminishingGroup group);
    float ApplyDiminishingToDuration(DiminishingGroup group, int32& duration, Unit* caster, DiminishingLevels Level, int32 limitduration);
    void ApplyDiminishingAura(DiminishingGroup group, bool apply);
    void ClearDiminishings() { m_Diminishing.clear(); }

    // target dependent range checks
    float GetSpellMaxRangeForTarget(Unit const* target, SpellInfo const* spellInfo) const;
    float GetSpellMinRangeForTarget(Unit const* target, SpellInfo const* spellInfo) const;

    void Update(uint32 time) override;

    void setAttackTimer(WeaponAttackType type, int32 time) { m_attackTimer[type] = time; }  /// @todo - Look to convert to std::chrono
    void resetAttackTimer(WeaponAttackType type = BASE_ATTACK);
    int32 getAttackTimer(WeaponAttackType type) const { return m_attackTimer[type]; }
    bool isAttackReady(WeaponAttackType type = BASE_ATTACK) const { return m_attackTimer[type] <= 0; }
    bool haveOffhandWeapon() const;
    bool CanDualWield() const { return m_canDualWield; }
    virtual void SetCanDualWield(bool value) { m_canDualWield = value; }
    float GetCombatReach() const override { return m_floatValues[UNIT_FIELD_COMBATREACH]; }
    float GetMeleeReach() const { float reach = m_floatValues[UNIT_FIELD_COMBATREACH]; return reach > MIN_MELEE_REACH ? reach : MIN_MELEE_REACH; }
    bool IsWithinRange(Unit const* obj, float dist) const;
    bool IsWithinCombatRange(Unit const* obj, float dist2compare) const;
    bool IsWithinMeleeRange(Unit const* obj, float dist = 0.f) const;
    float GetMeleeRange(Unit const* target) const;
    virtual SpellSchoolMask GetMeleeDamageSchoolMask(WeaponAttackType attackType = BASE_ATTACK, uint8 damageIndex = 0) const = 0;
    bool GetRandomContactPoint(Unit const* target, float& x, float& y, float& z, bool force = false) const;
    uint32 m_extraAttacks;
    bool m_canDualWield;

    void _addAttacker(Unit* pAttacker)                  // must be called only from Unit::Attack(Unit*)
    {
        m_attackers.insert(pAttacker);
    }
    void _removeAttacker(Unit* pAttacker)               // must be called only from Unit::AttackStop()
    {
        m_attackers.erase(pAttacker);
    }
    Unit* getAttackerForHelper() const                 // If someone wants to help, who to give them
    {
        if (GetVictim() != nullptr)
            return GetVictim();

        if (!IsEngaged())
            return nullptr;

        if (!m_attackers.empty())
            return *(m_attackers.begin());

        return nullptr;
    }
    bool Attack(Unit* victim, bool meleeAttack);
    void CastStop(uint32 except_spellid = 0, bool withInstant = true);
    bool AttackStop();
    void RemoveAllAttackers();
    AttackerSet const& getAttackers() const { return m_attackers; }
    bool GetMeleeAttackPoint(Unit* attacker, Position& pos);
    bool isAttackingPlayer() const;
    Unit* GetVictim() const { return m_attacking; }

    void CombatStop(bool includingCast = false);
    void CombatStopWithPets(bool includingCast = false);
    void StopAttackFaction(uint32 faction_id);
    void StopAttackingInvalidTarget();
    Unit* SelectNearbyTarget(Unit* exclude = nullptr, float dist = NOMINAL_MELEE_RANGE) const;
    Unit* SelectNearbyNoTotemTarget(Unit* exclude = nullptr, float dist = NOMINAL_MELEE_RANGE) const;
    void SendMeleeAttackStop(Unit* victim = nullptr);
    void SendMeleeAttackStart(Unit* victim, Player* sendTo = nullptr);

    void AddUnitState(uint32 f) { m_state |= f; }
    bool HasUnitState(const uint32 f) const { return (m_state & f); }
    void ClearUnitState(uint32 f) { m_state &= ~f; }
    uint32 GetUnitState() const { return m_state; }
    bool CanFreeMove() const
    {
        return !HasUnitState(UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING | UNIT_STATE_ON_TAXI |
                             UNIT_STATE_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_DISTRACTED) && !GetOwnerGUID();
    }

    uint32 HasUnitTypeMask(uint32 mask) const { return mask & m_unitTypeMask; }
    void AddUnitTypeMask(uint32 mask) { m_unitTypeMask |= mask; }
    uint32 GetUnitTypeMask() const { return m_unitTypeMask; }
    bool IsSummon() const { return m_unitTypeMask & UNIT_MASK_SUMMON; }
    bool IsGuardian() const { return m_unitTypeMask & UNIT_MASK_GUARDIAN; }
    bool IsControllableGuardian() const { return m_unitTypeMask & UNIT_MASK_CONTROLABLE_GUARDIAN; }
    bool IsPet() const { return m_unitTypeMask & UNIT_MASK_PET; }
    bool IsHunterPet() const { return m_unitTypeMask & UNIT_MASK_HUNTER_PET; }
    bool IsTotem() const { return m_unitTypeMask & UNIT_MASK_TOTEM; }
    bool IsVehicle() const { return m_unitTypeMask & UNIT_MASK_VEHICLE; }

    uint32_t GetLevel() const { return GetUInt32Value(UNIT_FIELD_LEVEL); }
    uint8 getLevelForTarget(WorldObject const* /*target*/) const override { return GetLevel(); }
    void SetLevel(uint8 lvl, bool showLevelChange = true);
    uint8 getRace(bool original = false) const;
    void setRace(uint8 race);
    uint32 getRaceMask() const { return 1 << (getRace(true) - 1); }
    uint32_t GetClass() const { return GetByteValue(UNIT_FIELD_BYTES_0, 1); }
    virtual bool IsClass(Classes unitClass, [[maybe_unused]] ClassContext context = CLASS_CONTEXT_NONE) const { return (GetClass() == unitClass); }
    uint32 getClassMask() const { return 1 << (GetClass() - 1); }
    uint8 getGender() const { return GetByteValue(UNIT_FIELD_BYTES_0, 2); }
    DisplayRace GetDisplayRaceFromModelId(uint32 modelId) const;
    DisplayRace GetDisplayRace() const { return GetDisplayRaceFromModelId(GetDisplayId()); };

    float GetStat(Stats stat) const { return float(GetUInt32Value(static_cast<uint16>(UNIT_FIELD_STAT0) + stat)); }
    void SetStat(Stats stat, int32 val) { SetStatInt32Value(static_cast<uint16>(UNIT_FIELD_STAT0) + stat, val); }
    uint32 GetArmor() const { return GetResistance(SPELL_SCHOOL_NORMAL); }
    void SetArmor(int32 val) { SetResistance(SPELL_SCHOOL_NORMAL, val); }

    uint32 GetResistance(SpellSchools school) const { return GetUInt32Value(static_cast<uint16>(UNIT_FIELD_RESISTANCES) + school); }
    uint32 GetResistance(SpellSchoolMask mask) const;
    void SetResistance(SpellSchools school, int32 val) { SetStatInt32Value(static_cast<uint16>(UNIT_FIELD_RESISTANCES) + school, val); }
    static float GetEffectiveResistChance(Unit const* owner, SpellSchoolMask schoolMask, Unit const* victim);

    uint32 GetHealth()    const { return GetUInt32Value(UNIT_FIELD_HEALTH); }
    uint32 GetMaxHealth() const { return GetUInt32Value(UNIT_FIELD_MAXHEALTH); }

    bool HealthBelowPct(int32 pct) const { return GetHealth() < CountPctFromMaxHealth(pct); }
    bool HealthBelowPctDamaged(int32 pct, uint32 damage) const { return int64(GetHealth()) - int64(damage) < int64(CountPctFromMaxHealth(pct)); }
    bool HealthAbovePct(int32 pct) const { return GetHealth() > CountPctFromMaxHealth(pct); }
    bool HealthAbovePctHealed(int32 pct, uint32 heal) const { return uint64(GetHealth()) + uint64(heal) > CountPctFromMaxHealth(pct); }
    float GetHealthPct() const { return GetMaxHealth() ? 100.f * GetHealth() / GetMaxHealth() : 0.0f; }
    uint32 CountPctFromMaxHealth(int32 pct) const { return CalculatePct(GetMaxHealth(), pct); }
    uint32 CountPctFromCurHealth(int32 pct) const { return CalculatePct(GetHealth(), pct); }
    float GetPowerPct(POWER_TYPE power) const { return GetMaxPower(power) ? 100.f * GetPower(power) / GetMaxPower(power) : 0.0f; }

    void SetHealth(uint32 val);
    void SetMaxHealth(uint32 val);
    inline void SetFullHealth() { SetHealth(GetMaxHealth()); }
    int32 ModifyHealth(int32 val);
    int32 GetHealthGain(int32 dVal);

    POWER_TYPE GetPowerType() const { return POWER_TYPE(GetByteValue(UNIT_FIELD_BYTES_0, 3)); }
    void setPowerType(POWER_TYPE power);
    virtual bool HasActivePowerType(POWER_TYPE power) { return GetPowerType() == power; }
    uint32 GetPower(POWER_TYPE power) const { return GetUInt32Value(static_cast<uint16>(UNIT_FIELD_POWER1) + power); }
    uint32 GetMaxPower(POWER_TYPE power) const { return GetUInt32Value(static_cast<uint16>(UNIT_FIELD_MAXPOWER1) + power); }
    void SetPower(POWER_TYPE power, uint32 val, bool withPowerUpdate = true, bool fromRegenerate = false);
    void SetMaxPower(POWER_TYPE power, uint32 val);
    // returns the change in power
    int32 ModifyPower(POWER_TYPE power, int32 val, bool withPowerUpdate = true);
    int32 ModifyPowerPct(POWER_TYPE power, float pct, bool apply = true);

    uint32 GetAttackTime(WeaponAttackType att) const
    {
        float f_BaseAttackTime = GetFloatValue(static_cast<uint16>(UNIT_FIELD_BASEATTACKTIME) + att) / m_modAttackSpeedPct[att];
        return (uint32)f_BaseAttackTime;
    }

    void SetAttackTime(WeaponAttackType att, uint32 val) { SetFloatValue(static_cast<uint16>(UNIT_FIELD_BASEATTACKTIME) + att, val * m_modAttackSpeedPct[att]); }
    void ApplyAttackTimePercentMod(WeaponAttackType att, float val, bool apply);
    void ApplyCastTimePercentMod(float val, bool apply);

    void SetUInt32Value(uint16 index, uint32 value);

    UnitFlags GetUnitFlags() const { return UnitFlags(GetUInt32Value(UNIT_FIELD_FLAGS)); }
    bool HasUnitFlag(UnitFlags flags) const { return HasFlag(UNIT_FIELD_FLAGS, flags); }
    void SetUnitFlag(UnitFlags flags) { SetFlag(UNIT_FIELD_FLAGS, flags); }
    void RemoveUnitFlag(UnitFlags flags) { RemoveFlag(UNIT_FIELD_FLAGS, flags); }
    void ReplaceAllUnitFlags(UnitFlags flags) { SetUInt32Value(UNIT_FIELD_FLAGS, flags); }

    UnitFlags2 GetUnitFlags2() const { return UnitFlags2(GetUInt32Value(UNIT_FIELD_FLAGS_2)); }
    bool HasUnitFlag2(UnitFlags2 flags) const { return HasFlag(UNIT_FIELD_FLAGS_2, flags); }
    void SetUnitFlag2(UnitFlags2 flags) { SetFlag(UNIT_FIELD_FLAGS_2, flags); }
    void RemoveUnitFlag2(UnitFlags2 flags) { RemoveFlag(UNIT_FIELD_FLAGS_2, flags); }
    void ReplaceAllUnitFlags2(UnitFlags2 flags) { SetUInt32Value(UNIT_FIELD_FLAGS_2, flags); }

    SheathState GetSheath() const { return SheathState(GetByteValue(UNIT_FIELD_BYTES_2, 0)); }
    virtual void SetSheath(SheathState sheathed) { SetByteValue(UNIT_FIELD_BYTES_2, 0, sheathed); }

    // faction template id
    uint32 GetFaction() const { return GetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE); }
    void SetFaction(uint32 faction);
    FactionTemplateEntry const* GetFactionTemplateEntry() const;

    ReputationRank GetReactionTo(Unit const* target, bool checkOriginalFaction = false) const;
    ReputationRank GetFactionReactionTo(FactionTemplateEntry const* factionTemplateEntry, Unit const* target) const;

    bool IsHostileTo(Unit const* unit) const;
    bool IsHostileToPlayers() const;
    bool IsFriendlyTo(Unit const* unit) const;
    bool IsNeutralToAll() const;
    bool IsInPartyWith(Unit const* unit) const;
    bool IsInRaidWith(Unit const* unit) const;
    void GetPartyMembers(std::list<Unit*>& units);
    bool IsContestedGuard() const
    {
        if (FactionTemplateEntry const* entry = GetFactionTemplateEntry())
            return entry->IsContestedGuardFaction();

        return false;
    }
    bool IsInSanctuary() const { return HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_SANCTUARY); }
    bool IsPvP() const { return HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_PVP); }
    bool IsFFAPvP() const { return HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_FFA_PVP); }
    void SetPvP(bool state)
    {
        if (state)
            SetByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_PVP);
        else
            RemoveByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_PVP);
    }

    uint32 GetCreatureType() const;
    uint32 GetCreatureTypeMask() const
    {
        uint32 creatureType = GetCreatureType();
        return (creatureType >= 1) ? (1 << (creatureType - 1)) : 0;
    }

    UNITSTANDSTATE GetStandState() const { return static_cast<UNITSTANDSTATE>(GetByteValue(UNIT_FIELD_BYTES_1, 0)); }
    bool IsSitting() const;
    bool IsStanding() const;
    void SetStandState(uint8 state);

    void  SetStandFlags(uint8 flags) { SetByteFlag(UNIT_FIELD_BYTES_1,  UNIT_BYTES_1_OFFSET_VIS_FLAG, flags); }
    void  RemoveStandFlags(uint8 flags) { RemoveByteFlag(UNIT_FIELD_BYTES_1,  UNIT_BYTES_1_OFFSET_VIS_FLAG, flags); }

    bool IsMounted() const { return HasUnitFlag(UNIT_FLAG_MOUNT); }
    uint32 GetMountID() const { return GetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID); }
    void Mount(uint32 mount, uint32 vehicleId = 0, uint32 creatureEntry = 0);
    void Dismount();

    uint16 GetMaxSkillValueForLevel(Unit const* target = nullptr) const { return (target ? getLevelForTarget(target) : GetLevel()) * 5; }
    static void DealDamageMods(Unit const* victim, uint32& damage, uint32* absorb);
    static uint32 DealDamage(Unit* attacker, Unit* victim, uint32 damage, CleanDamage const* cleanDamage = nullptr, DamageEffectType damagetype = DIRECT_DAMAGE, SpellSchoolMask damageSchoolMask = SPELL_SCHOOL_MASK_NORMAL, SpellInfo const* spellProto = nullptr, bool durabilityLoss = true, bool allowGM = false, Spell const* spell = nullptr);
    static void Kill(Unit* killer, Unit* victim, bool durabilityLoss = true, WeaponAttackType attackType = BASE_ATTACK, SpellInfo const* spellProto = nullptr, Spell const* spell = nullptr);
    void KillSelf(bool durabilityLoss = true, WeaponAttackType attackType = BASE_ATTACK, SpellInfo const* spellProto = nullptr, Spell const* spell = nullptr) { Kill(this, this, durabilityLoss, attackType, spellProto, spell); };
    static int32 DealHeal(Unit* healer, Unit* victim, uint32 addhealth);

    static void ProcDamageAndSpell(Unit* actor, Unit* victim, uint32 procAttacker, uint32 procVictim, uint32 procEx, uint32 amount, WeaponAttackType attType = BASE_ATTACK, SpellInfo const* procSpellInfo = nullptr, SpellInfo const* procAura = nullptr, int8 procAuraEffectIndex = -1, Spell const* procSpell = nullptr, DamageInfo* damageInfo = nullptr, HealInfo* healInfo = nullptr, uint32 procPhase = 2 /*PROC_SPELL_PHASE_HIT*/);
    void ProcDamageAndSpellFor(bool isVictim, Unit* target, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, SpellInfo const* procSpellInfo, uint32 damage, SpellInfo const* procAura = nullptr, int8 procAuraEffectIndex = -1, Spell const* procSpell = nullptr, DamageInfo* damageInfo = nullptr, HealInfo* healInfo = nullptr, uint32 procPhase = 2 /*PROC_SPELL_PHASE_HIT*/);

    void GetProcAurasTriggeredOnEvent(std::list<AuraApplication*>& aurasTriggeringProc, std::list<AuraApplication*>* procAuras, ProcEventInfo eventInfo);
    void TriggerAurasProcOnEvent(CalcDamageInfo& damageInfo);
    void TriggerAurasProcOnEvent(std::list<AuraApplication*>* myProcAuras, std::list<AuraApplication*>* targetProcAuras, Unit* actionTarget, uint32 typeMaskActor, uint32 typeMaskActionTarget, uint32 spellTypeMask, uint32 spellPhaseMask, uint32 hitMask, Spell* spell, DamageInfo* damageInfo, HealInfo* healInfo);
    void TriggerAurasProcOnEvent(ProcEventInfo& eventInfo, std::list<AuraApplication*>& procAuras);

    void HandleEmoteCommand(uint32 emoteId);
    void AttackerStateUpdate (Unit* victim, WeaponAttackType attType = BASE_ATTACK, bool extra = false, bool ignoreCasting = false);

    void CalculateMeleeDamage(Unit* victim, CalcDamageInfo* damageInfo, WeaponAttackType attackType = BASE_ATTACK, const bool sittingVictim = false);
    void DealMeleeDamage(CalcDamageInfo* damageInfo, bool durabilityLoss);

    void HandleProcExtraAttackFor(Unit* victim, uint32 count);
    void SetLastExtraAttackSpell(uint32 spellId) { _lastExtraAttackSpell = spellId; }
    uint32 GetLastExtraAttackSpell() const { return _lastExtraAttackSpell; }
    void AddExtraAttacks(uint32 count);
    void SetLastDamagedTargetGuid(WOWGUID const& guid) { _lastDamagedTargetGuid = guid; }
    WOWGUID const& GetLastDamagedTargetGuid() const { return _lastDamagedTargetGuid; }

    void CalculateSpellDamageTaken(SpellNonMeleeDamage* damageInfo, int32 damage, SpellInfo const* spellInfo, WeaponAttackType attackType = BASE_ATTACK, bool crit = false);
    void DealSpellDamage(SpellNonMeleeDamage* damageInfo, bool durabilityLoss, Spell const* spell = nullptr);

    // player or player's pet resilience (-1%)
    float GetMeleeCritChanceReduction() const { return GetCombatRatingReduction(CR_CRIT_TAKEN_MELEE); }
    float GetRangedCritChanceReduction() const { return GetCombatRatingReduction(CR_CRIT_TAKEN_RANGED); }
    float GetSpellCritChanceReduction() const { return GetCombatRatingReduction(CR_CRIT_TAKEN_SPELL); }

    // player or player's pet resilience (-1%)
    uint32 GetMeleeCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_MELEE, 2.2f, 33.0f, damage); }
    uint32 GetRangedCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_RANGED, 2.2f, 33.0f, damage); }
    uint32 GetSpellCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_SPELL, 2.2f, 33.0f, damage); }

    // player or player's pet resilience (-1%), cap 100%
    uint32 GetMeleeDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_MELEE, 2.0f, 100.0f, damage); }
    uint32 GetRangedDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_RANGED, 2.0f, 100.0f, damage); }
    uint32 GetSpellDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_SPELL, 2.0f, 100.0f, damage); }

    static void ApplyResilience(Unit const* victim, float* crit, int32* damage, bool isCrit, CombatRating type);

    float MeleeSpellMissChance(Unit const* victim, WeaponAttackType attType, int32 skillDiff, uint32 spellId) const;
    SpellMissInfo MeleeSpellHitResult(Unit* victim, SpellInfo const* spell);
    SpellMissInfo MagicSpellHitResult(Unit* victim, SpellInfo const* spell);
    SpellMissInfo SpellHitResult(Unit* victim, SpellInfo const* spell, bool canReflect = false);
    SpellMissInfo SpellHitResult(Unit* victim, Spell const* spell, bool canReflect = false);

    float GetUnitDodgeChance()    const;
    float GetUnitParryChance()    const;
    float GetUnitBlockChance()    const;
    float GetUnitMissChance(WeaponAttackType attType)     const;
    float GetUnitCriticalChance(WeaponAttackType attackType, Unit const* victim) const;
    int32 GetMechanicResistChance(SpellInfo const* spell);
    bool CanUseAttackType(uint8 attacktype) const
    {
        switch (attacktype)
        {
            case BASE_ATTACK:
                return !HasUnitFlag(UNIT_FLAG_DISARMED);
            case OFF_ATTACK:
                return !HasUnitFlag2(UNIT_FLAG2_DISARM_OFFHAND);
            case RANGED_ATTACK:
                return !HasUnitFlag2(UNIT_FLAG2_DISARM_RANGED);
        }
        return true;
    }

    virtual uint32 GetShieldBlockValue() const = 0;
    uint32 GetShieldBlockValue(uint32 soft_cap, uint32 hard_cap) const
    {
        uint32 value = GetShieldBlockValue();
        if (value >= hard_cap)
        {
            value = (soft_cap + hard_cap) / 2;
        }
        else if (value > soft_cap)
        {
            value = soft_cap + ((value - soft_cap) / 2);
        }

        return value;
    }
    uint32 GetUnitMeleeSkill(Unit const* target = nullptr) const { return (target ? getLevelForTarget(target) : GetLevel()) * 5; }
    uint32 GetDefenseSkillValue(Unit const* target = nullptr) const;
    uint32 GetWeaponSkillValue(WeaponAttackType attType, Unit const* target = nullptr) const;
    float GetWeaponProcChance() const;
    float GetPPMProcChance(uint32 WeaponSpeed, float PPM,  SpellInfo const* spellProto) const;

    MeleeHitOutcome RollMeleeOutcomeAgainst (Unit const* victim, WeaponAttackType attType) const;
    MeleeHitOutcome RollMeleeOutcomeAgainst (Unit const* victim, WeaponAttackType attType, int32 crit_chance, int32 miss_chance, int32 dodge_chance, int32 parry_chance, int32 block_chance) const;

    NPCFlags GetNpcFlags() const { return NPCFlags(GetUInt32Value(UNIT_NPC_FLAGS)); }
    bool HasNpcFlag(NPCFlags flags) const { return HasFlag(UNIT_NPC_FLAGS, flags) != 0; }
    void SetNpcFlag(NPCFlags flags) { SetFlag(UNIT_NPC_FLAGS, flags); }
    void RemoveNpcFlag(NPCFlags flags) { RemoveFlag(UNIT_NPC_FLAGS, flags); }
    void ReplaceAllNpcFlags(NPCFlags flags) { SetUInt32Value(UNIT_NPC_FLAGS, flags); }

    bool IsVendor()       const { return HasNpcFlag(UNIT_NPC_FLAG_VENDOR); }
    bool IsTrainer()      const { return HasNpcFlag(UNIT_NPC_FLAG_TRAINER); }
    bool IsQuestGiver()   const { return HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER); }
    bool IsGossip()       const { return HasNpcFlag(UNIT_NPC_FLAG_GOSSIP); }
    bool IsTaxi()         const { return HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER); }
    bool IsGuildMaster()  const { return HasNpcFlag(UNIT_NPC_FLAG_PETITIONER); }
    bool IsBattleMaster() const { return HasNpcFlag(UNIT_NPC_FLAG_BATTLEMASTER); }
    bool IsBanker()       const { return HasNpcFlag(UNIT_NPC_FLAG_BANKER); }
    bool IsInnkeeper()    const { return HasNpcFlag(UNIT_NPC_FLAG_INNKEEPER); }
    bool IsSpiritHealer() const { return HasNpcFlag(UNIT_NPC_FLAG_SPIRITHEALER); }
    bool IsSpiritGuide()  const { return HasNpcFlag(UNIT_NPC_FLAG_SPIRITGUIDE); }
    bool IsTabardDesigner()const { return HasNpcFlag(UNIT_NPC_FLAG_TABARDDESIGNER); }
    bool IsAuctioner()    const { return HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER); }
    bool IsArmorer()      const { return HasNpcFlag(UNIT_NPC_FLAG_REPAIR); }
    bool IsServiceProvider() const
    {
        return HasFlag(UNIT_NPC_FLAGS,
                       UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_FLIGHTMASTER |
                       UNIT_NPC_FLAG_PETITIONER | UNIT_NPC_FLAG_BATTLEMASTER | UNIT_NPC_FLAG_BANKER |
                       UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_SPIRITHEALER |
                       UNIT_NPC_FLAG_SPIRITGUIDE | UNIT_NPC_FLAG_TABARDDESIGNER | UNIT_NPC_FLAG_AUCTIONEER);
    }
    bool IsSpiritService() const { return HasNpcFlag(UNIT_NPC_FLAG_SPIRITHEALER | UNIT_NPC_FLAG_SPIRITGUIDE); }
    bool IsCritter() const { return GetCreatureType() == CREATURE_TYPE_CRITTER; }

    bool IsOnTaxi () const { return HasUnitState(UNIT_STATE_ON_TAXI); }

    void SetImmuneToAll(bool apply, bool keepCombat = false) { SetImmuneToPC(apply, keepCombat); SetImmuneToNPC(apply, keepCombat); }
    bool IsImmuneToAll() const { return IsImmuneToPC() && IsImmuneToNPC(); }
    void SetImmuneToPC(bool apply, bool keepCombat = false);
    bool IsImmuneToPC() const { return HasUnitFlag(UNIT_FLAG_IMMUNE_TO_PC); }
    void SetImmuneToNPC(bool apply, bool keepCombat = false);
    bool IsImmuneToNPC() const { return HasUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC); }

    bool IsEngaged() const { return IsInCombat(); }
    bool IsEngagedBy(Unit const* who) const { return IsInCombatWith(who); }

    bool IsInCombat() const { return HasUnitFlag(UNIT_FLAG_IN_COMBAT); }
    bool IsInCombatWith(Unit const* who) const;

    bool IsPetInCombat() const { return HasUnitFlag(UNIT_FLAG_PET_IN_COMBAT); }
    void CombatStart(Unit* target, bool initialAggro = true);
    void CombatStartOnCast(Unit* target, bool initialAggro = true, uint32 duration = 0);
    void SetInCombatState(bool PvP, Unit* enemy = nullptr, uint32 duration = 0);
    void SetInCombatWith(Unit* enemy, uint32 duration = 0);
    void ClearInCombat();
    void ClearInPetCombat();
    uint32 GetCombatTimer() const { return m_CombatTimer; }
    void SetCombatTimer(uint32 timer) { m_CombatTimer = timer; }

    bool HasAuraTypeWithFamilyFlags(AuraType auraType, uint32 familyName, uint32 familyFlags) const;
    bool virtual HasSpell(uint32 /*spellID*/) const { return false; }
    bool HasBreakableByDamageAuraType(AuraType type, uint32 excludeAura = 0) const;
    bool HasBreakableByDamageCrowdControlAura(Unit* excludeCasterChannel = nullptr) const;

    bool HasStealthAura()      const { return HasAuraType(SPELL_AURA_MOD_STEALTH); }
    bool HasInvisibilityAura() const { return HasAuraType(SPELL_AURA_MOD_INVISIBILITY); }
    bool isFeared()  const { return HasAuraType(SPELL_AURA_MOD_FEAR); }
    bool isInRoots() const { return HasAuraType(SPELL_AURA_MOD_ROOT); }
    bool IsPolymorphed() const;

    bool isFrozen() const;

    bool isTargetableForAttack(bool checkFakeDeath = true, Unit const* byWho = nullptr) const;

    bool IsValidAttackTarget(Unit const* target, SpellInfo const* bySpell = nullptr) const;
    bool _IsValidAttackTarget(Unit const* target, SpellInfo const* bySpell, WorldObject const* obj = nullptr) const;

    bool IsValidAssistTarget(Unit const* target) const;
    bool _IsValidAssistTarget(Unit const* target, SpellInfo const* bySpell) const;

    virtual bool IsInWater() const;
    virtual bool IsUnderWater() const;
    bool isInAccessiblePlaceFor(Creature const* c) const;

    void SendHealSpellLog(HealInfo const& healInfo, bool critical = false);
    int32 HealBySpell(HealInfo& healInfo, bool critical = false);
    void SendEnergizeSpellLog(Unit* victim, uint32 SpellID, uint32 Damage, POWER_TYPE powertype);
    void EnergizeBySpell(Unit* victim, uint32 SpellID, uint32 Damage, POWER_TYPE powertype);

    SpellCastResult CastSpell(SpellCastTargets const& targets, SpellInfo const* spellInfo, CustomSpellValues const* value, TriggerCastFlags triggerFlags = TRIGGERED_NONE, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastSpell(Unit* victim, uint32 spellId, bool triggered, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastSpell(Unit* victim, uint32 spellId, TriggerCastFlags triggerFlags = TRIGGERED_NONE, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastSpell(Unit* victim, SpellInfo const* spellInfo, bool triggered, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastSpell(Unit* victim, SpellInfo const* spellInfo, TriggerCastFlags triggerFlags = TRIGGERED_NONE, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastSpell(float x, float y, float z, uint32 spellId, bool triggered, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastSpell(GameObject* go, uint32 spellId, bool triggered, Item* castItem = nullptr, AuraEffect* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastCustomSpell(Unit* victim, uint32 spellId, int32 const* bp0, int32 const* bp1, int32 const* bp2, bool triggered, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastCustomSpell(uint32 spellId, SpellValueMod mod, int32 value, Unit* victim, bool triggered, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastCustomSpell(uint32 spellId, SpellValueMod mod, int32 value, Unit* victim = nullptr, TriggerCastFlags triggerFlags = TRIGGERED_NONE, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    SpellCastResult CastCustomSpell(uint32 spellId, CustomSpellValues const& value, Unit* victim = nullptr, TriggerCastFlags triggerFlags = TRIGGERED_NONE, Item* castItem = nullptr, AuraEffect const* triggeredByAura = nullptr, WOWGUID originalCaster = WOWGUID::Empty);
    Aura* AddAura(uint32 spellId, Unit* target);
    Aura* AddAura(SpellInfo const* spellInfo, uint8 effMask, Unit* target);
    void SetAuraStack(uint32 spellId, Unit* target, uint32 stack);
    void SendPlaySpellVisual(uint32 id);
    void SendPlaySpellImpact(WOWGUID guid, uint32 id);
    void BuildCooldownPacket(WDataStore& data, uint8 flags, uint32 spellId, uint32 cooldown);
    void BuildCooldownPacket(WDataStore& data, uint8 flags, PacketCooldowns const& cooldowns);

    void DeMorph();

    void SendAttackStateUpdate(CalcDamageInfo* damageInfo);
    void SendAttackStateUpdate(uint32 HitInfo, Unit* target, uint8 SwingType, SpellSchoolMask damageSchoolMask, uint32 Damage, uint32 AbsorbDamage, uint32 Resist, VictimState TargetState, uint32 BlockedAmount);
    void SendSpellNonMeleeDamageLog(SpellNonMeleeDamage* log);
    void SendSpellNonMeleeReflectLog(SpellNonMeleeDamage* log, Unit* attacker);
    void SendSpellNonMeleeDamageLog(Unit* target, SpellInfo const* spellInfo, uint32 Damage, SpellSchoolMask damageSchoolMask, uint32 AbsorbedDamage, uint32 Resist, bool PhysicalDamage, uint32 Blocked, bool CriticalHit = false, bool Split = false);
    void SendPeriodicAuraLog(SpellPeriodicAuraLogInfo* pInfo);
    void SendSpellMiss(Unit* target, uint32 spellID, SpellMissInfo missInfo);
    void SendSpellDamageResist(Unit* target, uint32 spellId);
    void SendSpellDamageImmune(Unit* target, uint32 spellId);

    void NearTeleportTo(Position& pos, bool casting = false, bool vehicleTeleport = false, bool withPet = false, bool removeTransport = false);
    void NearTeleportTo(float x, float y, float z, float orientation, bool casting = false, bool vehicleTeleport = false, bool withPet = false, bool removeTransport = false);
    void SendTameFailure(uint8 result);
    void SendTeleportPacket(Position& pos);
    virtual bool UpdatePosition(float x, float y, float z, float ang, bool teleport = false);
    // returns true if unit's position really changed
    bool UpdatePosition(const Position& pos, bool teleport = false) { return UpdatePosition(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation(), teleport); }
    void UpdateOrientation(float orientation);
    void UpdateHeight(float newZ);

    void KnockbackFrom(float x, float y, float speedXY, float speedZ);
    void JumpTo(float speedXY, float speedZ, bool forward = true);
    void JumpTo(WorldObject* obj, float speedZ);

    void SendMonsterMove(float NewPosX, float NewPosY, float NewPosZ, uint32 TransitTime, SplineFlags sf = SPLINEFLAG_WALK_MODE); // pussywizard: need to just send packet, with no movement/spline
    void MonsterMoveWithSpeed(float x, float y, float z, float speed);
    //void SetFacing(float ori, WorldObject* obj = nullptr);
    //void SendMonsterMove(float NewPosX, float NewPosY, float NewPosZ, uint8 type, uint32 MOVEFLAG, uint32 Time, Player* player = nullptr);
    void SendMovementFlagUpdate(bool self = false);

    virtual bool SetWalk(bool enable);
    virtual bool SetDisableGravity(bool disable, bool packetOnly = false, bool updateAnimationTier = true);
    virtual bool SetSwim(bool enable);
    virtual bool SetCanFly(bool enable, bool packetOnly = false);
    virtual bool SetWaterWalking(bool enable, bool packetOnly = false);
    virtual bool SetFeatherFall(bool enable, bool packetOnly = false);
    virtual bool SetHover(bool enable, bool packetOnly = false, bool updateAnimationTier = true);

    void SendMovementWaterWalking(Player* sendTo);
    void SendMovementFeatherFall(Player* sendTo);
    void SendMovementHover(Player* sendTo);

    void SetInFront(WorldObject const* target);
    void SetFacingTo(float ori);
    void SetFacingToObject(WorldObject* object);

    void SendChangeCurrentVictimOpcode(HostileReference* pHostileReference);
    void SendClearThreatListOpcode();
    void SendRemoveFromThreatListOpcode(HostileReference* pHostileReference);
    void SendThreatListUpdate();

    void SendClearTarget();

    void BuildHeartBeatMsg(WDataStore* data) const;

    bool IsAlive() const { return (m_deathState == DeathState::Alive); };
    bool isDying() const { return (m_deathState == DeathState::JustDied); };
    bool isDead() const { return (m_deathState == DeathState::Dead || m_deathState == DeathState::Corpse); };
    DeathState getDeathState() { return m_deathState; };
    virtual void setDeathState(DeathState s, bool despawn = false);           // overwrited in Creature/Player/Pet

    WOWGUID GetOwnerGUID() const { return GetGuidValue(UNIT_FIELD_SUMMONEDBY); }
    void SetOwnerGUID(WOWGUID owner);
    WOWGUID GetCreatorGUID() const { return GetGuidValue(UNIT_FIELD_CREATEDBY); }
    void SetCreatorGUID(WOWGUID creator) { SetGuidValue(UNIT_FIELD_CREATEDBY, creator); }
    WOWGUID GetMinionGUID() const { return GetGuidValue(UNIT_FIELD_SUMMON); }
    void SetMinionGUID(WOWGUID guid) { SetGuidValue(UNIT_FIELD_SUMMON, guid); }
    WOWGUID GetCharmerGUID() const { return GetGuidValue(UNIT_FIELD_CHARMEDBY); }
    void SetCharmerGUID(WOWGUID owner) { SetGuidValue(UNIT_FIELD_CHARMEDBY, owner); }
    WOWGUID GetCharmGUID() const { return  GetGuidValue(UNIT_FIELD_CHARM); }
    void SetPetGUID(WOWGUID guid) { m_SummonSlot[SUMMON_SLOT_PET] = guid; }
    WOWGUID GetPetGUID() const { return m_SummonSlot[SUMMON_SLOT_PET]; }
    void SetCritterGUID(WOWGUID guid) { SetGuidValue(UNIT_FIELD_CRITTER, guid); }
    WOWGUID GetCritterGUID() const { return GetGuidValue(UNIT_FIELD_CRITTER); }

    bool IsControlledByPlayer() const { return m_ControlledByPlayer; }
    bool IsCreatedByPlayer() const { return m_CreatedByPlayer; }
    WOWGUID GetCharmerOrOwnerGUID() const { return GetCharmerGUID() ? GetCharmerGUID() : GetOwnerGUID(); }
    WOWGUID GetCharmerOrOwnerOrOwnGUID() const
    {
        if (WOWGUID guid = GetCharmerOrOwnerGUID())
            return guid;

        return GetGUID();
    }
    bool IsCharmedOwnedByPlayerOrPlayer() const { return GetCharmerOrOwnerOrOwnGUID().IsPlayer(); }

    Player* GetSpellModOwner() const;

    Unit* GetOwner() const;
    Guardian* GetGuardianPet() const;
    Minion* GetFirstMinion() const;
    Unit* GetCharmer() const;
    Unit* GetCharm() const;
    Unit* GetCharmerOrOwner() const { return GetCharmerGUID() ? GetCharmer() : GetOwner(); }
    Unit* GetCharmerOrOwnerOrSelf() const
    {
        if (Unit* u = GetCharmerOrOwner())
            return u;

        return (Unit*)this;
    }
    Player* GetCharmerOrOwnerPlayerOrPlayerItself() const;
    Player* GetAffectingPlayer() const;

    void SetMinion(Minion* minion, bool apply);
    void GetAllMinionsByEntry(std::list<Creature*>& Minions, uint32 entry);
    void RemoveAllMinionsByEntry(uint32 entry);
    void SetCharm(Unit* target, bool apply);
    Unit* GetNextRandomRaidMemberOrPet(float radius);
    bool SetCharmedBy(Unit* charmer, CharmType type, AuraApplication const* aurApp = nullptr);
    void RemoveCharmedBy(Unit* charmer);
    void RestoreFaction();

    ControlSet m_Controlled;
    Unit* GetFirstControlled() const;
    void RemoveAllControlled(bool onDeath = false);

    bool IsCharmed() const { return GetCharmerGUID(); }
    bool isPossessed() const { return HasUnitState(UNIT_STATE_POSSESSED); }
    bool isPossessedByPlayer() const { return HasUnitState(UNIT_STATE_POSSESSED) && GetCharmerGUID().IsPlayer(); }
    bool isPossessing() const
    {
        if (Unit* u = GetCharm())
            return u->isPossessed();
        else
            return false;
    }
    bool isPossessing(Unit* u) const { return u->isPossessed() && GetCharmGUID() == u->GetGUID(); }

    CharmInfo* GetCharmInfo() { return m_charmInfo; }
    CharmInfo* InitCharmInfo();
    void DeleteCharmInfo();
    void UpdateCharmAI();
    //Player* GetMoverSource() const;
    SafeUnitPointer m_movedByPlayer;
    SharedVisionList const& GetSharedVisionList() { return m_sharedVision; }
    void AddPlayerToVision(Player* player);
    void RemovePlayerFromVision(Player* player);
    bool HasSharedVision() const { return !m_sharedVision.empty(); }
    void RemoveBindSightAuras();
    void RemoveCharmAuras();

    Pet* CreatePet(Creature* creatureTarget, uint32 spell_id = 0);
    Pet* CreatePet(uint32 creatureEntry, uint32 spell_id = 0);
    bool InitTamedPet(Pet* pet, uint8 level, uint32 spell_id);

    // aura apply/remove helpers - you should better not use these
    Aura* _TryStackingOrRefreshingExistingAura(SpellInfo const* newAura, uint8 effMask, Unit* caster, int32* baseAmount = nullptr, Item* castItem = nullptr, WOWGUID casterGUID = WOWGUID::Empty, bool periodicReset = false);
    void _AddAura(UnitAura* aura, Unit* caster);
    AuraApplication* _CreateAuraApplication(Aura* aura, uint8 effMask);
    void _ApplyAuraEffect(Aura* aura, uint8 effIndex);
    void _ApplyAura(AuraApplication* aurApp, uint8 effMask);
    void _UnapplyAura(AuraApplicationMap::iterator& i, AuraRemoveMode removeMode);
    void _UnapplyAura(AuraApplication* aurApp, AuraRemoveMode removeMode);
    void _RemoveNoStackAuraApplicationsDueToAura(Aura* aura);
    void _RemoveNoStackAurasDueToAura(Aura* aura);
    bool _IsNoStackAuraDueToAura(Aura* appliedAura, Aura* existingAura) const;
    void _RegisterAuraEffect(AuraEffect* aurEff, bool apply);

    // m_ownedAuras container management
    AuraMap&       GetOwnedAuras()       { return m_ownedAuras; }
    AuraMap const& GetOwnedAuras() const { return m_ownedAuras; }

    void RemoveOwnedAura(AuraMap::iterator& i, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
    void RemoveOwnedAura(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, uint8 reqEffMask = 0, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
    void RemoveOwnedAura(Aura* aura, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);

    Aura* GetOwnedAura(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, WOWGUID itemCasterGUID = WOWGUID::Empty, uint8 reqEffMask = 0, Aura* except = nullptr) const;

    // m_appliedAuras container management
    AuraApplicationMap&       GetAppliedAuras()       { return m_appliedAuras; }
    AuraApplicationMap const& GetAppliedAuras() const { return m_appliedAuras; }

    void RemoveAura(AuraApplicationMap::iterator& i, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
    void RemoveAura(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, uint8 reqEffMask = 0, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
    void RemoveAura(AuraApplication* aurApp, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
    void RemoveAura(Aura* aur, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);

    // Convenience methods removing auras by predicate
    void RemoveAppliedAuras(std::function<bool(AuraApplication const*)> const& check);
    void RemoveOwnedAuras(std::function<bool(Aura const*)> const& check);

    // Optimized overloads taking advantage of map key
    void RemoveAppliedAuras(uint32 spellId, std::function<bool(AuraApplication const*)> const& check);
    void RemoveOwnedAuras(uint32 spellId, std::function<bool(Aura const*)> const& check);

    void RemoveAurasDueToSpell(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, uint8 reqEffMask = 0, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
    void RemoveAuraFromStack(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
    void RemoveAurasDueToSpellByDispel(uint32 spellId, uint32 dispellerSpellId, WOWGUID casterGUID, Unit* dispeller, uint8 chargesRemoved = 1);
    void RemoveAurasDueToSpellBySteal(uint32 spellId, WOWGUID casterGUID, Unit* stealer);
    void RemoveAurasDueToItemSpell(uint32 spellId, WOWGUID castItemGuid);
    void RemoveAurasByType(AuraType auraType, WOWGUID casterGUID = WOWGUID::Empty, Aura* except = nullptr, bool negative = true, bool positive = true);
    void RemoveNotOwnSingleTargetAuras();
    void RemoveAurasWithInterruptFlags(uint32 flag, uint32 except = 0, bool isAutoshot = false);
    void RemoveAurasWithAttribute(uint32 flags);
    void RemoveAurasWithFamily(SpellFamilyNames family, uint32 familyFlag1, uint32 familyFlag2, uint32 familyFlag3, WOWGUID casterGUID);
    void RemoveAurasWithMechanic(uint32 mechanic_mask, AuraRemoveMode removemode = AURA_REMOVE_BY_DEFAULT, uint32 except = 0);
    void RemoveMovementImpairingAuras(bool withRoot);
    void RemoveAurasByShapeShift();

    void RemoveAreaAurasDueToLeaveWorld();
    void RemoveAllAuras();
    void RemoveArenaAuras();
    void RemoveAllAurasOnDeath();
    void RemoveAllAurasRequiringDeadTarget();
    void RemoveAllAurasExceptType(AuraType type);
    //void RemoveAllAurasExceptType(AuraType type1, AuraType type2); // pussywizard: replaced with RemoveEvadeAuras()
    void RemoveEvadeAuras();
    void DelayOwnedAuras(uint32 spellId, WOWGUID caster, int32 delaytime);

    void _RemoveAllAuraStatMods();
    void _ApplyAllAuraStatMods();

    AuraEffectList const& GetAuraEffectsByType(AuraType type) const { return m_modAuras[type]; }
    AuraList&       GetSingleCastAuras()       { return m_scAuras; }
    AuraList const& GetSingleCastAuras() const { return m_scAuras; }

    AuraEffect* GetAuraEffect(uint32 spellId, uint8 effIndex, WOWGUID casterGUID = WOWGUID::Empty) const;
    AuraEffect* GetAuraEffectOfRankedSpell(uint32 spellId, uint8 effIndex, WOWGUID casterGUID = WOWGUID::Empty) const;
    AuraEffect* GetAuraEffect(AuraType type, SpellFamilyNames name, uint32 iconId, uint8 effIndex) const; // spell mustn't have familyflags
    AuraEffect* GetAuraEffect(AuraType type, SpellFamilyNames family, uint32 familyFlag1, uint32 familyFlag2, uint32 familyFlag3, WOWGUID casterGUID = WOWGUID::Empty) const;
    AuraEffect* GetAuraEffectDummy(uint32 spellid) const;
    inline AuraEffect* GetDummyAuraEffect(SpellFamilyNames name, uint32 iconId, uint8 effIndex) const { return GetAuraEffect(SPELL_AURA_DUMMY, name, iconId, effIndex);}

    AuraApplication* GetAuraApplication(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, WOWGUID itemCasterGUID = WOWGUID::Empty, uint8 reqEffMask = 0, AuraApplication* except = nullptr) const;
    Aura* GetAura(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, WOWGUID itemCasterGUID = WOWGUID::Empty, uint8 reqEffMask = 0) const;

    AuraApplication* GetAuraApplicationOfRankedSpell(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, WOWGUID itemCasterGUID = WOWGUID::Empty, uint8 reqEffMask = 0, AuraApplication* except = nullptr) const;
    Aura* GetAuraOfRankedSpell(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, WOWGUID itemCasterGUID = WOWGUID::Empty, uint8 reqEffMask = 0) const;

    void GetDispellableAuraList(Unit* caster, uint32 dispelMask, DispelChargesList& dispelList, SpellInfo const* dispelSpell);

    bool HasAuraEffect(uint32 spellId, uint8 effIndex, WOWGUID caster = WOWGUID::Empty) const;
    uint32 GetAuraCount(uint32 spellId) const;
    bool HasAura(uint32 spellId, WOWGUID casterGUID = WOWGUID::Empty, WOWGUID itemCasterGUID = WOWGUID::Empty, uint8 reqEffMask = 0) const;
    bool HasAuraType(AuraType auraType) const;
    bool HasAuraTypeWithCaster(AuraType auratype, WOWGUID caster) const;
    bool HasAuraTypeWithMiscvalue(AuraType auratype, int32 miscvalue) const;
    bool HasAuraTypeWithAffectMask(AuraType auratype, SpellInfo const* affectedSpell) const;
    bool HasAuraTypeWithValue(AuraType auratype, int32 value) const;
    bool HasAuraTypeWithTriggerSpell(AuraType auratype, uint32 triggerSpell) const;
    bool HasNegativeAuraWithInterruptFlag(uint32 flag, WOWGUID guid = WOWGUID::Empty);
    bool HasVisibleAuraType(AuraType auraType) const;
    bool HasNegativeAuraWithAttribute(uint32 flag, WOWGUID guid = WOWGUID::Empty);
    bool HasAuraWithMechanic(uint32 mechanicMask) const;

    AuraEffect* IsScriptOverriden(SpellInfo const* spell, int32 script) const;
    uint32 GetDiseasesByCaster(WOWGUID casterGUID, uint8 mode = 0);
    uint32 GetDoTsByCaster(WOWGUID casterGUID) const;

    int32 GetTotalAuraModifierAreaExclusive(AuraType auratype) const;
    int32 GetTotalAuraModifier(AuraType auratype) const;
    float GetTotalAuraMultiplier(AuraType auratype) const;
    int32 GetMaxPositiveAuraModifier(AuraType auratype);
    int32 GetMaxNegativeAuraModifier(AuraType auratype) const;

    int32 GetTotalAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const;
    float GetTotalAuraMultiplierByMiscMask(AuraType auratype, uint32 misc_mask) const;
    int32 GetMaxPositiveAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask, const AuraEffect* except = nullptr) const;
    int32 GetMaxNegativeAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const;

    int32 GetTotalAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const;
    float GetTotalAuraMultiplierByMiscValue(AuraType auratype, int32 misc_value) const;
    int32 GetMaxPositiveAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const;
    int32 GetMaxNegativeAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const;

    int32 GetTotalAuraModifierByAffectMask(AuraType auratype, SpellInfo const* affectedSpell) const;
    float GetTotalAuraMultiplierByAffectMask(AuraType auratype, SpellInfo const* affectedSpell) const;
    int32 GetMaxPositiveAuraModifierByAffectMask(AuraType auratype, SpellInfo const* affectedSpell) const;
    int32 GetMaxNegativeAuraModifierByAffectMask(AuraType auratype, SpellInfo const* affectedSpell) const;

    float GetResistanceBuffMods(SpellSchools school, bool positive) const { return GetFloatValue(positive ? static_cast<uint16>(UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE) + school : static_cast<uint16>(UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE) +  + school); }
    void SetResistanceBuffMods(SpellSchools school, bool positive, float val) { SetFloatValue(positive ? static_cast<uint16>(UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE) + school : static_cast<uint16>(UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE) +  + school, val); }
    void ApplyResistanceBuffModsMod(SpellSchools school, bool positive, float val, bool apply) { ApplyModSignedFloatValue(positive ? static_cast<uint16>(UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE) + school : static_cast<uint16>(UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE) +  + school, val, apply); }
    void ApplyResistanceBuffModsPercentMod(SpellSchools school, bool positive, float val, bool apply) { ApplyPercentModFloatValue(positive ? static_cast<uint16>(UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE) + school : static_cast<uint16>(UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE) +  + school, val, apply); }
    void InitStatBuffMods()
    {
        for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i) SetFloatValue(static_cast<uint16>(UNIT_FIELD_POSSTAT0) +  i, 0);
        for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i) SetFloatValue(static_cast<uint16>(UNIT_FIELD_NEGSTAT0) +  i, 0);
    }
    void ApplyStatBuffMod(Stats stat, float val, bool apply) { ApplyModSignedFloatValue((val > 0 ? static_cast<uint16>(UNIT_FIELD_POSSTAT0) +  stat : static_cast<uint16>(UNIT_FIELD_NEGSTAT0) +  stat), val, apply); }
    void ApplyStatPercentBuffMod(Stats stat, float val, bool apply);

    void SetCreateStat(Stats stat, float val) { m_createStats[stat] = val; }
    void SetCreateHealth(uint32 val) { SetUInt32Value(UNIT_FIELD_BASE_HEALTH, val); }
    uint32 GetCreateHealth() const { return GetUInt32Value(UNIT_FIELD_BASE_HEALTH); }
    void SetCreateMana(uint32 val) { SetUInt32Value(UNIT_FIELD_BASE_MANA, val); }
    uint32 GetCreateMana() const { return GetUInt32Value(UNIT_FIELD_BASE_MANA); }
    uint32 GetCreatePowers(POWER_TYPE power) const;
    float GetPosStat(Stats stat) const { return GetFloatValue(static_cast<uint16>(UNIT_FIELD_POSSTAT0) +  stat); }
    float GetNegStat(Stats stat) const { return GetFloatValue(static_cast<uint16>(UNIT_FIELD_NEGSTAT0) +  stat); }
    float GetCreateStat(Stats stat) const { return m_createStats[stat]; }

    void SetCurrentCastedSpell(Spell* pSpell);
    virtual void ProhibitSpellSchool(SpellSchoolMask /*idSchoolMask*/, uint32 /*unTimeMs*/) { }
    void InterruptSpell(CurrentSpellTypes spellType, bool withDelayed = true, bool withInstant = true, bool bySelf = false);
    void FinishSpell(CurrentSpellTypes spellType, bool ok = true);

    // set withDelayed to true to account delayed spells as casted
    // delayed+channeled spells are always accounted as casted
    // we can skip channeled or delayed checks using flags
    bool IsNonMeleeSpellCast(bool withDelayed, bool skipChanneled = false, bool skipAutorepeat = false, bool isAutoshoot = false, bool skipInstant = true) const;

    // set withDelayed to true to interrupt delayed spells too
    // delayed+channeled spells are always interrupted
    void InterruptNonMeleeSpells(bool withDelayed, uint32 spellid = 0, bool withInstant = true, bool bySelf = false);

    Spell* GetCurrentSpell(CurrentSpellTypes spellType) const { return m_currentSpells[spellType]; }
    Spell* GetCurrentSpell(uint32 spellType) const { return m_currentSpells[spellType]; }
    Spell* FindCurrentSpellBySpellId(uint32 spell_id) const;
    int32 GetCurrentSpellCastTime(uint32 spell_id) const;

    virtual bool IsMovementPreventedByCasting() const;

    WOWGUID m_SummonSlot[MAX_SUMMON_SLOT];
    WOWGUID m_ObjectSlot[MAX_GAMEOBJECT_SLOT];

    ShapeshiftForm GetShapeshiftForm() const { return ShapeshiftForm(GetByteValue(UNIT_FIELD_BYTES_2, 3)); }
    void SetShapeshiftForm(ShapeshiftForm form)
    {
        SetByteValue(UNIT_FIELD_BYTES_2, 3, form);
    }

    bool IsInFeralForm() const
    {
        ShapeshiftForm form = GetShapeshiftForm();
        return form == FORM_CAT || form == FORM_BEAR || form == FORM_DIREBEAR || form == FORM_GHOSTWOLF; // Xinef: added shamans Ghost Wolf, should behave exactly like druid forms
    }

    bool IsInDisallowedMountForm() const;

    float m_modMeleeHitChance;
    float m_modRangedHitChance;
    float m_modSpellHitChance;
    int32 m_baseSpellCritChance;

    float m_threatModifier[MAX_SPELL_SCHOOL];
    float m_modAttackSpeedPct[3];

    // Event handler
    EventProcessor m_Events;

    // stat system
    bool HandleStatModifier(UnitMods unitMod, UnitModifierType modifierType, float amount, bool apply);
    void SetModifierValue(UnitMods unitMod, UnitModifierType modifierType, float value) { m_auraModifiersGroup[unitMod][modifierType] = value; }
    float GetModifierValue(UnitMods unitMod, UnitModifierType modifierType) const;
    float GetTotalStatValue(Stats stat, float additionalValue = 0.0f) const;
    float GetTotalAuraModValue(UnitMods unitMod) const;
    SpellSchools GetSpellSchoolByAuraGroup(UnitMods unitMod) const;
    Stats GetStatByAuraGroup(UnitMods unitMod) const;
    POWER_TYPE GetPowerTypeByAuraGroup(UnitMods unitMod) const;
    bool CanModifyStats() const { return m_canModifyStats; }
    void SetCanModifyStats(bool modifyStats) { m_canModifyStats = modifyStats; }
    virtual bool UpdateStats(Stats stat) = 0;
    virtual bool UpdateAllStats() = 0;
    virtual void UpdateResistances(uint32 school) = 0;
    virtual void UpdateAllResistances();
    virtual void UpdateArmor() = 0;
    virtual void UpdateMaxHealth() = 0;
    virtual void UpdateMaxPower(POWER_TYPE power) = 0;
    virtual void UpdateAttackPowerAndDamage(bool ranged = false) = 0;
    virtual void UpdateDamagePhysical(WeaponAttackType attType);
    float GetTotalAttackPowerValue(WeaponAttackType attType, Unit* pVictim = nullptr) const;
    float GetWeaponDamageRange(WeaponAttackType attType, WeaponDamageRange type, uint8 damageIndex = 0) const;
    void SetBaseWeaponDamage(WeaponAttackType attType, WeaponDamageRange damageRange, float value, uint8 damageIndex = 0) { m_weaponDamage[attType][damageRange][damageIndex] = value; }
    virtual void CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage, uint8 damageIndex = 0) = 0;
    uint32 CalculateDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, uint8 itemDamagesMask = 0);
    float GetAPMultiplier(WeaponAttackType attType, bool normalized);

    bool isInFrontInMap(Unit const* target, float distance, float arc = M_PI) const;
    bool isInBackInMap(Unit const* target, float distance, float arc = M_PI) const;

    // Visibility system
    bool IsVisible() const { return m_serverSideVisibility.GetValue(SERVERSIDE_VISIBILITY_GM) <= SEC_PLAYER; }
    void SetVisible(bool x);
    void SetModelVisible(bool on);

    // common function for visibility checks for player/creatures with detection code
    uint32 GetPhaseByAuras() const;
    void SetPhaseMask(uint32 newPhaseMask, bool update) override;// overwrite WorldObject::SetPhaseMask
    void UpdateObjectVisibility(bool forced = true, bool fromUpdate = false) override;

    SpellImmuneList m_spellImmune[MAX_SPELL_IMMUNITY];
    uint32 m_lastSanctuaryTime;

    // Threat related methods
    bool CanHaveThreatList() const;
    void AddThreat(Unit* victim, float fThreat, SpellSchoolMask schoolMask = SPELL_SCHOOL_MASK_NORMAL, SpellInfo const* threatSpell = nullptr);
    float ApplyTotalThreatModifier(float fThreat, SpellSchoolMask schoolMask = SPELL_SCHOOL_MASK_NORMAL);
    void TauntApply(Unit* victim);
    void TauntFadeOut(Unit* taunter);
    ThreatMgr& GetThreatMgr() { return m_ThreatMgr; }
    ThreatMgr const& GetThreatMgr() const { return m_ThreatMgr; }
    void addHatedBy(HostileReference* pHostileReference) { m_HostileRefMgr.insertFirst(pHostileReference); };
    void removeHatedBy(HostileReference* /*pHostileReference*/) { /* nothing to do yet */ }
    HostileRefMgr& getHostileRefMgr() { return m_HostileRefMgr; }

    VisibleAuraMap const* GetVisibleAuras() { return &m_visibleAuras; }
    AuraApplication* GetVisibleAura(uint8 slot)
    {
        VisibleAuraMap::iterator itr = m_visibleAuras.find(slot);
        if (itr != m_visibleAuras.end())
            return itr->second;
        return nullptr;
    }
    void SetVisibleAura(uint8 slot, AuraApplication* aur) { m_visibleAuras[slot] = aur; UpdateAuraForGroup(slot);}
    void RemoveVisibleAura(uint8 slot) { m_visibleAuras.erase(slot); UpdateAuraForGroup(slot);}

    uint32 GetInterruptMask() const { return m_interruptMask; }
    void AddInterruptMask(uint32 mask) { m_interruptMask |= mask; }
    void UpdateInterruptMask();

    virtual float GetNativeObjectScale() const { return 1.0f; }
    virtual void RecalculateObjectScale();
    uint32 GetDisplayId() const { return GetUInt32Value(UNIT_FIELD_DISPLAYID); }
    virtual void SetDisplayId(uint32 modelId, float displayScale = 1.f);
    uint32 GetNativeDisplayId() const { return GetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID); }
    void RestoreDisplayId();
    void SetNativeDisplayId(uint32 displayId) { SetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID, displayId); }
    void setTransForm(uint32 spellid) { m_transform = spellid;}
    uint32 getTransForm() const { return m_transform;}

    // DynamicObject management
    void _RegisterDynObject(DynamicObject* dynObj);
    void _UnregisterDynObject(DynamicObject* dynObj);
    DynamicObject* GetDynObject(uint32 spellId);
    bool RemoveDynObject(uint32 spellId);
    void RemoveAllDynObjects();

    GameObject* GetGameObject(uint32 spellId) const;
    void AddGameObject(GameObject* gameObj);
    void RemoveGameObject(GameObject* gameObj, bool del);
    void RemoveGameObject(uint32 spellid, bool del);
    void RemoveAllGameObjects();

    void ModifyAuraState(AuraStateType flag, bool apply);
    uint32 BuildAuraStateUpdateForTarget(Unit* target) const;
    bool HasAuraState(AuraStateType flag, SpellInfo const* spellProto = nullptr, Unit const* Caster = nullptr) const;
    void UnsummonAllTotems(bool onDeath = false);
    Unit* GetMagicHitRedirectTarget(Unit* victim, SpellInfo const* spellInfo);
    Unit* GetMeleeHitRedirectTarget(Unit* victim, SpellInfo const* spellInfo = nullptr);

    int32 SpellBaseDamageBonusDone(SpellSchoolMask schoolMask);
    int32 SpellBaseDamageBonusTaken(SpellSchoolMask schoolMask, bool isDoT = false);
    float SpellPctDamageModsDone(Unit* victim, SpellInfo const* spellProto, DamageEffectType damagetype);
    uint32 SpellDamageBonusDone(Unit* victim, SpellInfo const* spellProto, uint32 pdamage, DamageEffectType damagetype, uint8 effIndex, float TotalMod = 0.0f, uint32 stack = 1);
    uint32 SpellDamageBonusTaken(Unit* caster, SpellInfo const* spellProto, uint32 pdamage, DamageEffectType damagetype, uint32 stack = 1);
    int32 SpellBaseHealingBonusDone(SpellSchoolMask schoolMask);
    int32 SpellBaseHealingBonusTaken(SpellSchoolMask schoolMask);
    float SpellPctHealingModsDone(Unit* victim, SpellInfo const* spellProto, DamageEffectType damagetype);
    uint32 SpellHealingBonusDone(Unit* victim, SpellInfo const* spellProto, uint32 healamount, DamageEffectType damagetype, uint8 effIndex, float TotalMod = 0.0f, uint32 stack = 1);
    uint32 SpellHealingBonusTaken(Unit* caster, SpellInfo const* spellProto, uint32 healamount, DamageEffectType damagetype, uint32 stack = 1);

    uint32 MeleeDamageBonusDone(Unit* pVictim, uint32 damage, WeaponAttackType attType, SpellInfo const* spellProto = nullptr, SpellSchoolMask damageSchoolMask = SPELL_SCHOOL_MASK_NORMAL);
    uint32 MeleeDamageBonusTaken(Unit* attacker, uint32 pdamage, WeaponAttackType attType, SpellInfo const* spellProto = nullptr, SpellSchoolMask damageSchoolMask = SPELL_SCHOOL_MASK_NORMAL);

    bool   isSpellBlocked(Unit* victim, SpellInfo const* spellProto, WeaponAttackType attackType = BASE_ATTACK);
    bool   isBlockCritical();
    float  SpellDoneCritChance(Unit const* /*victim*/, SpellInfo const* spellProto, SpellSchoolMask schoolMask, WeaponAttackType attackType, bool skipEffectCheck) const;
    float  SpellTakenCritChance(Unit const* caster, SpellInfo const* spellProto, SpellSchoolMask schoolMask, float doneChance, WeaponAttackType attackType, bool skipEffectCheck) const;
    static uint32 SpellCriticalDamageBonus(Unit const* caster, SpellInfo const* spellProto, uint32 damage, Unit const* victim);
    static uint32 SpellCriticalHealingBonus(Unit const* caster, SpellInfo const* spellProto, uint32 damage, Unit const* victim);

    void SetLastManaUse(uint32 spellCastTime) { m_lastManaUse = spellCastTime; }
    bool IsUnderLastManaUseEffect() const;

    void SetContestedPvP(Player* attackedPlayer = nullptr, bool lookForNearContestedGuards = true);

    uint32 GetCastingTimeForBonus(SpellInfo const* spellProto, DamageEffectType damagetype, uint32 CastingTime) const;
    float CalculateDefaultCoefficient(SpellInfo const* spellInfo, DamageEffectType damagetype) const;

    void CastDelayedSpellWithPeriodicAmount(Unit* caster, uint32 spellId, AuraType auraType, int32 addAmount, uint8 effectIndex = 0);

    void ApplySpellImmune(uint32 spellId, uint32 op, uint32 type, bool apply, SpellImmuneBlockType blockType = SPELL_BLOCK_TYPE_ALL);
    void ApplySpellDispelImmunity(SpellInfo const* spellProto, DispelType type, bool apply);
    virtual bool IsImmunedToSpell(SpellInfo const* spellInfo, Spell const* spell = nullptr);
    // redefined in Creature
    bool IsImmunedToDamage(SpellSchoolMask meleeSchoolMask) const;
    bool IsImmunedToDamage(SpellInfo const* spellInfo) const;
    bool IsImmunedToDamage(Spell const* spell) const;
    bool IsImmunedToSchool(SpellSchoolMask meleeSchoolMask) const;
    bool IsImmunedToSchool(SpellInfo const* spellInfo) const;
    bool IsImmunedToSchool(Spell const* spell) const;
    bool IsImmunedToDamageOrSchool(SpellSchoolMask meleeSchoolMask) const;
    bool IsImmunedToDamageOrSchool(SpellInfo const* spellInfo) const;
    virtual bool IsImmunedToSpellEffect(SpellInfo const* spellInfo, uint32 index) const;
    // redefined in Creature
    static bool IsDamageReducedByArmor(SpellSchoolMask damageSchoolMask, SpellInfo const* spellInfo = nullptr, uint8 effIndex = MAX_SPELL_EFFECTS);
    static uint32 CalcArmorReducedDamage(Unit const* attacker, Unit const* victim, const uint32 damage, SpellInfo const* spellInfo, uint8 attackerLevel = 0, WeaponAttackType attackType = MAX_ATTACK);
    static void CalcAbsorbResist(DamageInfo& dmgInfo, bool Splited = false);
    static void CalcHealAbsorb(HealInfo& healInfo);

    void  UpdateSpeed(UnitMoveType mtype, bool forced);
    float GetSpeed(UnitMoveType mtype) const;
    float GetSpeedRate(UnitMoveType mtype) const { return m_speed_rate[mtype]; }
    void SetSpeed(UnitMoveType mtype, float rate, bool forced = false);
    void SetSpeedRate(UnitMoveType mtype, float rate) { m_speed_rate[mtype] = rate; }

    float ApplyEffectModifiers(SpellInfo const* spellProto, uint8 effect_index, float value) const;
    int32 CalculateSpellDamage(Unit const* target, SpellInfo const* spellProto, uint8 effect_index, int32 const* basePoints = nullptr) const;
    int32 CalcSpellDuration(SpellInfo const* spellProto);
    int32 ModSpellDuration(SpellInfo const* spellProto, Unit const* target, int32 duration, bool positive, uint32 effectMask);
    void  ModSpellCastTime(SpellInfo const* spellProto, int32& castTime, Spell* spell = nullptr);
    float CalculateLevelPenalty(SpellInfo const* spellProto) const;

    void addFollower(FollowerReference* pRef) { m_FollowingRefMgr.insertFirst(pRef); }
    void removeFollower(FollowerReference* /*pRef*/) { /* nothing to do yet */ }

    MotionMaster* GetMotionMaster() { return i_motionMaster; }
    const MotionMaster* GetMotionMaster() const { return i_motionMaster; }
    virtual MovementGeneratorType GetDefaultMovementType() const;

    bool IsStopped() const { return !(HasUnitState(UNIT_STATE_MOVING)); }
    void StopMoving();
    void StopMovingOnCurrentPos();
    virtual void PauseMovement(uint32 timer = 0, uint8 slot = 0); // timer in ms
    void ResumeMovement(uint32 timer = 0, uint8 slot = 0);

    void AddUnitMovementFlag(uint32 f) { m_movement.m_moveFlags |= f; }
    void RemoveUnitMovementFlag(uint32 f) { m_movement.m_moveFlags &= ~f; }
    bool HasUnitMovementFlag(uint32 f) const { return (m_movement.m_moveFlags & f) == f; }
    uint32 GetUnitMovementFlags() const { return m_movement.m_moveFlags; }
    void SetUnitMovementFlags(uint32 f) { m_movement.m_moveFlags = f; }

    void AddExtraUnitMovementFlag(uint16 f) { m_movement.m_moveFlags2 |= f; }
    void RemoveExtraUnitMovementFlag(uint16 f) { m_movement.m_moveFlags2 &= ~f; }
    uint16 HasExtraUnitMovementFlag(uint16 f) const { return m_movement.m_moveFlags2 & f; }
    uint16 GetExtraUnitMovementFlags() const { return m_movement.m_moveFlags2; }
    void SetExtraUnitMovementFlags(uint16 f) { m_movement.m_moveFlags2 = f; }

    void SetControlled(bool apply, UnitState state, Unit* source = nullptr, bool isFear = false);
    void DisableRotate(bool apply);
    void DisableSpline();

    ///-----------Combo point system-------------------
       // This unit having CP on other units
    uint8 GetComboPoints(Unit const* who = nullptr) const { return (who && m_comboTarget != who) ? 0 : m_comboPoints; }
    uint8 GetComboPoints(WOWGUID const& guid) const { return (m_comboTarget && m_comboTarget->GetGUID() == guid) ? m_comboPoints : 0; }
    Unit* GetComboTarget() const { return m_comboTarget; }
    WOWGUID const GetComboTargetGUID() const { return m_comboTarget ? m_comboTarget->GetGUID() : WOWGUID::Empty; }
    void AddComboPoints(Unit* target, int8 count);
    void AddComboPoints(int8 count) { AddComboPoints(nullptr, count); }
    void ClearComboPoints();
    void SendComboPoints();
    // Other units having CP on this unit
    void AddComboPointHolder(Unit* unit) { m_ComboPointHolders.insert(unit); }
    void RemoveComboPointHolder(Unit* unit) { m_ComboPointHolders.erase(unit); }
    void ClearComboPointHolders();

    ///----------Pet responses methods-----------------
    void SendPetActionFeedback (uint8 msg);
    void SendPetTalk (uint32 pettalk);
    void SendPetAIReaction(WOWGUID guid);
    ///----------End of Pet responses methods----------

    void propagateSpeedChange() { GetMotionMaster()->propagateSpeedChange(); }

    // reactive attacks
    void ClearAllReactives();
    void StartReactiveTimer(ReactiveType reactive) { m_reactiveTimer[reactive] = REACTIVE_TIMER_START;}
    void UpdateReactives(uint32 p_time);

    // group updates
    void UpdateAuraForGroup(uint8 slot);

    // proc trigger system
    bool CanProc() {return !m_procDeep;}
    void SetCantProc(bool apply)
    {
        if (apply)
            ++m_procDeep;
        else
        {
            ASSERT(m_procDeep);
            --m_procDeep;
        }
    }

    // pet auras
    typedef std::set<PetAura const*> PetAuraSet;
    PetAuraSet m_petAuras;
    void AddPetAura(PetAura const* petSpell);
    void RemovePetAura(PetAura const* petSpell);
    void CastPetAura(PetAura const* aura);
    bool IsPetAura(Aura const* aura);

    uint32 GetModelForForm(ShapeshiftForm form, uint32 spellId) const;
    uint32 GetModelForTotem(PlayerTotemType totemType);

    // Redirect Threat
    void SetRedirectThreat(WOWGUID guid, uint32 pct) { _redirectThreatInfo.Set(guid, pct); }
    void ResetRedirectThreat() { SetRedirectThreat(WOWGUID::Empty, 0); }
    void ModifyRedirectThreat(int32 amount) { _redirectThreatInfo.ModifyThreatPct(amount); }
    uint32 GetRedirectThreatPercent() { return _redirectThreatInfo.GetThreatPct(); }
    Unit* GetRedirectThreatTarget() const;

    bool IsAIEnabled, NeedChangeAI;
    bool CreateVehicleKit(uint32 id, uint32 creatureEntry);
    void RemoveVehicleKit();
    Vehicle* GetVehicleKit()const { return m_vehicleKit; }
    Vehicle* GetVehicle()   const { return m_vehicle; }
    bool IsOnVehicle(Unit const* vehicle) const { return m_vehicle && m_vehicle == vehicle->GetVehicleKit(); }
    Unit* GetVehicleBase()  const;
    Creature* GetVehicleCreatureBase() const;
    WOWGUID GetTransGUID() const override;
    /// Returns the transport this unit is on directly (if on vehicle and transport, return vehicle)
    TransportBase* GetDirectTransport() const;

    bool m_ControlledByPlayer;
    bool m_CreatedByPlayer;

    bool HandleSpellClick(Unit* clicker, int8 seatId = -1);
    void EnterVehicle(Unit* base, int8 seatId = -1);
    void EnterVehicleUnattackable(Unit* base, int8 seatId = -1);
    void ExitVehicle(Position const* exitPosition = nullptr);
    void ChangeSeat(int8 seatId, bool next = true);

    // Should only be called by AuraEffect::HandleAuraControlVehicle(AuraApplication const* auraApp, uint8 mode, bool apply) const;
    void _ExitVehicle(Position const* exitPosition = nullptr);
    void _EnterVehicle(Vehicle* vehicle, int8 seatId, AuraApplication const* aurApp = nullptr);

    void BuildMovementPacket(ByteBuffer* data) const;

    virtual bool CanSwim() const;
    bool IsLevitating() const { return (m_movement.m_moveFlags & MOVEFLAG_DISABLE_GRAVITY) != 0; }
    bool IsWalking() const { return (m_movement.m_moveFlags & MOVEFLAG_WALK) != 0; }
    bool IsMoving() const { return (m_movement.m_moveFlags & MOVEFLAG_MOVE_MASK) != 0; }
    bool isTurning() const { return (m_movement.m_moveFlags & MOVEFLAG_TURN_MASK) != 0; }
    bool IsHovering() const { return (m_movement.m_moveFlags & MOVEFLAG_HOVER) != 0; }
    bool isSwimming() const { return (m_movement.m_moveFlags & MOVEFLAG_SWIMMING) != 0; }
    virtual bool CanFly() const = 0;
    bool IsFlying() const { return (m_movement.m_moveFlags & (MOVEFLAG_FLYING | MOVEFLAG_DISABLE_GRAVITY)) != 0; }
    bool IsFalling() const;
    float GetHoverHeight() const { return IsHovering() ? GetFloatValue(UNIT_FIELD_HOVERHEIGHT) : 0.0f; }
    virtual bool CanEnterWater() const = 0;

    void RewardRage(uint32 damage, uint32 weaponSpeedHitFactor, bool attacker);

    virtual float GetFollowAngle() const { return static_cast<float>(M_PI / 2); }

    void OutDebugInfo() const;
    virtual bool isBeingLoaded() const { return false;}
    bool IsDuringRemoveFromWorld() const {return m_duringRemoveFromWorld;}

    Pet* ToPet() { if (IsPet()) return reinterpret_cast<Pet*>(this); else return nullptr; }
    Totem* ToTotem() { if (IsTotem()) return reinterpret_cast<Totem*>(this); else return nullptr; }
    TempSummon* ToTempSummon() { if (IsSummon()) return reinterpret_cast<TempSummon*>(this); else return nullptr; }
    const TempSummon* ToTempSummon() const { if (IsSummon()) return reinterpret_cast<const TempSummon*>(this); else return nullptr; }

    // Safe mover
    std::set<SafeUnitPointer*> SafeUnitPointerSet;
    void AddPointedBy(SafeUnitPointer* sup) { SafeUnitPointerSet.insert(sup); }
    void RemovePointedBy(SafeUnitPointer* sup) { SafeUnitPointerSet.erase(sup); }
    static void HandleSafeUnitPointersOnDelete(Unit* thisUnit);
    // Relocation Nofier optimization
    Position m_last_notify_position;
    uint32 m_last_notify_mstime;
    uint16 m_delayed_unit_relocation_timer;
    uint16 m_delayed_unit_ai_notify_timer;
    bool bRequestForcedVisibilityUpdate;
    void ExecuteDelayedUnitRelocationEvent();
    void ExecuteDelayedUnitAINotifyEvent();

    // cooldowns
    virtual bool HasSpellCooldown(uint32 /*spell_id*/) const { return false; }
    virtual bool HasSpellItemCooldown(uint32 /*spell_id*/, uint32 /*itemid*/) const { return false; }
    virtual void AddSpellCooldown(uint32 /*spell_id*/, uint32 /*itemid*/, uint32 /*end_time*/, bool needSendToClient = false, bool forceSendToSpectator = false)
    {
        // workaround for unused parameters
        (void)needSendToClient;
        (void)forceSendToSpectator;
    }

    bool CanApplyResilience() const { return m_applyResilience; }

    void PetSpellFail(SpellInfo const* spellInfo, Unit* target, uint32 result);

    int32 CalculateAOEDamageReduction(int32 damage, uint32 schoolMask, bool npcCaster) const;

    WOWGUID GetTarget() const { return GetGuidValue(UNIT_FIELD_TARGET); }
    virtual void SetTarget(WOWGUID /*guid*/ = WOWGUID::Empty) = 0;

    void SetInstantCast(bool set) { _instantCast = set; }
    bool CanInstantCast() const { return _instantCast; }

    // Movement info
    Movement::MoveSpline* movespline;

    virtual void Talk(std::string_view text, ChatMsg msgType, Language language, float textRange, WorldObject const* target);
    virtual void Say(std::string_view text, Language language, WorldObject const* target = nullptr);
    virtual void Yell(std::string_view text, Language language, WorldObject const* target = nullptr);
    virtual void TextEmote(std::string_view text, WorldObject const* target = nullptr, bool isBossEmote = false);
    virtual void Whisper(std::string_view text, Language language, Player* target, bool isBossWhisper = false);
    virtual void Talk(uint32 textId, ChatMsg msgType, float textRange, WorldObject const* target);
    virtual void Say(uint32 textId, WorldObject const* target = nullptr);
    virtual void Yell(uint32 textId, WorldObject const* target = nullptr);
    virtual void TextEmote(uint32 textId, WorldObject const* target = nullptr, bool isBossEmote = false);
    virtual void Whisper(uint32 textId, Player* target, bool isBossWhisper = false);

    float GetCollisionHeight() const override;
    float GetCollisionWidth() const override;
    float GetCollisionRadius() const override;

    void ProcessPositionDataChanged(PositionFullTerrainStatus const& data) override;
    virtual void ProcessTerrainStatusUpdate();

    bool CanRestoreMana(SpellInfo const* spellInfo) const;

    std::string GetDebugInfo() const override;

    uint32 GetOldFactionId() const { return _oldFactionId; }

protected:
    explicit Unit (bool isWorldObject);

    void BuildValuesUpdate(uint8 updateType, ByteBuffer* data, Player* target) override;

    UnitAI* i_AI, *i_disabledAI;

    void _UpdateSpells(uint32 time);
    void _DeleteRemovedAuras();

    void _UpdateAutoRepeatSpell();

    uint8 m_realRace;
    uint8 m_race;

    bool m_AutoRepeatFirstCast;

    int32 m_attackTimer[MAX_ATTACK];

    float m_createStats[MAX_STATS];

    AttackerSet m_attackers;
    Unit* m_attacking;

    DeathState m_deathState;

    int32 m_procDeep;

    typedef std::list<DynamicObject*> DynObjectList;
    DynObjectList m_dynObj;

    typedef GuidList GameObjectList;
    GameObjectList m_gameObj;
    uint32 m_transform;

    Spell* m_currentSpells[CURRENT_MAX_SPELL];

    AuraMap m_ownedAuras;
    AuraApplicationMap m_appliedAuras;
    AuraList m_removedAuras;
    AuraMap::iterator m_auraUpdateIterator;
    uint32 m_removedAurasCount;

    AuraEffectList m_modAuras[TOTAL_AURAS];
    AuraList m_scAuras;                        // casted singlecast auras
    AuraApplicationList m_interruptableAuras;             // auras which have interrupt mask applied on unit
    AuraStateAurasMap m_auraStateAuras;        // Used for improve performance of aura state checks on aura apply/remove
    uint32 m_interruptMask;

    float m_auraModifiersGroup[UNIT_MOD_END][MODIFIER_TYPE_END];
    float m_weaponDamage[MAX_ATTACK][MAX_WEAPON_DAMAGE_RANGE][MAX_ITEM_PROTO_DAMAGES];
    bool m_canModifyStats;
    VisibleAuraMap m_visibleAuras;

    float m_speed_rate[MAX_MOVE_TYPE];

    CharmInfo* m_charmInfo;
    SharedVisionList m_sharedVision;

    MotionMaster* i_motionMaster;

    uint32 m_reactiveTimer[MAX_REACTIVE];
    int32 m_regenTimer;

    ThreatMgr m_ThreatMgr;
    typedef std::map<WOWGUID, float> CharmThreatMap;
    CharmThreatMap _charmThreatInfo;

    Vehicle* m_vehicle;
    Vehicle* m_vehicleKit;

    uint32 m_unitTypeMask;
    LiquidTypeEntry const* _lastLiquid;

    // xinef: apply resilience
    bool m_applyResilience;
    bool IsAlwaysVisibleFor(WorldObject const* seer) const override;
    bool IsAlwaysDetectableFor(WorldObject const* seer) const override;
    bool _instantCast;

private:
    bool IsTriggeredAtSpellProcEvent(Unit* victim, Aura* aura, WeaponAttackType attType, bool isVictim, bool active, SpellProcEventEntry const*& spellProcEvent, ProcEventInfo const& eventInfo);
    bool HandleDummyAuraProc(Unit* victim, uint32 damage, AuraEffect* triggeredByAura, SpellInfo const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown, ProcEventInfo const& eventInfo);
    bool HandleAuraProc(Unit* victim, uint32 damage, Aura* triggeredByAura, SpellInfo const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown, bool* handled);
    bool HandleProcTriggerSpell(Unit* victim, uint32 damage, AuraEffect* triggeredByAura, SpellInfo const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown, uint32 procPhase, ProcEventInfo& eventInfo);
    bool HandleOverrideClassScriptAuraProc(Unit* victim, uint32 damage, AuraEffect* triggeredByAura, SpellInfo const* procSpell, uint32 cooldown);
    bool HandleAuraRaidProcFromChargeWithValue(AuraEffect* triggeredByAura);
    bool HandleAuraRaidProcFromCharge(AuraEffect* triggeredByAura);

    void UpdateSplineMovement(uint32 t_diff);
    void UpdateSplinePosition();

    // player or player's pet
    float GetCombatRatingReduction(CombatRating cr) const;
    uint32 GetCombatRatingDamageReduction(CombatRating cr, float rate, float cap, uint32 damage) const;

    void PatchValuesUpdate(ByteBuffer& valuesUpdateBuf, BuildValuesCachePosPointers& posPointers, Player* target);
    void InvalidateValuesUpdateCache() { _valuesUpdateCache.clear(); }

protected:
    void SetFeared(bool apply, Unit* fearedBy = nullptr, bool isFear = false);
    void SetConfused(bool apply);
    void SetStunned(bool apply);
    void SetRooted(bool apply, bool isStun = false);

    uint32 m_rootTimes;

private:
    uint32 m_state;                                     // Even derived shouldn't modify
    uint32 m_CombatTimer;
    uint32 m_lastManaUse;                               // msecs
    //TimeTrackerSmall m_movesplineTimer;

    Diminishing m_Diminishing;
    // Manage all Units that are threatened by us
    HostileRefMgr m_HostileRefMgr;

    FollowerRefMgr m_FollowingRefMgr;

    Unit* m_comboTarget;
    int8 m_comboPoints;
    std::unordered_set<Unit*> m_ComboPointHolders;

    RedirectThreatInfo _redirectThreatInfo;

    bool m_cleanupDone; // lock made to not add stuff after cleanup before delete
    bool m_duringRemoveFromWorld; // lock made to not add stuff after begining removing from world

    uint32 _oldFactionId;           ///< faction before charm
    bool _isWalkingBeforeCharm;     ///< Are we walking before we were charmed?

    float processDummyAuras(float TakenTotalMod) const;

    uint32 _lastExtraAttackSpell;
    std::unordered_map<WOWGUID /*guid*/, uint32 /*count*/> extraAttacksTargets;
    WOWGUID _lastDamagedTargetGuid;

    typedef std::unordered_map<uint64 /*visibleFlag(uint32) + updateType(uint8)*/, BuildValuesCachedBuffer>  ValuesUpdateCache;
    ValuesUpdateCache _valuesUpdateCache;
};

namespace Acore
{
    // Binary predicate for sorting Units based on percent value of a power
    class PowerPctOrderPred
    {
    public:
        PowerPctOrderPred(POWER_TYPE power, bool ascending = true) : _power(power), _ascending(ascending) { }

        bool operator()(WorldObject const* objA, WorldObject const* objB) const
        {
            Unit const* a = objA->ToUnit();
            Unit const* b = objB->ToUnit();
            float rA = (a && a->GetMaxPower(_power)) ? float(a->GetPower(_power)) / float(a->GetMaxPower(_power)) : 0.0f;
            float rB = (b && b->GetMaxPower(_power)) ? float(b->GetPower(_power)) / float(b->GetMaxPower(_power)) : 0.0f;
            return _ascending ? rA < rB : rA > rB;
        }

        bool operator()(Unit const* a, Unit const* b) const
        {
            float rA = a->GetMaxPower(_power) ? float(a->GetPower(_power)) / float(a->GetMaxPower(_power)) : 0.0f;
            float rB = b->GetMaxPower(_power) ? float(b->GetPower(_power)) / float(b->GetMaxPower(_power)) : 0.0f;
            return _ascending ? rA < rB : rA > rB;
        }

    private:
        POWER_TYPE const _power;
        bool const _ascending;
    };

    // Binary predicate for sorting Units based on percent value of health
    class HealthPctOrderPred
    {
    public:
        HealthPctOrderPred(bool ascending = true) : _ascending(ascending) { }

        bool operator()(WorldObject const* objA, WorldObject const* objB) const
        {
            Unit const* a = objA->ToUnit();
            Unit const* b = objB->ToUnit();
            float rA = (a && a->GetMaxHealth()) ? float(a->GetHealth()) / float(a->GetMaxHealth()) : 0.0f;
            float rB = (b && b->GetMaxHealth()) ? float(b->GetHealth()) / float(b->GetMaxHealth()) : 0.0f;
            return _ascending ? rA < rB : rA > rB;
        }

        bool operator() (Unit const* a, Unit const* b) const
        {
            float rA = a->GetMaxHealth() ? float(a->GetHealth()) / float(a->GetMaxHealth()) : 0.0f;
            float rB = b->GetMaxHealth() ? float(b->GetHealth()) / float(b->GetMaxHealth()) : 0.0f;
            return _ascending ? rA < rB : rA > rB;
        }

    private:
        bool const _ascending;
    };
}

class RedirectSpellEvent : public BasicEvent
{
public:
    RedirectSpellEvent(Unit& self, WOWGUID auraOwnerGUID, AuraEffect* auraEffect) : _self(self), _auraOwnerGUID(auraOwnerGUID), _auraEffect(auraEffect) { }
    bool Execute(uint64 e_time, uint32 p_time) override;

protected:
    Unit& _self;
    WOWGUID _auraOwnerGUID;
    AuraEffect* _auraEffect;
};

#endif
