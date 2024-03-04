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

#ifndef __AUTHSESSION_H__
#define __AUTHSESSION_H__

#include "../../../game/WowServices/AccountInfo.h"
#include "AsyncCallbackProcessor.h"
#include "BigNumber.h"
#include "ByteBuffer.h"
#include "Common.h"
#include "CryptoHash.h"
#include "Optional.h"
#include "QueryResult.h"
#include "SRP6.h"
#include "Socket.h"
#include <boost/asio/ip/tcp.hpp>
#include <memory>

using boost::asio::ip::tcp;

class Field;
struct AuthHandler;

enum eAuthCmd
{
    AUTH_LOGON_CHALLENGE = 0x00,
    AUTH_LOGON_PROOF = 0x01,
    AUTH_RECONNECT_CHALLENGE = 0x02,
    AUTH_RECONNECT_PROOF = 0x03,
    REALM_LIST = 0x10,
    XFER_INITIATE = 0x30,
    XFER_DATA = 0x31,
    XFER_ACCEPT = 0x32,
    XFER_RESUME = 0x33,
    XFER_CANCEL = 0x34,
    NUM_AUTH_CMD
};

enum AuthStatus
{
    STATUS_CHALLENGE = 0,
    STATUS_LOGON_PROOF,
    STATUS_RECONNECT_PROOF,
    STATUS_AUTHED,
    STATUS_WAITING_FOR_REALM_LIST,
    STATUS_CLOSED
};

struct AccountInfo
{
    uint32_t    m_accountId = 0;
    std::string m_accountName;
    uint32_t    m_accountFlags = 0;

    void LoadResult(Field* fields);

    bool IsLockedToIP = false;
    std::string LockCountry;
    std::string LastIP;
    uint32 FailedLogins = 0;
    bool IsBanned = false;
    bool IsPermanentlyBanned = false;
    AccountTypes SecurityLevel = SEC_PLAYER;
};

class AuthSession : public Socket<AuthSession>
{
    typedef Socket<AuthSession> AuthSocket;

public:
    static std::unordered_map<uint8, AuthHandler> InitHandlers();

    AuthSession(tcp::socket&& socket);

    void Start() override;
    bool Update() override;

    void SendPacket(ByteBuffer& packet);

    static int SetMessageHandler(unsigned int msgId,int(*handler)(void* formal,unsigned int msgId,unsigned int eventTime,ByteBuffer* msg));
    static int ClearMessageHandler(unsigned int msgId);

protected:
    void ReadHandler() override;

private:
    bool HandleLogonChallenge();
    bool HandleLogonProof();
    bool HandleReconnectChallenge();
    bool HandleReconnectProof();
    bool HandleRealmList();

    void CheckIpCallback(PreparedQueryResult result);
    void LogonChallengeCallback(PreparedQueryResult result);
    void ReconnectChallengeCallback(PreparedQueryResult result);
    void RealmListCallback(PreparedQueryResult result);

    bool VerifyVersion(uint8 const* a, int32 aLength, Acore::Crypto::SHA1::Digest const& versionProof, bool isReconnect);

    Optional<Acore::Crypto::SRP6> _srp6;
    SessionKey _sessionKey = {};
    std::array<uint8, 16> _reconnectProof = {};

    AuthStatus _status;
    AccountInfo _accountInfo;
    Optional<std::vector<uint8>> _totpSecret;
    std::string _localizationName;
    std::string _os;
    std::string _ipCountry;
    uint16 _build;
    uint8 _expversion;

    QueryCallbackProcessor _queryProcessor;
};

#pragma pack(push, 1)

struct AuthHandler
{
    AuthStatus status;
    size_t packetSize;
    bool (AuthSession::* handler)();
};

#pragma pack(pop)

#endif
