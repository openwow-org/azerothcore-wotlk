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

/** \file
    \ingroup u2w
*/

#include "User.h"

#include <WowServices/AccountInfo.h>
#include "AccountMgr.h"
#include "BattlegroundMgr.h"
#include "CharacterPackets.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Hyperlinks.h"
#include "Language.h"
#include "Log.h"
#include "MapMgr.h"
#include "Metric.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "OutdoorPvPMgr.h"
#include "PacketUtilities.h"
#include "Pet.h"
#include "Player.h"
#include "QueryHolder.h"
#include "ScriptMgr.h"
#include "Transport.h"
#include "Vehicle.h"
#include "WardenWin.h"
#include "World.h"
#include "WDataStore.h"
#include "WowConnection.h"
#include <zlib.h>

#include "BanMgr.h"
#include "WowConnectionNet.h"
#define CAMP_TIME_SECONDS 20  // The time it takes for a character to log out

static bool s_initialized;

static void UserBootMeHandler (User*        user,
                               NETMESSAGE   msgId,
                               uint         eventTime,
                               WDataStore*  msg);
static void UserBugReportHandler (User*       user,
                                  NETMESSAGE  msgId,
                                  uint        eventTime,
                                  WDataStore* msg);
static void UserGmResurrectHandler (User*       user,
                                    NETMESSAGE  msgId,
                                    uint        eventTime,
                                    WDataStore* msg);
static void UserWorldTeleportHandler (User*       user,
                                      NETMESSAGE  msgId,
                                      uint        eventTime,
                                      WDataStore* msg);

namespace
{
    std::string const DefaultPlayerName = "<none>";
}

bool MapSessionFilter::Process(WDataStore* packet)
{
    ClientOpcodeHandler const* opHandle = opcodeTable[static_cast<OpcodeClient>(packet->GetOpcode())];
    if (!opHandle) {
        // New msg processing code does not care about this bullshit!
        // Nuke this code at some point in the future...
        return true;
    }

    //let's check if our has an anxiety disorder can be really processed in Map::Update()
    if (opHandle->ProcessingPlace == PROCESS_INPLACE)
        return true;

    //we do not process thread-unsafe packets
    if (opHandle->ProcessingPlace == PROCESS_THREADUNSAFE)
        return false;

    Player* player = m_pSession->GetPlayer();
    if (!player)
        return false;

    //in Map::Update() we do not process packets where player is not in world!
    return player->IsInWorld();
}

//we should process ALL packets when player is not in world/logged in
//OR packet handler is not thread-safe!
bool WorldSessionFilter::Process(WDataStore* packet)
{
    ClientOpcodeHandler const* opHandle = opcodeTable[static_cast<OpcodeClient>(packet->GetOpcode())];
    if (!opHandle) {
      // New msg processing code does not care about this bullshit!
      // Nuke this code at some point in the future...
      return true;
    }

    //check if packet handler is supposed to be safe
    if (opHandle->ProcessingPlace == PROCESS_INPLACE)
        return true;

    //thread-unsafe packets should be processed in World::UpdateUsers()
    if (opHandle->ProcessingPlace == PROCESS_THREADUNSAFE)
        return true;

    //no player attached? -> our client! ^^
    Player* player = m_pSession->GetPlayer();
    if (!player)
        return true;

    //lets process all packets for non-in-the-world player
    return !player->IsInWorld();
}

//=============================================================================
Player* User::ActivePlayer() const
{
    if (!m_player || !m_player->IsInWorld())
        return nullptr;
    // Return the active Player object for the current session
    return m_player;
}

//=============================================================================
void User::AddFriend (char const* name, char const* notes) {
  ASSERT(name);
  ASSERT(notes);

  // Look for a character profile matching the name provided
  if (auto charInfo = sCharacterCache->GetCharacterCacheByName(name)) {
    FriendList::Friend frnd;
    frnd.m_flags = CONTACT_FRIEND;
    frnd.m_GUID = charInfo->Guid;
    frnd.SetName(name);
    frnd.SetNotes(notes);

    // Fill in the player info for the friend if they are in-game
    if (Player* plrFriend = ObjectAccessor::FindPlayer(charInfo->Guid)) {
      frnd.m_status = FRIEND_STATUS_ONLINE;
      if (plrFriend->isAFK())
        frnd.m_status = FRIEND_STATUS_AFK;
      if (plrFriend->isDND())
        frnd.m_status = FRIEND_STATUS_DND;
      frnd.m_areaId = plrFriend->GetAreaId();
      frnd.m_level = plrFriend->GetLevel();
      frnd.m_classId = plrFriend->GetClass();
    }

    // Add the friend
    FRIEND_RESULT res = FriendList()->AddFriend(frnd);
    SendFriendStatus(res, charInfo->Guid);
  }
  else
    SendFriendStatus(FRIEND_NOT_FOUND, WOWGUID());
}

//=============================================================================
void User::AddIgnore (char const* name) {
  auto charInfo = sCharacterCache->GetCharacterCacheByName(name);
  if (!charInfo) {
    SendFriendStatus(FRIEND_IGNORE_NOT_FOUND, WOWGUID());
    return;
  }
  FRIEND_RESULT res = FriendList()->AddIgnore(charInfo->Guid);
  SendFriendStatus(res, charInfo->Guid);
}

//=============================================================================
void User::DelIgnore (WOWGUID& guid) {
  FRIEND_RESULT res = FriendList()->DelIgnore(guid);
  SendFriendStatus(res, guid);
}

//=============================================================================
FriendList* User::FriendList () const {
  return m_player->FriendList();
}

/// User constructor
User::User(uint32 id, uint32_t accountFlags, std::string&& name, std::shared_ptr<WowConnection> sock, AccountTypes sec, uint8 expansion,
    time_t mute_time, LocaleConstant locale, uint32 recruiter, bool isARecruiter, bool skipQueue, uint32 TotalTime) :
    m_muteTime(mute_time),
    m_timeOutTime(0),
    _lastAuctionListItemsMSTime(0),
    _lastAuctionListOwnerItemsMSTime(0),
    AntiDOS(this),
    m_GUIDLow(0),
    m_player(nullptr),
    m_sock(sock),
    _security(sec),
    _skipQueue(skipQueue),
    _accountId(id),
    m_accountFlags(accountFlags),
    m_accountName(std::move(name)),
    m_expansion(expansion),
    m_total_time(TotalTime),
    m_logoutRequestTime(0),
    m_inQueue(false),
    m_playerLoading(false),
    m_loggingOut(false),
    m_playerRecentlyLogout(false),
    m_playerSave(false),
    m_sessionDbcLocale(sWorld->GetDefaultDbcLocale()),
    m_sessionDbLocaleIndex(locale),
    m_latency(0),
    m_TutorialsChanged(false),
    recruiterId(recruiter),
    isRecruiter(isARecruiter),
    m_currentVendorEntry(0),
    _calendarEventCreationCooldown(0),
    _addonMessageReceiveCount(0),
    _timeSyncClockDeltaQueue(6),
    _timeSyncClockDelta(0),
    _pendingTimeSyncRequests()
{
    memset(m_Tutorials, 0, sizeof(m_Tutorials));

    _offlineTime = 0;
    _kicked = false;

    _timeSyncNextCounter = 0;
    _timeSyncTimer = 0;

    if (sock)
    {
        m_Address = sock->GetRemoteIpAddress().to_string();
        ResetTimeOutTime(false);
        LoginDatabase.Execute("UPDATE account SET online = 1 WHERE id = {};", GetAccountId()); // One-time query
    }
}

/// User destructor
User::~User()
{
    LoginDatabase.Execute("UPDATE account SET totaltime = {} WHERE id = {}", GetTotalTime(), GetAccountId());

    ///- unload player if not unloaded
    if (m_player)
        CharacterRemoveFromGame(true);

    /// - If have unclosed socket, close it
    if (m_sock)
    {
        m_sock->Disconnect();
        m_sock = nullptr;
    }

    ///- empty incoming packet queue
    WDataStore* packet = nullptr;
    while (_recvQueue.next(packet))
        delete packet;

    LoginDatabase.Execute("UPDATE account SET online = 0 WHERE id = {};", GetAccountId());     // One-time query
}

//=============================================================================
void User::CharacterAbortLogout () {
  if (Player* plr = ActivePlayer()) {
    this->m_loggingOut = false;

    SendLogoutCancelAckMessage();

    // The player was locked during the camping timeout in CharacterLogout,
    // so allow it to move freely again.
    plr->SetMovement(MOVE_UNROOT);
    plr->RemoveUnitFlag(UNIT_FLAG_STUNNED);
  }
}

//=============================================================================
void User::CharacterLogout (bool instant) {
  if (Player* plr = ActivePlayer()) {
    this->m_loggingOut = true;
    if (instant)
      CharacterRemoveFromGame(true);
    else {
      // Start the logout timer
      this->m_logoutRequestTime = time(nullptr);

      // Force the player to sit
      if (plr->GetStandState() == UNIT_STANDING)
        plr->SetStandState(UNIT_SITTING);

      // Root and stun the player to prevent control during logout
      plr->SetMovement(MOVE_ROOT);
      plr->SetUnitFlag(UNIT_FLAG_STUNNED);
    }
  }
}

//=============================================================================
char const* User::GetAccountName()
{
    return m_accountName.c_str();
}

//=============================================================================
bool User::IsGMAccount () const {
    return (m_accountFlags & static_cast<uint>(AccountFlag::FLAG_GM)) != 0;
}

std::string const& User::GetPlayerName() const
{
    return m_player ? m_player->GetName() : DefaultPlayerName;
}

std::string User::GetPlayerInfo() const
{
    std::ostringstream ss;

    ss << "[Player: ";

    if (!m_playerLoading && m_player)
    {
        ss << m_player->GetName() << ' ' << m_player->GetGUID().ToString() << ", ";
    }

    ss << "Account: " << GetAccountId() << "]";

    return ss.str();
}

/// Get player guid if available. Use for logging purposes only
WOWGUID::LowType User::GetGuidLow() const
{
    return GetPlayer() ? GetPlayer()->GetGUID().GetCounter() : 0;
}

//=============================================================================
void User::SendLogoutCancelAckMessage () {
  WDataStore msg(SMSG_LOGOUT_CANCEL_ACK, 0);
  Send(&msg);
}

//=============================================================================
void User::SendLogoutCompleteMessage () {
  WDataStore msg(SMSG_LOGOUT_COMPLETE, 0);
  Send(&msg);
}

//=============================================================================
void User::SendLogoutResponse (LogoutResponse& res) {
  WDataStore msg(SMSG_LOGOUT_RESPONSE, sizeof(res));
  msg << res.logoutFailed;
  msg << res.instantLogout;
  Send(&msg);
}


