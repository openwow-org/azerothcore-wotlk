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

#ifndef OUTDOOR_PVP_TF_
#define OUTDOOR_PVP_TF_

#include "OutdoorPvP.h"

const uint8 OutdoorPvPTFBuffZonesNum = 5;

const uint32 OutdoorPvPTFBuffZones[OutdoorPvPTFBuffZonesNum] =
{
    3519 /*Terokkar Forest*/,
    3791 /*Sethekk Halls*/,
    3789 /*Shadow Labyrinth*/,
    3792 /*Mana-Tombs*/,
    3790 /*Auchenai Crypts*/
};

// locked for 6 hours after capture
const uint32 TF_LOCK_TIME = 6 * HOUR * IN_MILLISECONDS;

// update lock timer every 1/4 minute (overkill, but this way it's sure the timer won't "jump" 2 minutes at once.)
const uint32 TF_LOCK_TIME_UPDATE = 15 * IN_MILLISECONDS;

// blessing of auchindoun
#define TF_CAPTURE_BUFF 33377

const uint32 TF_ALLY_QUEST = 11505;
const uint32 TF_HORDE_QUEST = 11506;

enum OutdoorPvPTF_TowerType
{
    TF_TOWER_NW = 0,
    TF_TOWER_N,
    TF_TOWER_NE,
    TF_TOWER_SE,
    TF_TOWER_S,
    TF_TOWER_NUM
};

const go_type TFCapturePoints[TF_TOWER_NUM] =
{
    {183104, 530, -3081.65f, 5335.03f, 17.1853f, -2.14675f, 0.0f, 0.0f, 0.878817f, -0.477159f},
    {183411, 530, -2939.9f, 4788.73f, 18.987f, 2.77507f, 0.0f, 0.0f, 0.983255f, 0.182236f},
    {183412, 530, -3174.94f, 4440.97f, 16.2281f, 1.86750f, 0.0f, 0.0f, 0.803857f, 0.594823f},
    {183413, 530, -3603.31f, 4529.15f, 20.9077f, 0.994838f, 0.0f, 0.0f, 0.477159f, 0.878817f},
    {183414, 530, -3812.37f, 4899.3f, 17.7249f, 0.087266f, 0.0f, 0.0f, 0.043619f, 0.999048f}
};

struct tf_tower_world_state
{
    uint32 n;
    uint32 h;
    uint32 a;
};

const tf_tower_world_state TFTowerWorldStates[TF_TOWER_NUM] =
{
    {0xa79, 0xa7a, 0xa7b},
    {0xa7e, 0xa7d, 0xa7c},
    {0xa82, 0xa81, 0xa80},
    {0xa88, 0xa87, 0xa86},
    {0xa85, 0xa84, 0xa83}
};

//const uint32 TFTowerPlayerEnterEvents[TF_TOWER_NUM] =
//{
//    12226,
//    12497,
//    12486,
//    12499,
//    12501
//};

//const uint32 TFTowerPlayerLeaveEvents[TF_TOWER_NUM] =
//{
//    12225,
//    12496,
//    12487,
//    12498,
//    12500
//};

enum TFWorldStates
{
    TF_UI_TOWER_SLIDER_POS = 0xa41,
    TF_UI_TOWER_SLIDER_N = 0xa40,
    TF_UI_TOWER_SLIDER_DISPLAY = 0xa3f,

    TF_UI_TOWER_COUNT_H = 0xa3e,
    TF_UI_TOWER_COUNT_A = 0xa3d,
    TF_UI_TOWERS_CONTROLLED_DISPLAY = 0xa3c,

    TF_UI_LOCKED_TIME_MINUTES_FIRST_DIGIT = 0x9d0,
    TF_UI_LOCKED_TIME_MINUTES_SECOND_DIGIT = 0x9ce,
    TF_UI_LOCKED_TIME_HOURS = 0x9cd,
    TF_UI_LOCKED_DISPLAY_NEUTRAL = 0x9cc,
    TF_UI_LOCKED_DISPLAY_HORDE = 0xad0,
    TF_UI_LOCKED_DISPLAY_ALLIANCE = 0xacf
};

enum TFTowerStates
{
    TF_TOWERSTATE_N = 1,
    TF_TOWERSTATE_H = 2,
    TF_TOWERSTATE_A = 4
};

class OPvPCapturePointTF : public OPvPCapturePoint
{
public:
    OPvPCapturePointTF(OutdoorPvP* pvp, OutdoorPvPTF_TowerType type);

    bool Update(uint32 diff) override;

    void ChangeState() override;

    void SendChangePhase() override;

    void FillInitialWorldStates(WDataStore& data) override;

    // used when player is activated/inactivated in the area
    bool HandlePlayerEnter(Player* player) override;
    void HandlePlayerLeave(Player* player) override;

    void ResetToTeamControlled(TeamId team);

    void UpdateTowerState();

protected:
    OutdoorPvPTF_TowerType m_TowerType;

    uint32 m_TowerState;
};

class OutdoorPvPTF : public OutdoorPvP
{
public:
    OutdoorPvPTF();

    bool SetupOutdoorPvP() override;

    void HandlePlayerEnterZone(Player* player, uint32 zone) override;
    void HandlePlayerLeaveZone(Player* player, uint32 zone) override;

    bool Update(uint32 diff) override;

    void FillInitialWorldStates(WDataStore& data) override;

    void SendRemoveWorldStates(Player* player) override;

    void SaveRequiredWorldStates() const;

    void ResetZoneToTeamControlled(TeamId team);

    void RecalculateClientUILockTime()
    {
        uint32 minutes_left = m_LockTimer / 60000;
        hours_left = minutes_left / 60;
        minutes_left -= hours_left * 60;
        second_digit = minutes_left % 10;
        first_digit = minutes_left / 10;
    }

    uint32 GetAllianceTowersControlled() const;
    void SetAllianceTowersControlled(uint32 count);

    uint32 GetHordeTowersControlled() const;
    void SetHordeTowersControlled(uint32 count);

    bool IsLocked() const;

private:
    bool m_IsLocked;
    bool m_JustLocked;
    uint32 m_LockTimer;
    uint32 m_LockTimerUpdate;

    uint32 m_AllianceTowersControlled;
    uint32 m_HordeTowersControlled;

    uint32 hours_left, second_digit, first_digit;
};

#endif
