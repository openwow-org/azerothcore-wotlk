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

#ifndef _ARENA_SCORE_H
#define _ARENA_SCORE_H

#include "BattlegroundScore.h"
#include "StringFormat.h"

struct AC_GAME_API ArenaScore : public BattlegroundScore
{
    friend class Arena;

protected:
    ArenaScore(WOWGUID playerGuid, TeamId team) :
        BattlegroundScore(playerGuid), PvPTeamId(team == TEAM_ALLIANCE ? PVP_TEAM_ALLIANCE : PVP_TEAM_HORDE) { }

    void AppendToPacket(WDataStore& data) final;
    void BuildObjectivesBlock(WDataStore& data) final;

    // For Logging purpose
    std::string ToString() const override
    {
        return Acore::StringFormatFmt("Damage done: {}, Healing done: {}, Killing blows: {}", DamageDone, HealingDone, KillingBlows);
    }

    uint8 PvPTeamId;
};

struct AC_GAME_API ArenaTeamScore
{
    friend class Arena;
    friend class Battleground;

protected:
    ArenaTeamScore() = default;
    virtual ~ArenaTeamScore() = default;

    void Reset()
    {
        RatingChange = 0;
        MatchmakerRating = 0;
        TeamName = {};
    }

    void Assign(int32 ratingChange, uint32 matchMakerRating, std::string_view teamName)
    {
        RatingChange = ratingChange;
        MatchmakerRating = matchMakerRating;
        TeamName = std::string(teamName);
    }

    void BuildRatingInfoBlock(WDataStore& data);
    void BuildTeamInfoBlock(WDataStore& data);

    int32 RatingChange = 0;
    uint32 MatchmakerRating = 0;
    std::string TeamName{};
};

#endif // ACORE_ARENA_SCORE_H