/// Send a packet to the client
void User::Send(WDataStore const* packet)
{
    if (!m_sock)
        return;

#if defined(ACORE_DEBUG)
    // Code for network use statistic
    static uint64 sendPacketCount = 0;
    static uint64 sendPacketBytes = 0;

    static time_t firstTime = GameTime::GetGameTime().count();
    static time_t lastTime = firstTime;                     // next 60 secs start time

    static uint64 sendLastPacketCount = 0;
    static uint64 sendLastPacketBytes = 0;

    time_t cur_time = GameTime::GetGameTime().count();

    if ((cur_time - lastTime) < 60)
    {
        sendPacketCount += 1;
        sendPacketBytes += packet->size();

        sendLastPacketCount += 1;
        sendLastPacketBytes += packet->size();
    }
    else
    {
        uint64 minTime = uint64(cur_time - lastTime);
        uint64 fullTime = uint64(lastTime - firstTime);

        LOG_DEBUG("network", "Send all time packets count: {} bytes: {} avr.count/sec: {} avr.bytes/sec: {} time: {}", sendPacketCount, sendPacketBytes, float(sendPacketCount) / fullTime, float(sendPacketBytes) / fullTime, uint32(fullTime));

        LOG_DEBUG("network", "Send last min packets count: {} bytes: {} avr.count/sec: {} avr.bytes/sec: {}", sendLastPacketCount, sendLastPacketBytes, float(sendLastPacketCount) / minTime, float(sendLastPacketBytes) / minTime);

        lastTime = cur_time;
        sendLastPacketCount = 1;
        sendLastPacketBytes = packet->wpos();               // wpos is real written size
    }
#endif                                                      // !ACORE_DEBUG

    if (!sScriptMgr->CanPacketSend(this, *packet))
    {
        return;
    }

    m_sock->SendPacket(*packet);
}

/// Add an incoming packet to the queue
void User::QueuePacket(WDataStore* new_packet)
{
    _recvQueue.add(new_packet);
}

/// Logging helper for unexpected opcodes
void User::LogUnexpectedOpcode(WDataStore* packet, char const* status, const char* reason)
{
    LOG_ERROR("network.opcode", "Received unexpected opcode {} Status: {} Reason: {} from {}",
        GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())), status, reason, GetPlayerInfo());
}

/// Logging helper for unexpected opcodes
void User::LogUnprocessedTail(WDataStore* packet)
{
    if (!sLog->ShouldLog("network.opcode", LogLevel::LOG_LEVEL_TRACE) || packet->rpos() >= packet->wpos())
        return;

    LOG_TRACE("network.opcode", "Unprocessed tail data (read stop at {} from {}) Opcode {} from {}",
        uint32(packet->rpos()), uint32(packet->wpos()), GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())), GetPlayerInfo());

    packet->print_storage();
}

/// Update the User (triggered by World update)
bool User::Update(uint32 diff, PacketFilter& updater)
{
    ///- Before we process anything:
    /// If necessary, kick the player because the client didn't send anything for too long
    /// (or they've been idling in character select)
    if (sWorld->getBoolConfig(CONFIG_CLOSE_IDLE_CONNECTIONS) && IsConnectionIdle() && m_sock)
        m_sock->Disconnect();

    if (updater.ProcessUnsafe())
        UpdateTimeOutTime(diff);

    HandleTeleportTimeout(updater.ProcessUnsafe());

    ///- Retrieve packets from the receive queue and call the appropriate handlers
    /// not process packets if socket already closed
    WDataStore* packet = nullptr;

    //! Delete packet after processing by default
    bool deletePacket = true;
    std::vector<WDataStore*> requeuePackets;
    uint32 processedPackets = 0;
    time_t currentTime = GameTime::GetGameTime().count();

    constexpr uint32 MAX_PROCESSED_PACKETS_IN_SAME_WORLDSESSION_UPDATE = 150;

    while (m_sock && _recvQueue.next(packet, updater))
    {
        // New msg system:
        if (WowConnection::m_handlers.contains(static_cast<NETMESSAGE>(packet->GetOpcode()))) {
            auto requiredPermission = WowConnection::m_handlerPermissions.at(static_cast<NETMESSAGE>(packet->GetOpcode()));
            if ((ActivePlayer() && ActivePlayer()->GetSecurityGroup() < requiredPermission) ||
                (!ActivePlayer() && requiredPermission > DEFAULT_SECURITY && !IsGMAccount())) {
                SendNotification(LANG_PERMISSION_DENIED);
            } else
                WowConnection::m_handlers[static_cast<NETMESSAGE>(packet->GetOpcode())](this, static_cast<NETMESSAGE>(packet->GetOpcode()), currentTime, packet);
        }
        else {  // TODO: NUKE ALL THIS GARBAGE
            OpcodeClient opcode = static_cast<OpcodeClient>(packet->GetOpcode());
            ClientOpcodeHandler const* opHandle = opcodeTable[opcode];

            METRIC_DETAILED_TIMER("worldsession_update_opcode_time", METRIC_TAG("opcode", opHandle->Name));
            LOG_DEBUG("network", "message id {} ({}) under READ", opcode, opHandle->Name);

            try
            {
                switch (opHandle->Status)
                {
                case STATUS_LOGGEDIN:
                    if (!m_player)
                    {
                        // skip STATUS_LOGGEDIN opcode unexpected errors if player logout sometime ago - this can be network lag delayed packets
                        //! If player didn't log out a while ago, it means packets are being sent while the server does not recognize
                        //! the client to be in world yet. We will re-add the packets to the bottom of the queue and process them later.
                        if (!m_playerRecentlyLogout)
                        {
                            requeuePackets.push_back(packet);
                            deletePacket = false;

                            LOG_DEBUG("network", "Delaying processing of message with status STATUS_LOGGEDIN: No players in the world for account id {}", GetAccountId());
                        }
                    }
                    else if (m_player->IsInWorld())
                    {
                        if (AntiDOS.EvaluateOpcode(*packet, currentTime))
                        {
                            if (!sScriptMgr->CanPacketReceive(this, *packet))
                            {
                                break;
                            }

                            opHandle->Call(this, *packet);
                            LogUnprocessedTail(packet);
                        }
                        else
                            processedPackets = MAX_PROCESSED_PACKETS_IN_SAME_WORLDSESSION_UPDATE;   // break out of packet processing loop
                    }

                    // lag can cause STATUS_LOGGEDIN opcodes to arrive after the player started a transfer
                    break;
                case STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT:
                    if (!m_player && !m_playerRecentlyLogout) // There's a short delay between m_player = null and m_playerRecentlyLogout = true during logout
                    {
                        LogUnexpectedOpcode(packet, "STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT",
                                "the player has not logged in yet and not recently logout");
                    }
                    else if (AntiDOS.EvaluateOpcode(*packet, currentTime))
                    {
                        // not expected m_player or must checked in packet hanlder
                        if (!sScriptMgr->CanPacketReceive(this, *packet))
                            break;

                        opHandle->Call(this, *packet);
                        LogUnprocessedTail(packet);
                    }
                    else
                        processedPackets = MAX_PROCESSED_PACKETS_IN_SAME_WORLDSESSION_UPDATE;   // break out of packet processing loop
                    break;
                case STATUS_TRANSFER:
                    if (m_player && !m_player->IsInWorld() && AntiDOS.EvaluateOpcode(*packet, currentTime))
                    {
                        if (!sScriptMgr->CanPacketReceive(this, *packet))
                        {
                            break;
                        }

                        opHandle->Call(this, *packet);
                        LogUnprocessedTail(packet);
                    }
                    else
                        processedPackets = MAX_PROCESSED_PACKETS_IN_SAME_WORLDSESSION_UPDATE;   // break out of packet processing loop
                    break;
                case STATUS_AUTHED:
                    if (m_inQueue) // prevent cheating
                        break;

                    // some auth opcodes can be recieved before STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT opcodes
                    // however when we recieve CMSG_CHAR_ENUM we are surely no longer during the logout process.
                    if (packet->GetOpcode() == CMSG_CHAR_ENUM)
                        m_playerRecentlyLogout = false;

                    if (AntiDOS.EvaluateOpcode(*packet, currentTime))
                    {
                        if (!sScriptMgr->CanPacketReceive(this, *packet))
                        {
                            break;
                        }

                        opHandle->Call(this, *packet);
                        LogUnprocessedTail(packet);
                    }
                    else
                        processedPackets = MAX_PROCESSED_PACKETS_IN_SAME_WORLDSESSION_UPDATE;   // break out of packet processing loop
                    break;
                case STATUS_NEVER:
                    LOG_ERROR("network.opcode", "Received not allowed opcode {} from {}",
                        GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())), GetPlayerInfo());
                    break;
                case STATUS_UNHANDLED:
                    LOG_DEBUG("network.opcode", "Received not handled opcode {} from {}",
                        GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())), GetPlayerInfo());
                    break;
                }
            }
            catch (WorldPackets::InvalidHyperlinkException const& ihe)
            {
                LOG_ERROR("network", "{} sent {} with an invalid link:\n{}", GetPlayerInfo(),
                    GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())), ihe.GetInvalidValue());

                if (sWorld->getIntConfig(CONFIG_CHAT_STRICT_LINK_CHECKING_KICK))
                {
                    KickPlayer("User::Update Invalid chat link");
                }
            }
            catch (WorldPackets::IllegalHyperlinkException const& ihe)
            {
                LOG_ERROR("network", "{} sent {} which illegally contained a hyperlink:\n{}", GetPlayerInfo(),
                    GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())), ihe.GetInvalidValue());

                if (sWorld->getIntConfig(CONFIG_CHAT_STRICT_LINK_CHECKING_KICK))
                {
                    KickPlayer("User::Update Illegal chat link");
                }
            }
            catch (WorldPackets::PacketArrayMaxCapacityException const& pamce)
            {
                LOG_ERROR("network", "PacketArrayMaxCapacityException: {} while parsing {} from {}.",
                    pamce.what(), GetOpcodeNameForLogging(static_cast<OpcodeClient>(packet->GetOpcode())), GetPlayerInfo());
            }
            catch (ByteBufferException const&)
            {
                LOG_ERROR("network", "User::Update ByteBufferException occured while parsing a packet (opcode: {}) from client {}, accountid={}. Skipped packet.", packet->GetOpcode(), GetRemoteAddress(), GetAccountId());
                if (sLog->ShouldLog("network", LogLevel::LOG_LEVEL_DEBUG))
                {
                    LOG_DEBUG("network", "Dumping error causing packet:");
                    packet->hexlike();
                }
            }
        }

        if (deletePacket)
            delete packet;

        deletePacket = true;

        processedPackets++;

        //process only a max amout of packets in 1 Update() call.
        //Any leftover will be processed in next update
        if (processedPackets > MAX_PROCESSED_PACKETS_IN_SAME_WORLDSESSION_UPDATE)
            break;
    }

    _recvQueue.readd(requeuePackets.begin(), requeuePackets.end());

    METRIC_VALUE("processed_packets", processedPackets);
    METRIC_VALUE("addon_messages", _addonMessageReceiveCount.load());
    _addonMessageReceiveCount = 0;

    if (!updater.ProcessUnsafe()) // <=> updater is of type MapSessionFilter
    {
        // Send time sync packet every 10s.
        if (_timeSyncTimer > 0)
        {
            if (diff >= _timeSyncTimer)
            {
                SendTimeSync();
            }
            else
            {
                _timeSyncTimer -= diff;
            }
        }
    }

    ProcessQueryCallbacks();

    //check if we are safe to proceed with logout
    //logout procedure should happen only in World::UpdateUsers() method!!!
    if (updater.ProcessUnsafe())
    {
        if (m_sock && m_sock->IsOpen() && _warden)
        {
            _warden->Update(diff);
        }

        if (currentTime >= (m_logoutRequestTime+CAMP_TIME_SECONDS) && m_loggingOut)
        {
            CharacterRemoveFromGame(true);
        }

        if (m_sock && !m_sock->IsOpen())
        {
            if (GetPlayer() && _warden)
                _warden->Update(diff);

            m_sock = nullptr;
        }

        if (!m_sock)
        {
            return false;                                       //Will remove this session from the world session map
        }
    }

    return true;
}

bool User::HandleSocketClosed()
{
    if (m_sock && !m_sock->IsOpen() && !IsKicked() && GetPlayer() && !CharacterLoggingOut() && GetPlayer()->m_taxi.empty() && GetPlayer()->IsInWorld() && !World::IsStopped())
    {
        m_sock = nullptr;
        GetPlayer()->TradeCancel(false);
        return true;
    }

    return false;
}

bool User::IsSocketClosed() const
{
    return !m_sock || !m_sock->IsOpen();
}

void User::HandleTeleportTimeout(bool updateInSessions)
{
    // pussywizard: handle teleport ack timeout
    if (m_sock && m_sock->IsOpen() && GetPlayer() && GetPlayer()->IsBeingTeleported())
    {
        time_t currTime = GameTime::GetGameTime().count();
        if (updateInSessions) // session update from World::UpdateUsers
        {
            if (GetPlayer()->IsBeingTeleportedFar() && GetPlayer()->GetSemaphoreTeleportFar() + sWorld->getIntConfig(CONFIG_TELEPORT_TIMEOUT_FAR) < currTime)
                while (GetPlayer() && GetPlayer()->IsBeingTeleportedFar())
                    HandleMoveWorldportAck();
        }
        else // session update from Map::Update
        {
            if (GetPlayer()->IsBeingTeleportedNear() && GetPlayer()->GetSemaphoreTeleportNear() + sWorld->getIntConfig(CONFIG_TELEPORT_TIMEOUT_NEAR) < currTime)
                while (GetPlayer() && GetPlayer()->IsInWorld() && GetPlayer()->IsBeingTeleportedNear())
                {
                    Player* plMover = GetPlayer()->m_mover->ToPlayer();
                    if (!plMover)
                        break;
                    WDataStore pkt(MSG_MOVE_TELEPORT_ACK, 20);
                    pkt << plMover->GetPackGUID();
                    pkt << uint32(0); // flags
                    pkt << uint32(0); // time
                    HandleMoveTeleportAck(pkt);
                }
        }
    }
}

void User::CharacterRemoveFromGame(bool save)
{
    // finish pending transfers before starting the logout
    while (m_player && m_player->IsBeingTeleportedFar())
        HandleMoveWorldportAck();

    m_loggingOut = true;
    m_playerSave = save;

    if (m_player)
    {
        //! Call script hook before other logout events
        sScriptMgr->OnBeforePlayerLogout(m_player);

        if (WOWGUID lguid = m_player->GetLootGUID())
            DoLootRelease(lguid);

        ///- If the player just died before logging out, make him appear as a ghost
        //FIXME: logout must be delayed in case lost connection with client in time of combat
        if (m_player->GetDeathTimer())
        {
            m_player->getHostileRefMgr().deleteReferences(true);
            m_player->BuildPlayerRepop();
            m_player->RepopAtGraveyard();
        }
        else if (m_player->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION))
        {
            // this will kill character by SPELL_AURA_SPIRIT_OF_REDEMPTION
            m_player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
            m_player->KillPlayer();
            m_player->BuildPlayerRepop();
            m_player->RepopAtGraveyard();
        }
        else if (m_player->HasPendingBind())
        {
            m_player->RepopAtGraveyard();
            m_player->SetPendingBind(0, 0);
        }

        // pussywizard: leave whole bg on logout (character stays ingame when necessary)
        // pussywizard: GetBattleground() checked inside
        m_player->LeaveBattleground();

        // pussywizard: checked first time
        if (!m_player->IsBeingTeleportedFar() && !m_player->m_InstanceValid && !m_player->IsGameMaster())
            m_player->RepopAtGraveyard();

        sOutdoorPvPMgr->HandlePlayerLeaveZone(m_player, m_player->GetZoneId());

        // pussywizard: remove from battleground queues on logout
        for (int i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
            if (BattlegroundQueueTypeId bgQueueTypeId = m_player->GetBattlegroundQueueTypeId(i))
            {
                // track if player logs out after invited to join BG
                if (m_player->IsInvitedForBattlegroundInstance())
                {
                    if (sWorld->getBoolConfig(CONFIG_BATTLEGROUND_TRACK_DESERTERS))
                    {
                        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_DESERTER_TRACK);
                        stmt->SetData(0, m_player->GetGUID().GetCounter());
                        stmt->SetData(1, BG_DESERTION_TYPE_INVITE_LOGOUT);
                        CharacterDatabase.Execute(stmt);
                    }

                    sScriptMgr->OnBattlegroundDesertion(m_player, BG_DESERTION_TYPE_INVITE_LOGOUT);
                }

                m_player->RemoveBattlegroundQueueId(bgQueueTypeId);
                sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId).RemovePlayer(m_player->GetGUID(), true);
            }

        ///- If the player is in a guild, update the guild roster and broadcast a logout message to other guild members
        if (Guild* guild = sGuildMgr->GetGuildById(m_player->GetGuildId()))
            guild->HandleMemberLogout(this);

        ///- Remove pet
        m_player->RemovePet(nullptr, PET_SAVE_AS_CURRENT);

        // pussywizard: on logout remove auras that are removed at map change (before saving to db)
        // there are some positive auras from boss encounters that can be kept by logging out and logging in after boss is dead, and may be used on next bosses
        m_player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_CHANGE_MAP);

        ///- If the player is in a group and LeaveGroupOnLogout is enabled or if the player is invited to a group, remove him. If the group is then only 1 person, disband the group.
        if (!m_player->GetGroup() || sWorld->getBoolConfig(CONFIG_LEAVE_GROUP_ON_LOGOUT))
            m_player->UninviteFromGroup();

        // remove player from the group if he is:
        // a) in group; b) not in raid group; c) logging out normally (not being kicked or disconnected) d) LeaveGroupOnLogout is enabled
        if (m_player->GetGroup() && !m_player->GetGroup()->isRaidGroup() && !m_player->GetGroup()->isLFGGroup() && m_sock && sWorld->getBoolConfig(CONFIG_LEAVE_GROUP_ON_LOGOUT))
            m_player->RemoveFromGroup();

        // pussywizard: checked second time after being removed from a group
        if (!m_player->IsBeingTeleportedFar() && !m_player->m_InstanceValid && !m_player->IsGameMaster())
            m_player->RepopAtGraveyard();

        // Repop at GraveYard or other player far teleport will prevent saving player because of not present map
        // Teleport player immediately for correct player save
        while (m_player && m_player->IsBeingTeleportedFar())
            HandleMoveWorldportAck();

        ///- empty buyback items and save the player in the database
        // some save parts only correctly work in case player present in map/player_lists (pets, etc)
        if (save)
        {
            uint32 eslot;
            for (int j = BUYBACK_SLOT_START; j < BUYBACK_SLOT_END; ++j)
            {
                eslot = j - BUYBACK_SLOT_START;
                m_player->SetGuidValue(PLAYER_FIELD_VENDORBUYBACK_SLOT_1 + (eslot * 2), WOWGUID::Empty);
                m_player->SetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, 0);
                m_player->SetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1 + eslot, 0);
            }
            m_player->SaveToDB(false, true);
        }

        ///- Leave all channels before player delete...
        m_player->CleanupChannels();

        //! Send update to group and reset stored max enchanting level
        if (m_player->GetGroup())
        {
            m_player->GetGroup()->SendUpdate();
            m_player->GetGroup()->ResetMaxEnchantingLevel();

            if (m_player->GetMap()->IsDungeon() || m_player->GetMap()->IsRaidOrHeroicDungeon())
            {
                Map::PlayerList const &playerList = m_player->GetMap()->GetPlayers();
                if (playerList.IsEmpty())
                    m_player->TeleportToEntryPoint();
            }
        }

        //! Call script hook before deletion
        sScriptMgr->OnPlayerLogout(m_player);

        METRIC_EVENT("player_events", "Logout", m_player->GetName());

        LOG_INFO("entities.player", "Account: {} (IP: {}) Logout Character:[{}] ({}) Level: {}",
            GetAccountId(), GetRemoteAddress(), m_player->GetName(), m_player->GetGUID().ToString(), m_player->GetLevel());

        //! Remove the player from the world
        // the player may not be in the world when logging out
        // e.g if he got disconnected during a transfer to another map
        // calls to GetMap in this case may cause crashes
        m_player->CleanupsBeforeDelete();
        if (Map* _map = m_player->FindMap())
        {
            _map->RemovePlayerFromMap(m_player, true);
            _map->AfterPlayerUnlinkFromMap();
        }

        SetPlayer(nullptr); // pointer already deleted

        //! Send the 'logout complete' packet to the client
        //! Client will respond by sending 3x CMSG_CANCEL_TRADE, which we currently dont handle
        SendLogoutCompleteMessage();

        //! Since each account can only have one online character at any given time, ensure all characters for active account are marked as offline
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ACCOUNT_ONLINE);
        stmt->SetData(0, GetAccountId());
        CharacterDatabase.Execute(stmt);
    }

    m_loggingOut = false;
    m_playerSave = false;
    m_playerRecentlyLogout = true;
    m_logoutRequestTime = 0;
}

/// Kick a player out of the World
void User::KickPlayer(std::string const& reason, bool setKicked)
{
    if (m_sock)
    {
        LOG_INFO("network.kick", "Account: {} Character: '{}' {} kicked with reason: {}", GetAccountId(), m_player ? m_player->GetName() : "<none>",
            m_player ? m_player->GetGUID().ToString() : "", reason);

        m_sock->Disconnect();
    }

    if (setKicked)
        SetKicked(true); // pussywizard: the session won't be left ingame for 60 seconds and to also kick offline session
}

bool User::ValidateHyperlinksAndMaybeKick(std::string_view str)
{
    if (Acore::Hyperlinks::CheckAllLinks(str))
        return true;

    LOG_ERROR("network", "Player {} {} sent a message with an invalid link:\n%.*s", GetPlayer()->GetName(),
        GetPlayer()->GetGUID().ToString(), STRING_VIEW_FMT_ARG(str));

    if (sWorld->getIntConfig(CONFIG_CHAT_STRICT_LINK_CHECKING_KICK))
        KickPlayer("User::ValidateHyperlinksAndMaybeKick Invalid chat link");

    return false;
}

bool User::DisallowHyperlinksAndMaybeKick(std::string_view str)
{
    if (str.find('|') == std::string_view::npos)
        return true;

    LOG_ERROR("network", "Player {} {} sent a message which illegally contained a hyperlink:\n%.*s", GetPlayer()->GetName(),
        GetPlayer()->GetGUID().ToString(), STRING_VIEW_FMT_ARG(str));

    if (sWorld->getIntConfig(CONFIG_CHAT_STRICT_LINK_CHECKING_KICK))
        KickPlayer("User::DisallowHyperlinksAndMaybeKick Illegal chat link");

    return false;
}

void User::SendNotification(const char* format, ...)
{
    if (format)
    {
        va_list ap;
        char szStr[1024];
        szStr[0] = '\0';
        va_start(ap, format);
        vsnprintf(szStr, 1024, format, ap);
        va_end(ap);

        WDataStore data(SMSG_NOTIFICATION, (strlen(szStr) + 1));
        data << szStr;
        Send(&data);
    }
}

void User::SendNotification(uint32 string_id, ...)
{
    char const* format = GetAcoreString(string_id);
    if (format)
    {
        va_list ap;
        char szStr[1024];
        szStr[0] = '\0';
        va_start(ap, string_id);
        vsnprintf(szStr, 1024, format, ap);
        va_end(ap);

        WDataStore data(SMSG_NOTIFICATION, (strlen(szStr) + 1));
        data << szStr;
        Send(&data);
    }
}

char const* User::GetAcoreString(uint32 entry) const
{
    return sObjectMgr->GetAcoreString(entry, GetSessionDbLocaleIndex());
}

void User::Handle_NULL(WDataStore& null)
{
    LOG_ERROR("network.opcode", "Received unhandled opcode {} from {}",
        GetOpcodeNameForLogging(static_cast<OpcodeClient>(null.GetOpcode())), GetPlayerInfo());
}

void User::Handle_EarlyProccess(WDataStore& recvPacket)
{
    LOG_ERROR("network.opcode", "Received opcode {} that must be processed in WowConnection::ReadDataHandler from {}",
        GetOpcodeNameForLogging(static_cast<OpcodeClient>(recvPacket.GetOpcode())), GetPlayerInfo());
}

void User::Handle_ServerSide(WDataStore& recvPacket)
{
    LOG_ERROR("network.opcode", "Received server-side opcode {} from {}",
        GetOpcodeNameForLogging(static_cast<OpcodeServer>(recvPacket.GetOpcode())), GetPlayerInfo());
}

void User::Handle_Deprecated(WDataStore& recvPacket)
{
    LOG_ERROR("network.opcode", "Received deprecated opcode {} from {}",
        GetOpcodeNameForLogging(static_cast<OpcodeClient>(recvPacket.GetOpcode())), GetPlayerInfo());
}

void User::SendAuthWaitQueue(uint32 position)
{
    if (position == 0)
    {
        WDataStore packet(SMSG_AUTH_RESPONSE, 1);
        packet << uint8(AUTH_OK);
        Send(&packet);
    }
    else
    {
        WDataStore packet(SMSG_AUTH_RESPONSE, 6);
        packet << uint8(AUTH_WAIT_QUEUE);
        packet << uint32(position);
        packet << uint8(0);                                 // unk
        Send(&packet);
    }
}

void User::LoadAccountData(PreparedQueryResult result, uint32 mask)
{
    for (uint32 i = 0; i < NUM_ACCOUNT_DATA_TYPES; ++i)
        if (mask & (1 << i))
            m_accountData[i] = AccountData();

    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        uint32 type = fields[0].Get<uint8>();
        if (type >= NUM_ACCOUNT_DATA_TYPES)
        {
            LOG_ERROR("network", "Table `{}` have invalid account data type ({}), ignore.", mask == GLOBAL_CACHE_MASK ? "account_data" : "character_account_data", type);
            continue;
        }

        if ((mask & (1 << type)) == 0)
        {
            LOG_ERROR("network", "Table `{}` have non appropriate for table  account data type ({}), ignore.", mask == GLOBAL_CACHE_MASK ? "account_data" : "character_account_data", type);
            continue;
        }

        m_accountData[type].Time = time_t(fields[1].Get<uint32>());
        m_accountData[type].Data = fields[2].Get<std::string>();
    } while (result->NextRow());
}

void User::SetAccountData(AccountDataType type, time_t tm, std::string const& data)
{
    uint32 id = 0;
    CharacterDatabaseStatements index;
    if ((1 << type) & GLOBAL_CACHE_MASK)
    {
        id = GetAccountId();
        index = CHAR_REP_ACCOUNT_DATA;
    }
    else
    {
        // m_player can be nullptr and packet received after logout but m_GUID still store correct guid
        if (!m_GUIDLow)
            return;

        id = m_GUIDLow;
        index = CHAR_REP_PLAYER_ACCOUNT_DATA;
    }

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(index);
    stmt->SetData(0, id);
    stmt->SetData(1, type);
    stmt->SetData(2, uint32(tm));
    stmt->SetData(3, data);
    CharacterDatabase.Execute(stmt);

    m_accountData[type].Time = tm;
    m_accountData[type].Data = data;
}

void User::SendAccountDataTimes(uint32 mask)
{
    WDataStore data(SMSG_ACCOUNT_DATA_TIMES, 4 + 1 + 4 + 8 * 4); // changed in WotLK
    data << uint32(GameTime::GetGameTime().count());                             // unix time of something
    data << uint8(1);
    data << uint32(mask);                                   // type mask
    for (uint32 i = 0; i < NUM_ACCOUNT_DATA_TYPES; ++i)
        if (mask & (1 << i))
            data << uint32(GetAccountData(AccountDataType(i))->Time);// also unix time
    Send(&data);
}

void User::LoadTutorialsData(PreparedQueryResult result)
{
    memset(m_Tutorials, 0, sizeof(uint32) * MAX_ACCOUNT_TUTORIAL_VALUES);

    if (result)
    {
        for (uint8 i = 0; i < MAX_ACCOUNT_TUTORIAL_VALUES; ++i)
        {
            m_Tutorials[i] = (*result)[i].Get<uint32>();
        }
    }

    m_TutorialsChanged = false;
}

void User::SendTutorialsData()
{
    WDataStore data(SMSG_TUTORIAL_FLAGS, 4 * MAX_ACCOUNT_TUTORIAL_VALUES);
    for (uint8 i = 0; i < MAX_ACCOUNT_TUTORIAL_VALUES; ++i)
        data << m_Tutorials[i];
    Send(&data);
}

void User::SaveTutorialsData(CharacterDatabaseTransaction trans)
{
    if (!m_TutorialsChanged)
        return;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_HAS_TUTORIALS);
    stmt->SetData(0, GetAccountId());
    bool hasTutorials = bool(CharacterDatabase.Query(stmt));

    stmt = CharacterDatabase.GetPreparedStatement(hasTutorials ? CHAR_UPD_TUTORIALS : CHAR_INS_TUTORIALS);

    for (uint8 i = 0; i < MAX_ACCOUNT_TUTORIAL_VALUES; ++i)
        stmt->SetData(i, m_Tutorials[i]);
    stmt->SetData(MAX_ACCOUNT_TUTORIAL_VALUES, GetAccountId());
    trans->Append(stmt);

    m_TutorialsChanged = false;
}

void User::ReadMovementInfo(WDataStore& data, CMovement* mi)
{
    Unit* mover = ActivePlayer()->m_mover;

    data >> mi->m_moveFlags;
    data >> mi->m_moveFlags2;
    data >> mi->time;
    data >> mi->pos.PositionXYZOStream();

    if ((mi->m_moveFlags & MOVEFLAG_IMMOBILIZED) != 0)
    {
        data >> mi->transport.guid.ReadAsPacked();

        data >> mi->transport.pos.PositionXYZOStream();
        data >> mi->transport.time;
        data >> mi->transport.seat;

        if ((mi->m_moveFlags2 & MOVEMENTFLAG2_INTERPOLATED_MOVEMENT) != 0)
            data >> mi->transport.time2;
    }

    if ((mi->m_moveFlags & (MOVEFLAG_SWIMMING | MOVEFLAG_FLYING)) != 0
        || (mi->m_moveFlags2 & MOVEMENTFLAG2_ALWAYS_ALLOW_PITCHING) != 0)
        data >> mi->pitch;

    data >> mi->fallTime;

    if ((mi->m_moveFlags & MOVEFLAG_FALLING) != 0)
    {
        data >> mi->jump.zspeed;
        data >> mi->jump.sinAngle;
        data >> mi->jump.cosAngle;
        data >> mi->jump.xyspeed;
    }

    if ((mi->m_moveFlags & MOVEFLAG_SPLINE_MOVER) != 0)
        data >> mi->splineElevation;

    // MOVEFLAG_ROOTED sent from the client is not valid
    // in conjunction with any of the moving movement flags such as MOVEFLAG_FORWARD.
    // It will freeze clients that receive this player's move.
    if ((mi->m_moveFlags & MOVEFLAG_ROOTED) != 0)
      mi->m_moveFlags &= ~MOVEFLAG_ROOTED;

    // Cannot hover without SPELL_AURA_HOVER
    if ((mi->m_moveFlags & MOVEFLAG_HOVER) != 0 && !mover->HasAuraType(SPELL_AURA_HOVER))
      mi->m_moveFlags &= ~MOVEFLAG_HOVER;

    // Cannot ascend and descend at the same time
    if ((mi->m_moveFlags & MOVEFLAG_ASCENDING) != 0) {
      if ((mi->m_moveFlags & MOVEFLAG_DESCENDING) != 0) {
        mi->m_moveFlags &= ~(MOVEFLAG_ASCENDING | MOVEFLAG_DESCENDING);
        KickPlayer("User sent illegal move flags");
      }
    }

    // Cannot move left and right at the same time
    if ((mi->m_moveFlags & MOVEFLAG_LEFT) != 0) {
      if ((mi->m_moveFlags & MOVEFLAG_RIGHT) != 0) {
        mi->m_moveFlags &= ~(MOVEFLAG_LEFT | MOVEFLAG_RIGHT);
        KickPlayer("User sent illegal move flags");
      }
    }

    // Cannot strafe left and right at the same time
    if ((mi->m_moveFlags & MOVEFLAG_STRAFE_LEFT) != 0) {
      if ((mi->m_moveFlags & MOVEFLAG_STRAFE_RIGHT) != 0) {
        mi->m_moveFlags &= ~(MOVEFLAG_STRAFE_LEFT | MOVEFLAG_STRAFE_RIGHT);
        KickPlayer("User sent illegal move flags");
      }
    }

    // Cannot pitch up and down at the same time
    if ((mi->m_moveFlags & MOVEFLAG_PITCH_UP) != 0) {
      if ((mi->m_moveFlags & MOVEFLAG_PITCH_DOWN) != 0) {
        mi->m_moveFlags &= ~(MOVEFLAG_PITCH_UP | MOVEFLAG_PITCH_DOWN);
        KickPlayer("User sent illegal move flags");
      }
    }

    // Cannot move forward and backward at the same time
    if ((mi->m_moveFlags & MOVEFLAG_FORWARD) != 0) {
      if ((mi->m_moveFlags & MOVEFLAG_BACKWARD) != 0) {
        mi->m_moveFlags &= ~(MOVEFLAG_FORWARD | MOVEFLAG_BACKWARD);
        KickPlayer("User sent illegal move flags");
      }
    }

    // Cannot walk on water without SPELL_AURA_WATER_WALK
    if ((mi->m_moveFlags & MOVEFLAG_WATER_WALK) != 0 &&
        !mover->HasAuraType(SPELL_AURA_WATER_WALK) &&
        !mover->HasAuraType(SPELL_AURA_GHOST) &&
        !mover->isDead()) {
      mi->m_moveFlags &= ~MOVEFLAG_WATER_WALK;
      // TODO: The client sends this flag on player repop.
      // Investigate a possible resolution
      //KickPlayer("User sent illegal move flags");
    }

    // Cannot feather fall without SPELL_AURA_FEATHER_FALL
    if ((mi->m_moveFlags & MOVEFLAG_FEATHER_FALL) != 0 &&
        !GetPlayer()->HasAuraType(SPELL_AURA_FEATHER_FALL)) {
      KickPlayer("User sent illegal move flags");
    }

    // Cannot fly without SPELL_AURA_FLY
    if ((mi->m_moveFlags & MOVEFLAG_FLYING) != 0) {
      if ((mi->m_moveFlags & MOVEFLAG_CAN_FLY) != 0 && mover->HasAuraType(SPELL_AURA_FLY))
        KickPlayer("User sent illegal move flags");
    }

    // Cannot fly and fall at the same time
    if ((mi->m_moveFlags & MOVEFLAG_CAN_FLY) != 0 || (mi->m_moveFlags & MOVEFLAG_DISABLE_GRAVITY) != 0) {
      if ((mi->m_moveFlags & MOVEFLAG_FALLING) != 0)
        mi->m_moveFlags &= ~MOVEFLAG_FALLING;
    }

    if ((mi->m_moveFlags & MOVEFLAG_SPLINE_AWAITING_LOAD) != 0 &&
        (!mover->movespline->Initialized() || mover->movespline->Finalized())) {
      mi->m_moveFlags &= ~MOVEFLAG_SPLINE_AWAITING_LOAD;
    }
}

void User::WriteMovementInfo(WDataStore* data, CMovement* mi)
{
    *data << mi->guid.WriteAsPacked();

    *data << mi->m_moveFlags;
    *data << mi->m_moveFlags2;
    *data << mi->time;
    *data << mi->pos.PositionXYZOStream();

    if ((mi->m_moveFlags & MOVEFLAG_IMMOBILIZED) != 0)
    {
        *data << mi->transport.guid.WriteAsPacked();

        *data << mi->transport.pos.PositionXYZOStream();
        *data << mi->transport.time;
        *data << mi->transport.seat;

        if ((mi->m_moveFlags2 & MOVEMENTFLAG2_INTERPOLATED_MOVEMENT) != 0)
            *data << mi->transport.time2;
    }

    if ((mi->m_moveFlags & (MOVEFLAG_SWIMMING | MOVEFLAG_FLYING)) != 0 ||
        (mi->m_moveFlags2 & MOVEMENTFLAG2_ALWAYS_ALLOW_PITCHING) != 0)
        *data << mi->pitch;

    *data << mi->fallTime;

    if ((mi->m_moveFlags & MOVEFLAG_FALLING) != 0)
    {
        *data << mi->jump.zspeed;
        *data << mi->jump.sinAngle;
        *data << mi->jump.cosAngle;
        *data << mi->jump.xyspeed;
    }

    if ((mi->m_moveFlags & MOVEFLAG_SPLINE_MOVER) != 0)
        *data << mi->splineElevation;
}

void User::ReadAddonsInfo(ByteBuffer& data)
{
    if (data.rpos() + 4 > data.size())
        return;

    uint32 size;
    data >> size;

    if (!size)
        return;

    if (size > 0xFFFFF)
    {
        LOG_ERROR("network", "User::ReadAddonsInfo addon info too big, size {}", size);
        return;
    }

    uLongf uSize = size;

    uint32 pos = data.rpos();

    ByteBuffer addonInfo;
    addonInfo.resize(size);

    if (uncompress(addonInfo.contents(), &uSize, data.contents() + pos, data.size() - pos) == Z_OK)
    {
        uint32 addonsCount;
        addonInfo >> addonsCount;                         // addons count

        for (uint32 i = 0; i < addonsCount; ++i)
        {
            std::string addonName;
            uint8 enabled;
            uint32 crc, unk1;

            // check next addon data format correctness
            if (addonInfo.rpos() + 1 > addonInfo.size())
                return;

            addonInfo >> addonName;

            addonInfo >> enabled >> crc >> unk1;

            LOG_DEBUG("network", "ADDON: Name: {}, Enabled: 0x{:x}, CRC: 0x{:x}, Unknown2: 0x{:x}", addonName, enabled, crc, unk1);

            AddonInfo addon(addonName, enabled, crc, 2, true);

            SavedAddon const* savedAddon = AddonMgr::GetAddonInfo(addonName);
            if (savedAddon)
            {
                bool match = true;

                if (addon.CRC != savedAddon->CRC)
                    match = false;

                if (!match)
                    LOG_DEBUG("network", "ADDON: {} was known, but didn't match known CRC (0x{:x})!", addon.Name, savedAddon->CRC);
                else
                    LOG_DEBUG("network", "ADDON: {} was known, CRC is correct (0x{:x})", addon.Name, savedAddon->CRC);
            }
            else
            {
                AddonMgr::SaveAddon(addon);

                LOG_DEBUG("network", "ADDON: {} (0x{:x}) was not known, saving...", addon.Name, addon.CRC);
            }

            /// @todo: Find out when to not use CRC/pubkey, and other possible states.
            m_addonsList.push_back(addon);
        }

        uint32 currentTime;
        addonInfo >> currentTime;
        LOG_DEBUG("network", "ADDON: CurrentTime: {}", currentTime);

        if (addonInfo.rpos() != addonInfo.size())
            LOG_DEBUG("network", "packet under-read!");
    }
    else
        LOG_ERROR("network", "Addon packet uncompress error!");
}

void User::SendAddonsInfo()
{
    uint8 addonPublicKey[256] =
    {
        0xC3, 0x5B, 0x50, 0x84, 0xB9, 0x3E, 0x32, 0x42, 0x8C, 0xD0, 0xC7, 0x48, 0xFA, 0x0E, 0x5D, 0x54,
        0x5A, 0xA3, 0x0E, 0x14, 0xBA, 0x9E, 0x0D, 0xB9, 0x5D, 0x8B, 0xEE, 0xB6, 0x84, 0x93, 0x45, 0x75,
        0xFF, 0x31, 0xFE, 0x2F, 0x64, 0x3F, 0x3D, 0x6D, 0x07, 0xD9, 0x44, 0x9B, 0x40, 0x85, 0x59, 0x34,
        0x4E, 0x10, 0xE1, 0xE7, 0x43, 0x69, 0xEF, 0x7C, 0x16, 0xFC, 0xB4, 0xED, 0x1B, 0x95, 0x28, 0xA8,
        0x23, 0x76, 0x51, 0x31, 0x57, 0x30, 0x2B, 0x79, 0x08, 0x50, 0x10, 0x1C, 0x4A, 0x1A, 0x2C, 0xC8,
        0x8B, 0x8F, 0x05, 0x2D, 0x22, 0x3D, 0xDB, 0x5A, 0x24, 0x7A, 0x0F, 0x13, 0x50, 0x37, 0x8F, 0x5A,
        0xCC, 0x9E, 0x04, 0x44, 0x0E, 0x87, 0x01, 0xD4, 0xA3, 0x15, 0x94, 0x16, 0x34, 0xC6, 0xC2, 0xC3,
        0xFB, 0x49, 0xFE, 0xE1, 0xF9, 0xDA, 0x8C, 0x50, 0x3C, 0xBE, 0x2C, 0xBB, 0x57, 0xED, 0x46, 0xB9,
        0xAD, 0x8B, 0xC6, 0xDF, 0x0E, 0xD6, 0x0F, 0xBE, 0x80, 0xB3, 0x8B, 0x1E, 0x77, 0xCF, 0xAD, 0x22,
        0xCF, 0xB7, 0x4B, 0xCF, 0xFB, 0xF0, 0x6B, 0x11, 0x45, 0x2D, 0x7A, 0x81, 0x18, 0xF2, 0x92, 0x7E,
        0x98, 0x56, 0x5D, 0x5E, 0x69, 0x72, 0x0A, 0x0D, 0x03, 0x0A, 0x85, 0xA2, 0x85, 0x9C, 0xCB, 0xFB,
        0x56, 0x6E, 0x8F, 0x44, 0xBB, 0x8F, 0x02, 0x22, 0x68, 0x63, 0x97, 0xBC, 0x85, 0xBA, 0xA8, 0xF7,
        0xB5, 0x40, 0x68, 0x3C, 0x77, 0x86, 0x6F, 0x4B, 0xD7, 0x88, 0xCA, 0x8A, 0xD7, 0xCE, 0x36, 0xF0,
        0x45, 0x6E, 0xD5, 0x64, 0x79, 0x0F, 0x17, 0xFC, 0x64, 0xDD, 0x10, 0x6F, 0xF3, 0xF5, 0xE0, 0xA6,
        0xC3, 0xFB, 0x1B, 0x8C, 0x29, 0xEF, 0x8E, 0xE5, 0x34, 0xCB, 0xD1, 0x2A, 0xCE, 0x79, 0xC3, 0x9A,
        0x0D, 0x36, 0xEA, 0x01, 0xE0, 0xAA, 0x91, 0x20, 0x54, 0xF0, 0x72, 0xD8, 0x1E, 0xC7, 0x89, 0xD2
    };

    WDataStore data(SMSG_ADDON_INFO, 4);

    for (AddonsList::iterator itr = m_addonsList.begin(); itr != m_addonsList.end(); ++itr)
    {
        data << uint8(itr->State);

        uint8 crcpub = itr->UsePublicKeyOrCRC;
        data << uint8(crcpub);
        if (crcpub)
        {
            uint8 usepk = (itr->CRC != STANDARD_ADDON_CRC); // If addon is Standard addon CRC
            data << uint8(usepk);
            if (usepk)                                      // if CRC is wrong, add public key (client need it)
            {
                LOG_DEBUG("network", "ADDON: CRC (0x{:x}) for addon {} is wrong (does not match expected 0x{:x}), sending pubkey", itr->CRC, itr->Name, STANDARD_ADDON_CRC);
                data.append(addonPublicKey, sizeof(addonPublicKey));
            }

            data << uint32(0);                              /// @todo: Find out the meaning of this.
        }

        uint8 unk3 = 0;                                     // 0 is sent here
        data << uint8(unk3);
        if (unk3)
        {
            // String, length 256 (null terminated)
            data << uint8(0);
        }
    }

    m_addonsList.clear();

    AddonMgr::BannedAddonList const* bannedAddons = AddonMgr::GetBannedAddons();
    data << uint32(bannedAddons->size());
    for (AddonMgr::BannedAddonList::const_iterator itr = bannedAddons->begin(); itr != bannedAddons->end(); ++itr)
    {
        data << uint32(itr->Id);
        data.append(itr->NameMD5);
        data.append(itr->VersionMD5);
        data << uint32(itr->Timestamp);
        data << uint32(1);  // IsBanned
    }

    Send(&data);
}

//=============================================================================
void User::RemoveFriend(WOWGUID &guid) {
  FRIEND_RESULT res = FriendList()->RemoveFriend(guid);
  SendFriendStatus(res, guid);
  if (res == FRIEND_REMOVED) {
    // Remove the friend from the FriendList database
    CharacterDatabase.Execute("DELETE FROM character_social "
                              "WHERE friend = {} AND guid = {}",
                              guid.GetCounter(), ActivePlayer()->GetGUID().GetCounter());
  }
}

//=============================================================================
void User::SendContactList (uint32_t flags) {
  WDataStore msg(SMSG_CONTACT_LIST);

  msg << flags;

  // Initialize contact indexes
  uint32_t iFriend = FriendList()->GetNumFriends();
  uint32_t iIgnore = FriendList()->GetNumIgnores();
  uint32_t iMute = FriendList()->GetNumMutes();

  // Write the total number of contacts to the message
  msg << iFriend + iIgnore + iMute;

  // Write each contact information to the outbound message
  for (auto pFriend = FriendList()->m_friends.begin(); pFriend != FriendList()->m_friends.end(); pFriend++) {
    msg << pFriend->m_GUID;
    msg << pFriend->m_flags;
    msg << pFriend->m_notes;

    // Look for a player object in the world matching the friend GUID
    if (Player* plrFriend = ObjectAccessor::FindConnectedPlayer(pFriend->m_GUID)) {
      // Show GM Invis friends as offline if this user is non-GM
      if (!IsGMAccount() && !plrFriend->isGMVisible()) {
        pFriend->m_status = FRIEND_STATUS_OFFLINE;
      }
      else {
        pFriend->m_status = FRIEND_STATUS_ONLINE;
        pFriend->m_areaId = plrFriend->GetAreaId();
        pFriend->m_level = plrFriend->GetLevel();
        pFriend->m_classId = plrFriend->GetClass();
        if (plrFriend->isAFK())
          pFriend->m_status = FRIEND_STATUS_AFK;
        if (plrFriend->isDND())
          pFriend->m_status = FRIEND_STATUS_DND;
      }
    }
    else
      pFriend->m_status = FRIEND_STATUS_OFFLINE;

    // Write the friend status to the message
    msg << pFriend->m_status;

    // Send player info for this friend if they are online
    if (pFriend->m_status != FRIEND_STATUS_OFFLINE) {
      msg << pFriend->m_areaId;
      msg << pFriend->m_level;
      msg << pFriend->m_classId;
    }
  }

  // Write the GUIDs and flags of all ignored contacts
  for (uint32_t i = 0; i < iIgnore; i++) {
    msg << FriendList()->m_ignore[i];
    msg << CONTACT_IGNORED;
  }

  // Write the GUIDs and flags of all muted contacts
  for (uint32_t i = 0; i < iMute; i++) {
    msg << FriendList()->m_mute[i];
    msg << CONTACT_MUTED;
  }

  // Send the message
  Send(&msg);
}

//=============================================================================
void User::SendFriendStatus (FRIEND_RESULT res, WOWGUID guid) {
  FriendList::Friend const* frnd = FriendList()->GetFriend(guid);
  if (!frnd)
    res = FRIEND_NOT_FOUND;

  WDataStore msg(SMSG_FRIEND_STATUS);
  msg << static_cast<uint8_t>(res);
  msg << guid;

  // Write friend notes to the message buffer if the friend was just added
  if (res == FRIEND_ADDED_ONLINE || res == FRIEND_ADDED_OFFLINE) {
    msg << frnd->m_notes;
  }

  // Write player info to the message buffer if the friend is online
  if (res == FRIEND_ONLINE || res == FRIEND_ADDED_ONLINE) {
    msg << frnd->m_status;
    msg << frnd->m_areaId;
    msg << frnd->m_level;
    msg << frnd->m_classId;
  }

  // Send the message
  Send(&msg);
}

//=============================================================================
void User::SetFriendNotes (WOWGUID const& guid, char const* notes) {
  FriendList()->SetFriendNotes(guid, notes);
}

void User::SetPlayer(Player* player)
{
    m_player = player;

    // set m_GUID that can be used while player loggined and later until m_playerRecentlyLogout not reset
    if (m_player)
        m_GUIDLow = m_player->GetGUID().GetCounter();
}

void User::ProcessQueryCallbacks()
{
    _queryProcessor.ProcessReadyCallbacks();
    _transactionCallbacks.ProcessReadyCallbacks();
    _queryHolderProcessor.ProcessReadyCallbacks();
}

TransactionCallback& User::AddTransactionCallback(TransactionCallback&& callback)
{
    return _transactionCallbacks.AddCallback(std::move(callback));
}

SQLQueryHolderCallback& User::AddQueryHolderCallback(SQLQueryHolderCallback&& callback)
{
    return _queryHolderProcessor.AddCallback(std::move(callback));
}

void User::InitWarden(SessionKey const& k, std::string const& os)
{
    if (os == "Win")
    {
        _warden = std::make_unique<WardenWin>();
        _warden->Init(this, k);
    }
    else if (os == "OSX")
    {
        // Disabled as it is causing the client to crash
        // _warden = new WardenMac();
        // _warden->Init(this, k);
    }
}

Warden* User::GetWarden()
{
    return &(*_warden);
}

bool User::DosProtection::EvaluateOpcode(WDataStore& p, time_t time) const
{
    uint32 maxPacketCounterAllowed = GetMaxPacketCounterAllowed(p.GetOpcode());

    // Return true if there no limit for the opcode
    if (!maxPacketCounterAllowed)
        return true;

    PacketCounter& packetCounter = _PacketThrottlingMap[p.GetOpcode()];
    if (packetCounter.lastReceiveTime != time)
    {
        packetCounter.lastReceiveTime = time;
        packetCounter.amountCounter = 0;
    }

    // Check if player is flooding some packets
    if (++packetCounter.amountCounter <= maxPacketCounterAllowed)
        return true;

    LOG_WARN("network", "AntiDOS: Account {}, IP: {}, Ping: {}, Character: {}, flooding packet (opc: {} (0x{:X}), count: {})",
        Session->GetAccountId(), Session->GetRemoteAddress(), Session->GetLatency(), Session->GetPlayerName(),
        opcodeTable[static_cast<OpcodeClient>(p.GetOpcode())]->Name, p.GetOpcode(), packetCounter.amountCounter);

    switch (_policy)
    {
        case POLICY_LOG:
            return true;
        case POLICY_KICK:
            {
                LOG_INFO("network", "AntiDOS: Player {} kicked!", Session->GetPlayerName());
                Session->KickPlayer();
                return false;
            }
        case POLICY_BAN:
            {
                uint32 bm = sWorld->getIntConfig(CONFIG_PACKET_SPOOF_BANMODE);
                uint32 duration = sWorld->getIntConfig(CONFIG_PACKET_SPOOF_BANDURATION); // in seconds
                std::string nameOrIp = "";
                switch (bm)
                {
                    case 0: // Ban account
                        (void)AccountMgr::GetName(Session->GetAccountId(), nameOrIp);
                        sBan->BanAccount(nameOrIp, std::to_string(duration), "DOS (Packet Flooding/Spoofing", "Server: AutoDOS");
                        break;
                    case 1: // Ban ip
                        nameOrIp = Session->GetRemoteAddress();
                        sBan->BanIP(nameOrIp, std::to_string(duration), "DOS (Packet Flooding/Spoofing", "Server: AutoDOS");
                        break;
                }

                LOG_INFO("network", "AntiDOS: Player automatically banned for {} seconds.", duration);
                return false;
            }
        default: // invalid policy
            return true;
    }
}

uint32 User::DosProtection::GetMaxPacketCounterAllowed(uint16 opcode) const
{
    uint32 maxPacketCounterAllowed;
    switch (opcode)
    {
        // CPU usage sending 2000 packets/second on a 3.70 GHz 4 cores on Win x64
        //                                              [% CPU mysqld]   [%CPU worldserver RelWithDebInfo]
        case CMSG_PLAYER_LOGIN:                         //   0               0.5
        case CMSG_NAME_QUERY:                           //   0               1
        case CMSG_PET_NAME_QUERY:                       //   0               1
        case CMSG_NPC_TEXT_QUERY:                       //   0               1
        case CMSG_ATTACKSTOP:                           //   0               1
        case CMSG_QUERY_QUESTS_COMPLETED:               //   0               1
        case CMSG_QUERY_TIME:                           //   0               1
        case CMSG_CORPSE_MAP_POSITION_QUERY:            //   0               1
        case CMSG_MOVE_TIME_SKIPPED:                    //   0               1
        case MSG_QUERY_NEXT_MAIL_TIME:                  //   0               1
        case CMSG_SET_SHEATHED:                         //   0               1
        case MSG_RAID_TARGET_UPDATE:                    //   0               1
        case CMSG_PLAYER_LOGOUT:                        //   0               1
        case CMSG_LOGOUT_REQUEST:                       //   0               1
        case CMSG_PET_RENAME:                           //   0               1
        case CMSG_QUESTGIVER_CANCEL:                    //   0               1
        case CMSG_QUESTGIVER_REQUEST_REWARD:            //   0               1
        case CMSG_COMPLETE_CINEMATIC:                   //   0               1
        case CMSG_BANKER_ACTIVATE:                      //   0               1
        case CMSG_BUY_BANK_SLOT:                        //   0               1
        case CMSG_OPT_OUT_OF_LOOT:                      //   0               1
        case CMSG_DUEL_ACCEPTED:                        //   0               1
        case CMSG_DUEL_CANCELLED:                       //   0               1
        case CMSG_CALENDAR_COMPLAIN:                    //   0               1
        case CMSG_QUEST_QUERY:                          //   0               1.5
        case CMSG_ITEM_QUERY_SINGLE:                    //   0               1.5
        case CMSG_ITEM_NAME_QUERY:                      //   0               1.5
        case CMSG_GAMEOBJECT_QUERY:                     //   0               1.5
        case CMSG_CREATURE_QUERY:                       //   0               1.5
        case CMSG_QUESTGIVER_STATUS_QUERY:              //   0               1.5
        case CMSG_GUILD_QUERY:                          //   0               1.5
        case CMSG_ARENA_TEAM_QUERY:                     //   0               1.5
        case CMSG_TAXINODE_STATUS_QUERY:                //   0               1.5
        case CMSG_TAXIQUERYAVAILABLENODES:              //   0               1.5
        case CMSG_QUESTGIVER_QUERY_QUEST:               //   0               1.5
        case CMSG_PAGE_TEXT_QUERY:                      //   0               1.5
        case MSG_QUERY_GUILD_BANK_TEXT:                 //   0               1.5
        case MSG_CORPSE_QUERY:                          //   0               1.5
        case MSG_MOVE_SET_FACING:                       //   0               1.5
        case CMSG_REQUEST_PARTY_MEMBER_STATS:           //   0               1.5
        case CMSG_QUESTGIVER_COMPLETE_QUEST:            //   0               1.5
        case CMSG_SET_ACTION_BUTTON:                    //   0               1.5
        case CMSG_RESET_INSTANCES:                      //   0               1.5
        case CMSG_HEARTH_AND_RESURRECT:                 //   0               1.5
        case CMSG_TOGGLE_PVP:                           //   0               1.5
        case CMSG_PET_ABANDON:                          //   0               1.5
        case CMSG_ACTIVATETAXIEXPRESS:                  //   0               1.5
        case CMSG_ACTIVATETAXI:                         //   0               1.5
        case CMSG_SELF_RES:                             //   0               1.5
        case CMSG_UNLEARN_SKILL:                        //   0               1.5
        case CMSG_EQUIPMENT_SET_SAVE:                   //   0               1.5
        case CMSG_DELETEEQUIPMENT_SET:                  //   0               1.5
        case CMSG_DISMISS_CRITTER:                      //   0               1.5
        case CMSG_REPOP_REQUEST:                        //   0               1.5
        case CMSG_GROUP_INVITE:                         //   0               1.5
        case CMSG_GROUP_DECLINE:                        //   0               1.5
        case CMSG_GROUP_ACCEPT:                         //   0               1.5
        case CMSG_GROUP_UNINVITE_GUID:                  //   0               1.5
        case CMSG_GROUP_UNINVITE:                       //   0               1.5
        case CMSG_GROUP_DISBAND:                        //   0               1.5
        case CMSG_BATTLEMASTER_JOIN_ARENA:              //   0               1.5
        case CMSG_LEAVE_BATTLEFIELD:                    //   0               1.5
        case MSG_GUILD_BANK_LOG_QUERY:                  //   0               2
        case CMSG_LOGOUT_CANCEL:                        //   0               2
        case CMSG_REALM_SPLIT:                          //   0               2
        case CMSG_ALTER_APPEARANCE:                     //   0               2
        case CMSG_QUEST_CONFIRM_ACCEPT:                 //   0               2
        case MSG_GUILD_EVENT_LOG_QUERY:                 //   0               2.5
        case CMSG_READY_FOR_ACCOUNT_DATA_TIMES:         //   0               2.5
        case CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY:     //   0               2.5
        case CMSG_BEGIN_TRADE:                          //   0               2.5
        case CMSG_INITIATE_TRADE:                       //   0               3
        case CMSG_MESSAGECHAT:                          //   0               3.5
        case CMSG_INSPECT:                              //   0               3.5
        case CMSG_AREA_SPIRIT_HEALER_QUERY:             // not profiled
        case CMSG_STANDSTATECHANGE:                     // not profiled
        case MSG_RANDOM_ROLL:                           // not profiled
        case CMSG_TIME_SYNC_RESP:                       // not profiled
        case CMSG_TRAINER_BUY_SPELL:                    // not profiled
        case CMSG_FORCE_SWIM_SPEED_CHANGE_ACK:          // not profiled
        case CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK:     // not profiled
        case CMSG_FORCE_RUN_SPEED_CHANGE_ACK:           // not profiled
        case CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK:      // not profiled
        case CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK:        // not profiled
        case CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK:   // not profiled
        case CMSG_FORCE_WALK_SPEED_CHANGE_ACK:          // not profiled
        case CMSG_FORCE_TURN_RATE_CHANGE_ACK:           // not profiled
        case CMSG_FORCE_PITCH_RATE_CHANGE_ACK:          // not profiled
            {
                // "0" is a magic number meaning there's no limit for the opcode.
                // All the opcodes above must cause little CPU usage and no sync/async database queries at all
                maxPacketCounterAllowed = 0;
                break;
            }

        case CMSG_QUESTGIVER_ACCEPT_QUEST:              //   0               4
        case CMSG_QUESTLOG_REMOVE_QUEST:                //   0               4
        case CMSG_QUESTGIVER_CHOOSE_REWARD:             //   0               4
        case CMSG_CONTACT_LIST:                         //   0               5
        case CMSG_LEARN_PREVIEW_TALENTS:                //   0               6
        case CMSG_AUTOBANK_ITEM:                        //   0               6
        case CMSG_AUTOSTORE_BANK_ITEM:                  //   0               6
        case CMSG_WHO:                                  //   0               7
        case CMSG_PLAYER_VEHICLE_ENTER:                 //   0               8
        case CMSG_LEARN_PREVIEW_TALENTS_PET:            // not profiled
        case MSG_MOVE_HEARTBEAT:
            {
                maxPacketCounterAllowed = 200;
                break;
            }

        case CMSG_GUILD_SET_PUBLIC_NOTE:                //   1               2         1 async db query
        case CMSG_GUILD_SET_OFFICER_NOTE:               //   1               2         1 async db query
        case CMSG_SET_CONTACT_NOTES:                    //   1               2.5       1 async db query
        case CMSG_CALENDAR_GET_CALENDAR:                //   0               1.5       medium upload bandwidth usage
        case CMSG_GUILD_BANK_QUERY_TAB:                 //   0               3.5       medium upload bandwidth usage
        case CMSG_QUERY_INSPECT_ACHIEVEMENTS:           //   0              13         high upload bandwidth usage
        case CMSG_GAMEOBJ_REPORT_USE:                   // not profiled
        case CMSG_GAMEOBJ_USE:                          // not profiled
        case MSG_PETITION_DECLINE:                      // not profiled
            {
                maxPacketCounterAllowed = 50;
                break;
            }

        case CMSG_QUEST_POI_QUERY:                      //   0              25         very high upload bandwidth usage
            {
                maxPacketCounterAllowed = MAX_QUEST_LOG_SIZE;
                break;
            }

        case CMSG_GM_REPORT_LAG:                        //   1               3         1 async db query
        case CMSG_SPELLCLICK:                           // not profiled
        case CMSG_REMOVE_GLYPH:                         // not profiled
        case CMSG_DISMISS_CONTROLLED_VEHICLE:           // not profiled
            {
                maxPacketCounterAllowed = 20;
                break;
            }

        case CMSG_PETITION_SIGN:                        //   9               4         2 sync 1 async db queries
        case CMSG_TURN_IN_PETITION:                     //   8               5.5       2 sync db query
        case CMSG_GROUP_CHANGE_SUB_GROUP:               //   6               5         1 sync 1 async db queries
        case CMSG_PETITION_QUERY:                       //   4               3.5       1 sync db query
        case CMSG_CHAR_RACE_CHANGE:                     //   5               4         1 sync db query
        case CMSG_CHAR_CUSTOMIZE:                       //   5               5         1 sync db query
        case CMSG_CHAR_FACTION_CHANGE:                  //   5               5         1 sync db query
        case CMSG_CHAR_DELETE:                          //   4               4         1 sync db query
        case CMSG_DEL_FRIEND:                           //   7               5         1 async db query
        case CMSG_ADD_FRIEND:                           //   6               4         1 async db query
        case CMSG_CHAR_RENAME:                          //   5               3         1 async db query
        case CMSG_GMSURVEY_SUBMIT:                      //   2               3         1 async db query
        case CMSG_BUG:                                  //   1               1         1 async db query
        case CMSG_GROUP_SET_LEADER:                     //   1               2         1 async db query
        case CMSG_GROUP_RAID_CONVERT:                   //   1               5         1 async db query
        case CMSG_GROUP_ASSISTANT_LEADER:               //   1               2         1 async db query
        case CMSG_PETITION_BUY:                         // not profiled                1 sync 1 async db queries
        case CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE:   // not profiled
        case CMSG_REQUEST_VEHICLE_PREV_SEAT:            // not profiled
        case CMSG_REQUEST_VEHICLE_NEXT_SEAT:            // not profiled
        case CMSG_REQUEST_VEHICLE_SWITCH_SEAT:          // not profiled
        case CMSG_REQUEST_VEHICLE_EXIT:                 // not profiled
        case CMSG_CONTROLLER_EJECT_PASSENGER:           // not profiled
        case CMSG_ITEM_REFUND:                          // not profiled
        case CMSG_SOCKET_GEMS:                          // not profiled
        case CMSG_WRAP_ITEM:                            // not profiled
        case CMSG_REPORT_PVP_AFK:                       // not profiled
            {
                maxPacketCounterAllowed = 10;
                break;
            }

        case CMSG_CHAR_CREATE:                          //   7               5         3 async db queries
        case CMSG_CHAR_ENUM:                            //  22               3         2 async db queries
        case CMSG_GMTICKET_CREATE:                      //   1              25         1 async db query
        case CMSG_GMTICKET_UPDATETEXT:                  //   0              15         1 async db query
        case CMSG_GMTICKET_DELETETICKET:                //   1              25         1 async db query
        case CMSG_GMRESPONSE_RESOLVE:                   //   1              25         1 async db query
        case CMSG_CALENDAR_ADD_EVENT:                   //  21              10         2 async db query
        case CMSG_CALENDAR_UPDATE_EVENT:                // not profiled
        case CMSG_CALENDAR_REMOVE_EVENT:                // not profiled
        case CMSG_CALENDAR_COPY_EVENT:                  // not profiled
        case CMSG_CALENDAR_EVENT_INVITE:                // not profiled
        case CMSG_CALENDAR_EVENT_SIGNUP:                // not profiled
        case CMSG_CALENDAR_EVENT_RSVP:                  // not profiled
        case CMSG_CALENDAR_EVENT_REMOVE_INVITE:         // not profiled
        case CMSG_CALENDAR_EVENT_MODERATOR_STATUS:      // not profiled
        case CMSG_ARENA_TEAM_INVITE:                    // not profiled
        case CMSG_ARENA_TEAM_ACCEPT:                    // not profiled
        case CMSG_ARENA_TEAM_DECLINE:                   // not profiled
        case CMSG_ARENA_TEAM_LEAVE:                     // not profiled
        case CMSG_ARENA_TEAM_DISBAND:                   // not profiled
        case CMSG_ARENA_TEAM_REMOVE:                    // not profiled
        case CMSG_ARENA_TEAM_LEADER:                    // not profiled
        case CMSG_LOOT_METHOD:                          // not profiled
        case CMSG_GUILD_INVITE:                         // not profiled
        case CMSG_GUILD_ACCEPT:                         // not profiled
        case CMSG_GUILD_DECLINE:                        // not profiled
        case CMSG_GUILD_LEAVE:                          // not profiled
        case CMSG_GUILD_DISBAND:                        // not profiled
        case CMSG_GUILD_LEADER:                         // not profiled
        case CMSG_GUILD_MOTD:                           // not profiled
        case CMSG_GUILD_RANK:                           // not profiled
        case CMSG_GUILD_ADD_RANK:                       // not profiled
        case CMSG_GUILD_DEL_RANK:                       // not profiled
        case CMSG_GUILD_INFO_TEXT:                      // not profiled
        case CMSG_GUILD_BANK_DEPOSIT_MONEY:             // not profiled
        case CMSG_GUILD_BANK_WITHDRAW_MONEY:            // not profiled
        case CMSG_GUILD_BANK_BUY_TAB:                   // not profiled
        case CMSG_GUILD_BANK_UPDATE_TAB:                // not profiled
        case CMSG_SET_GUILD_BANK_TEXT:                  // not profiled
        case MSG_SAVE_GUILD_EMBLEM:                     // not profiled
        case MSG_PETITION_RENAME:                       // not profiled
        case MSG_TALENT_WIPE_CONFIRM:                   // not profiled
        case MSG_SET_DUNGEON_DIFFICULTY:                // not profiled
        case MSG_SET_RAID_DIFFICULTY:                   // not profiled
        case MSG_PARTY_ASSIGNMENT:                      // not profiled
        case MSG_RAID_READY_CHECK:                      // not profiled
            {
                maxPacketCounterAllowed = 3;
                break;
            }

        case CMSG_ITEM_REFUND_INFO:                     // not profiled
            {
                maxPacketCounterAllowed = PLAYER_SLOTS_COUNT;
                break;
            }

        default:
            {
                maxPacketCounterAllowed = 100;
                break;
            }
    }

    return maxPacketCounterAllowed;
}

User::DosProtection::DosProtection(User* s) :
    Session(s), _policy((Policy)sWorld->getIntConfig(CONFIG_PACKET_SPOOF_POLICY)) { }

void User::ResetTimeSync()
{
    _timeSyncNextCounter = 0;
    _pendingTimeSyncRequests.clear();
}

void User::SendTimeSync()
{
    WDataStore data(SMSG_TIME_SYNC_REQ, 4);
    data << uint32(_timeSyncNextCounter);
    Send(&data);

    _pendingTimeSyncRequests[_timeSyncNextCounter] = getMSTime();

    // Schedule next sync in 10 sec (except for the 2 first packets, which are spaced by only 5s)
    _timeSyncTimer = _timeSyncNextCounter == 0 ? 5000 : 10000;
    _timeSyncNextCounter++;
}

class AccountInfoQueryHolderPerRealm : public CharacterDatabaseQueryHolder
{
public:
    enum
    {
        GLOBAL_ACCOUNT_DATA = 0,
        TUTORIALS,

        MAX_QUERIES
    };

    AccountInfoQueryHolderPerRealm() { SetSize(MAX_QUERIES); }

    bool Initialize(uint32 accountId)
    {
        bool ok = true;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ACCOUNT_DATA);
        stmt->SetData(0, accountId);
        ok = SetPreparedQuery(GLOBAL_ACCOUNT_DATA, stmt) && ok;

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_TUTORIALS);
        stmt->SetData(0, accountId);
        ok = SetPreparedQuery(TUTORIALS, stmt) && ok;

        return ok;
    }
};

void User::InitializeSession()
{
    uint32 cacheVersion = sWorld->getIntConfig(CONFIG_CLIENTCACHE_VERSION);
    sScriptMgr->OnBeforeFinalizePlayerWorldSession(cacheVersion);

    std::shared_ptr<AccountInfoQueryHolderPerRealm> realmHolder = std::make_shared<AccountInfoQueryHolderPerRealm>();
    if (!realmHolder->Initialize(GetAccountId()))
    {
        SendAuthResponse(AUTH_SYSTEM_ERROR, false);
        return;
    }

    AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(realmHolder)).AfterComplete([this, cacheVersion](SQLQueryHolderBase const& holder)
    {
        InitializeSessionCallback(static_cast<AccountInfoQueryHolderPerRealm const&>(holder), cacheVersion);
    });
}

void User::InitializeSessionCallback(CharacterDatabaseQueryHolder const& realmHolder, uint32 clientCacheVersion)
{
    LoadAccountData(realmHolder.GetPreparedResult(AccountInfoQueryHolderPerRealm::GLOBAL_ACCOUNT_DATA), GLOBAL_CACHE_MASK);
    LoadTutorialsData(realmHolder.GetPreparedResult(AccountInfoQueryHolderPerRealm::TUTORIALS));

    if (!m_inQueue)
    {
        SendAuthResponse(AUTH_OK, true);
    }
    else
    {
        SendAuthWaitQueue(0);
    }

    SetInQueue(false);
    ResetTimeOutTime(false);

    SendAddonsInfo();
    SendClientCacheVersion(clientCacheVersion);
    SendTutorialsData();
}

//=============================================================================
void User::SendGmResurrectFailure()
{
    WDataStore msg(SMSG_RESURRECT_FAILED, sizeof(uint32_t));
    msg << 1u;
    Send(&msg);
}

//=============================================================================
void User::SendGmResurrectSuccess()
{
    WDataStore msg(SMSG_RESURRECT_FAILED, sizeof(uint32_t));
    msg << 0u;
    Send(&msg);
}

//=============================================================================
void User::SendPlayerNotFoundFailure()
{
    SendNotification("Player not found");
}

/******************************************************************************
*
*   MESSAGE HANDLERS
*
***/

//=============================================================================
static void UserBootMeHandler (User*        user,
                               NETMESSAGE   msgId,
                               uint         eventTime,
                               WDataStore*  msg) {

  // Forcibly remove the user from the server completely
  user->KickPlayer();
}

//=============================================================================
static void UserBeastmasterHandler (User*       user,
                                    NETMESSAGE  msgId,
                                    uint        eventTime,
                                    WDataStore* msg) {
  uint8_t val;
  msg->Get(val);

  user->SendNotification("Beastmaster %s for player %s", val ? "enabled" : "disabled", user->GetPlayerName().c_str());
}

//=============================================================================
static void UserBugReportHandler (User*       user,
                                  NETMESSAGE  msgId,
                                  uint        eventTime,
                                  WDataStore* msg) {

  uint32 category;
  msg->Get(category);

  uint32 bytes;
  msg->Get(bytes);

  auto text = (char*)malloc(bytes);
  msg->GetString(text, bytes);

  msg->Get(bytes);
  auto title = (char*)malloc(bytes);
  msg->GetString(title, bytes);

  auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_BUG_REPORT);
  stmt->SetData(0, category);
  stmt->SetData(1, title);
  stmt->SetData(2, text);
  CharacterDatabase.Execute(stmt);

  free(text);
  free(title);
}

//=============================================================================
static void UserGmResurrectHandler (User*        user,
                                    NETMESSAGE   msgId,
                                    uint         eventTime,
                                    WDataStore*  msg) {

  // Read the character name from the message buffer
  char name[256];
  msg->GetString(name, -1);

  FormatCharacterName(name);

  if (Player* player = ObjectAccessor::FindPlayerByName(name)) {
    if (player->IsDeadOrGhost()) {
      player->Resurrect(1.0f);
      user->SendGmResurrectSuccess();
    }
    else
      user->SendGmResurrectFailure();
  }
  else
    user->SendPlayerNotFoundFailure();
}

//=============================================================================
static void UserWorldTeleportHandler (User*       user,
                                      NETMESSAGE  msgId,
                                      uint        eventTime,
                                      WDataStore* msg) {

  // READ THE MESSAGE DATA
  auto requestTime  = msg->read<uint32>();
  auto continentID  = msg->read<uint32>();
  auto player       = msg->read<WOWGUID>();
  auto position     = msg->read<G3D::Vector3>();
  auto facing       = msg->read<float>();

  // LOOK FOR A PLAYER OBJECT IN THE WORLD THAT MATCHES THE PLAYER GUID
  // OR RETRIEVE A POINTER TO THE ACTIVE PLAYER FOR THE CURRENT SESSION
  Player* playerPtr = player ? ObjectAccessor::FindPlayer(player) : user->ActivePlayer();
  if (!playerPtr) {
    // TODO: Look for a matching character GUID in DB and set its position offline
    return;
  }

  // WRITE THE ACTION TO THE GM LOG
  LOG_GM(user->GetAccountId(),
         "Player {} sent command: worldport {} {} {} {} {}",
         playerPtr->GetName(), continentID, position.x, position.y, position.z, facing);

  // TELEPORT THE PLAYER
  playerPtr->Teleport(continentID, position.x, position.y, position.z, facing, TELE_TO_GM_MODE);
}

//=============================================================================
void UserInitialize () {
  if (s_initialized) return;

  WowConnection::SetMessageHandler(CMSG_BOOTME, UserBootMeHandler, GM_SECURITY);
  WowConnection::SetMessageHandler(CMSG_WORLD_TELEPORT, UserWorldTeleportHandler, GM_SECURITY);
  WowConnection::SetMessageHandler(CMSG_GM_RESURRECT, UserGmResurrectHandler, GM_SECURITY);
  WowConnection::SetMessageHandler(CMSG_BUG, UserBugReportHandler);
  WowConnection::SetMessageHandler(CMSG_BEASTMASTER, UserBeastmasterHandler, GM_SECURITY);

  s_initialized = true;
}

//=============================================================================
void UserDestroy () {
  if (!s_initialized) return;

  WowConnection::ClearMessageHandler(CMSG_BOOTME);
  WowConnection::ClearMessageHandler(CMSG_WORLD_TELEPORT);
  WowConnection::ClearMessageHandler(CMSG_GM_RESURRECT);
  WowConnection::ClearMessageHandler(CMSG_BUG);
  WowConnection::ClearMessageHandler(CMSG_BEASTMASTER);

  s_initialized = false;
}

