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

#include "WowConnection.h"
#include "AccountMgr.h"
#include "Config.h"
#include "CryptoHash.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "IPLocation.h"
#include "Opcodes.h"
#include "PacketLog.h"
#include "Random.h"
#include "Realm.h"
#include "ScriptMgr.h"
#include "World.h"
#include "User.h"
#include "zlib.h"
#include <memory>

using boost::asio::ip::tcp;

void compressBuff(void* dst, uint32* dst_size, void* src, int src_size)
{
    z_stream c_stream;

    c_stream.zalloc = (alloc_func)0;
    c_stream.zfree = (free_func)0;
    c_stream.opaque = (voidpf)0;

    // default Z_BEST_SPEED (1)
    int z_res = deflateInit(&c_stream, sWorld->getIntConfig(CONFIG_COMPRESSION));
    if (z_res != Z_OK)
    {
        LOG_ERROR("entities.object", "Can't compress update packet (zlib: deflateInit) Error code: {} ({})", z_res, zError(z_res));
        *dst_size = 0;
        return;
    }

    c_stream.next_out = (Bytef*)dst;
    c_stream.avail_out = *dst_size;
    c_stream.next_in = (Bytef*)src;
    c_stream.avail_in = (uInt)src_size;

    z_res = deflate(&c_stream, Z_NO_FLUSH);
    if (z_res != Z_OK)
    {
        LOG_ERROR("entities.object", "Can't compress update packet (zlib: deflate) Error code: {} ({})", z_res, zError(z_res));
        *dst_size = 0;
        return;
    }

    if (c_stream.avail_in != 0)
    {
        LOG_ERROR("entities.object", "Can't compress update packet (zlib: deflate not greedy)");
        *dst_size = 0;
        return;
    }

    z_res = deflate(&c_stream, Z_FINISH);
    if (z_res != Z_STREAM_END)
    {
        LOG_ERROR("entities.object", "Can't compress update packet (zlib: deflate should report Z_STREAM_END instead {} ({})", z_res, zError(z_res));
        *dst_size = 0;
        return;
    }

    z_res = deflateEnd(&c_stream);
    if (z_res != Z_OK)
    {
        LOG_ERROR("entities.object", "Can't compress update packet (zlib: deflateEnd) Error code: {} ({})", z_res, zError(z_res));
        *dst_size = 0;
        return;
    }

    *dst_size = c_stream.total_out;
}

void EncryptableAndCompressiblePacket::CompressIfNeeded()
{
    if (!NeedsCompression())
        return;

    uint32 pSize = size();

    uint32 destsize = compressBound(pSize);
    ByteBuffer buf(destsize + sizeof(uint32));
    buf.resize(destsize + sizeof(uint32));

    buf.put<uint32>(0, pSize);
    compressBuff(const_cast<uint8*>(buf.contents()) + sizeof(uint32), &destsize, (void*)contents(), pSize);
    if (destsize == 0)
        return;

    buf.resize(destsize + sizeof(uint32));

    ByteBuffer::operator=(std::move(buf));
    SetOpcode(SMSG_COMPRESSED_UPDATE_OBJECT);
}

WowConnection::WowConnection(tcp::socket&& socket)
    : Socket(std::move(socket)), _OverSpeedPings(0), _worldSession(nullptr), _authed(false), _sendBufferSize(4096)
{
    Acore::Crypto::GetRandomBytes(_authSeed);
    _headerBuffer.Resize(sizeof(ClientPktHeader));
}

WowConnection::~WowConnection() = default;

void WowConnection::Start()
{
    std::string ip_address = GetRemoteIpAddress().to_string();

    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_IP_INFO);
    stmt->SetData(0, ip_address);

    _queryProcessor.AddCallback(LoginDatabase.AsyncQuery(stmt).WithPreparedCallback(std::bind(&WowConnection::CheckIpCallback, this, std::placeholders::_1)));
}

void WowConnection::CheckIpCallback(PreparedQueryResult result)
{
    if (result)
    {
        bool banned = false;
        do
        {
            Field* fields = result->Fetch();
            if (fields[0].Get<uint64>() != 0)
                banned = true;

        } while (result->NextRow());

        if (banned)
        {
            SendAuthResponseError(AUTH_REJECT);
            LOG_ERROR("network", "WowConnection::CheckIpCallback: Sent Auth Response (IP {} banned).", GetRemoteIpAddress().to_string());
            DelayedCloseSocket();
            return;
        }
    }

    AsyncRead();
    HandleSendAuthSession();
}

bool WowConnection::Update()
{
    EncryptableAndCompressiblePacket* queued;
    if (_bufferQueue.Dequeue(queued))
    {
        // Allocate buffer only when it's needed but not on every Update() call.
        MessageBuffer buffer(_sendBufferSize);
        std::size_t currentPacketSize;
        do
        {
            queued->CompressIfNeeded();
            ServerPktHeader header(queued->size() + 2, queued->GetOpcode());
            if (queued->NeedsEncryption())
                _authCrypt.EncryptSend(header.header, header.getHeaderLength());

            currentPacketSize = queued->size() + header.getHeaderLength();

            if (buffer.GetRemainingSpace() < currentPacketSize)
            {
                QueuePacket(std::move(buffer));
                buffer.Resize(_sendBufferSize);
            }

            if (buffer.GetRemainingSpace() >= currentPacketSize)
            {
                buffer.Write(header.header, header.getHeaderLength());
                if (!queued->empty())
                    buffer.Write(queued->contents(), queued->size());
            }
            else    // Single packet larger than current buffer size
            {
                // Resize buffer to fit current packet
                buffer.Resize(currentPacketSize);

                // Grow future buffers to current packet size if still below limit
                if (currentPacketSize <= 65536)
                    _sendBufferSize = currentPacketSize;

                buffer.Write(header.header, header.getHeaderLength());
                if (!queued->empty())
                    buffer.Write(queued->contents(), queued->size());
            }

            delete queued;
        } while (_bufferQueue.Dequeue(queued));

        if (buffer.GetActiveSize() > 0)
            QueuePacket(std::move(buffer));
    }

    if (!BaseSocket::Update())
        return false;

    _queryProcessor.ProcessReadyCallbacks();

    return true;
}

void WowConnection::HandleSendAuthSession()
{
    WDataStore packet(SMSG_AUTH_CHALLENGE, 40);
    packet << uint32(1);                                    // 1...31
    packet.append(_authSeed);

    packet.append(Acore::Crypto::GetRandomBytes<32>());               // new encryption seeds

    SendPacketAndLogOpcode(packet);
}

void WowConnection::OnClose()
{
    {
        std::lock_guard<std::mutex> sessionGuard(_worldSessionLock);
        _worldSession = nullptr;
    }
}

void WowConnection::ReadHandler()
{
    if (!IsOpen())
        return;

    MessageBuffer& packet = GetReadBuffer();
    while (packet.GetActiveSize() > 0)
    {
        if (_headerBuffer.GetRemainingSpace() > 0)
        {
            // need to receive the header
            std::size_t readHeaderSize = std::min(packet.GetActiveSize(), _headerBuffer.GetRemainingSpace());
            _headerBuffer.Write(packet.GetReadPointer(), readHeaderSize);
            packet.ReadCompleted(readHeaderSize);

            if (_headerBuffer.GetRemainingSpace() > 0)
            {
                // Couldn't receive the whole header this time.
                ASSERT(packet.GetActiveSize() == 0);
                break;
            }

            // We just received nice new header
            if (!ReadHeaderHandler())
            {
                Disconnect();
                return;
            }
        }

        // We have full read header, now check the data payload
        if (_packetBuffer.GetRemainingSpace() > 0)
        {
            // need more data in the payload
            std::size_t readDataSize = std::min(packet.GetActiveSize(), _packetBuffer.GetRemainingSpace());
            _packetBuffer.Write(packet.GetReadPointer(), readDataSize);
            packet.ReadCompleted(readDataSize);

            if (_packetBuffer.GetRemainingSpace() > 0)
            {
                // Couldn't receive the whole data this time.
                ASSERT(packet.GetActiveSize() == 0);
                break;
            }
        }

        // just received fresh new payload
        ReadDataHandlerResult result = ReadDataHandler();
        _headerBuffer.Reset();

        if (result != ReadDataHandlerResult::Ok)
        {
            if (result != ReadDataHandlerResult::WaitingForQuery)
            {
                Disconnect();
            }

            return;
        }
    }

    AsyncRead();
}

bool WowConnection::ReadHeaderHandler()
{
    ASSERT(_headerBuffer.GetActiveSize() == sizeof(ClientPktHeader));

    if (_authCrypt.IsInitialized())
    {
        _authCrypt.DecryptRecv(_headerBuffer.GetReadPointer(), sizeof(ClientPktHeader));
    }

    ClientPktHeader* header = reinterpret_cast<ClientPktHeader*>(_headerBuffer.GetReadPointer());
    EndianConvertReverse(header->size);
    EndianConvert(header->cmd);

    if (!header->IsValidSize() || !header->IsValidOpcode())
    {
        LOG_ERROR("network", "WowConnection::ReadHeaderHandler(): client {} sent malformed packet (size: {}, cmd: {})",
            GetRemoteIpAddress().to_string(), header->size, header->cmd);

        return false;
    }

    header->size -= sizeof(header->cmd);
    _packetBuffer.Resize(header->size);

    return true;
}

struct RealmConnection
{
    uint32 BattlegroupID = 0;
    uint32 LoginServerType = 0;
    uint32 RealmID = 0;
    uint32 Build = 0;
    std::array<uint8, 4> LocalChallenge = {};
    uint32 LoginServerID = 0;
    uint32 RegionID = 0;
    uint64 DosResponse = 0;
    Acore::Crypto::SHA1::Digest Digest = {};
    std::string Account;
    ByteBuffer AddonInfo;
};

struct AccountInfo
{
    uint32      m_accountId;
    // TODO:    m_accountName
    uint32_t    m_accountFlags;

    ::SessionKey SessionKey;
    std::string LastIP;
    bool IsLockedToIP;
    std::string LockCountry;
    uint8 Expansion;
    int64 MuteTime;
    LocaleConstant Locale;
    uint32 Recruiter;
    std::string OS;
    bool IsRectuiter;
    AccountTypes Security;
    bool IsBanned;
    uint32 TotalTime;

    explicit AccountInfo(Field* fields)
    {
        //           0             1          2         3               4            5           6         7            8     9           10          11
        // SELECT a.id, a.sessionkey, a.last_ip, a.locked, a.lock_country, a.expansion, a.mutetime, a.locale, a.recruiter, a.os, a.totaltime, aa.gmLevel,
        //                                                           12    13
        // ab.unbandate > UNIX_TIMESTAMP() OR ab.unbandate = ab.bandate, r.id
        // FROM account a
        // LEFT JOIN account_access aa ON a.id = aa.AccountID AND aa.RealmID IN (-1, ?)
        // LEFT JOIN account_banned ab ON a.id = ab.id
        // LEFT JOIN account r ON a.id = r.recruiter
        // WHERE a.username = ? ORDER BY aa.RealmID DESC LIMIT 1
        m_accountId = fields[0].Get<uint32>();
        m_accountFlags = fields[1].Get<uint32_t>();
        SessionKey = fields[2].Get<Binary, SESSION_KEY_LENGTH>();
        LastIP = fields[3].Get<std::string>();
        IsLockedToIP = fields[4].Get<bool>();
        LockCountry = fields[5].Get<std::string>();
        Expansion = fields[6].Get<uint8>();
        MuteTime = fields[7].Get<int64>();
        Locale = LocaleConstant(fields[7].Get<uint8>());
        Recruiter = fields[9].Get<uint32>();
        OS = fields[10].Get<std::string>();
        TotalTime = fields[11].Get<uint32>();
        Security = AccountTypes(fields[12].Get<uint8>());
        IsBanned = fields[13].Get<uint64>() != 0;
        IsRectuiter = fields[14].Get<uint32>() != 0;

        uint32 world_expansion = sWorld->getIntConfig(CONFIG_EXPANSION);
        if (Expansion > world_expansion)
            Expansion = world_expansion;

        if (Locale >= TOTAL_LOCALES)
            Locale = LOCALE_enUS;
    }
};

WowConnection::ReadDataHandlerResult WowConnection::ReadDataHandler()
{
    ClientPktHeader* header = reinterpret_cast<ClientPktHeader*>(_headerBuffer.GetReadPointer());
    OpcodeClient opcode = static_cast<OpcodeClient>(header->cmd);

    WDataStore packet(opcode, std::move(_packetBuffer));
    WDataStore* packetToQueue;

    if (sPacketLog->CanLogPacket())
        sPacketLog->LogPacket(packet, CLIENT_TO_SERVER, GetRemoteIpAddress(), GetRemotePort());

    std::unique_lock<std::mutex> sessionGuard(_worldSessionLock, std::defer_lock);

    switch (opcode)
    {
        case CMSG_PING:
        {
            LogOpcodeText(opcode, sessionGuard);
            try
            {
                return HandlePing(packet) ? ReadDataHandlerResult::Ok : ReadDataHandlerResult::Error;
            }
            catch (ByteBufferException const&)
            {
            }
            LOG_ERROR("network", "WowConnection::ReadDataHandler(): client {} sent malformed CMSG_PING", GetRemoteIpAddress().to_string());
            return ReadDataHandlerResult::Error;
        }
        case CMSG_AUTH_SESSION:
        {
            LogOpcodeText(opcode, sessionGuard);
            if (_authed)
            {
                // locking just to safely log offending user is probably overkill but we are disconnecting him anyway
                if (sessionGuard.try_lock())
                    LOG_ERROR("network", "WowConnection::ProcessIncoming: received duplicate CMSG_AUTH_SESSION from {}", _worldSession->GetPlayerInfo());
                return ReadDataHandlerResult::Error;
            }

            try
            {
                HandleAuthSession(packet);
                return ReadDataHandlerResult::WaitingForQuery;
            }
            catch (ByteBufferException const&) { }

            LOG_ERROR("network", "WowConnection::ReadDataHandler(): client {} sent malformed CMSG_AUTH_SESSION", GetRemoteIpAddress().to_string());
            return ReadDataHandlerResult::Error;
        }
        case CMSG_KEEP_ALIVE: /// @todo: handle this packet in the same way of CMSG_TIME_SYNC_RESP
            sessionGuard.lock();
            LogOpcodeText(opcode, sessionGuard);
            if (_worldSession)
            {
                _worldSession->ResetTimeOutTime(true);
                return ReadDataHandlerResult::Ok;
            }
            LOG_ERROR("network", "WowConnection::ReadDataHandler: client {} sent CMSG_KEEP_ALIVE without being authenticated", GetRemoteIpAddress().to_string());
            return ReadDataHandlerResult::Error;
        case CMSG_TIME_SYNC_RESP:
            packetToQueue = new WDataStore(std::move(packet), GameTime::Now());
            break;
        default:
            packetToQueue = new WDataStore(std::move(packet));
            break;
    }

    sessionGuard.lock();

    LogOpcodeText(opcode, sessionGuard);

    if (!_worldSession)
    {
        LOG_ERROR("network.opcode", "ProcessIncoming: Client not authed opcode = {}", uint32(opcode));
        delete packetToQueue;
        return ReadDataHandlerResult::Error;
    }

    // Our Idle timer will reset on any non PING opcodes on login screen, allowing us to catch people idling.
    if (packetToQueue->GetOpcode() != CMSG_WARDEN_DATA)
    {
        _worldSession->ResetTimeOutTime(false);
    }

    // Copy the packet to the heap before enqueuing
    _worldSession->QueuePacket(packetToQueue);

    return ReadDataHandlerResult::Ok;
}

void WowConnection::LogOpcodeText(OpcodeClient opcode, std::unique_lock<std::mutex> const& guard) const
{
    if (!guard)
    {
        LOG_TRACE("network.opcode", "C->S: {} {}", GetRemoteIpAddress().to_string(), GetOpcodeNameForLogging(opcode));
    }
    else
    {
        LOG_TRACE("network.opcode", "C->S: {} {}", (_worldSession ? _worldSession->GetPlayerInfo() : GetRemoteIpAddress().to_string()),
            GetOpcodeNameForLogging(opcode));
    }
}

void WowConnection::SendPacketAndLogOpcode(WDataStore const& packet)
{
    LOG_TRACE("network.opcode", "S->C: {} {}", GetRemoteIpAddress().to_string(), GetOpcodeNameForLogging(static_cast<OpcodeServer>(packet.GetOpcode())));
    SendPacket(packet);
}

void WowConnection::SendPacket(WDataStore const& packet)
{
    if (!IsOpen())
        return;

    if (sPacketLog->CanLogPacket())
        sPacketLog->LogPacket(packet, SERVER_TO_CLIENT, GetRemoteIpAddress(), GetRemotePort());

    _bufferQueue.Enqueue(new EncryptableAndCompressiblePacket(packet, _authCrypt.IsInitialized()));
}

void WowConnection::HandleAuthSession(WDataStore & recvPacket)
{
    std::shared_ptr<RealmConnection> authSession = std::make_shared<RealmConnection>();

    // Read the content of the packet
    recvPacket >> authSession->Build;
    recvPacket >> authSession->LoginServerID;
    recvPacket >> authSession->Account;
    recvPacket >> authSession->LoginServerType;
    recvPacket.read(authSession->LocalChallenge);
    recvPacket >> authSession->RegionID;
    recvPacket >> authSession->BattlegroupID;
    recvPacket >> authSession->RealmID;               // realmId from auth_database.realmlist table
    recvPacket >> authSession->DosResponse;
    recvPacket.read(authSession->Digest);
    authSession->AddonInfo.resize(recvPacket.size() - recvPacket.rpos());
    recvPacket.read(authSession->AddonInfo.contents(), authSession->AddonInfo.size()); // .contents will throw if empty, thats what we want

    // Get the account information from the auth database
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_INFO_BY_NAME);
    stmt->SetData(0, int32(realm.Id.Realm));
    stmt->SetData(1, authSession->Account);

    _queryProcessor.AddCallback(LoginDatabase.AsyncQuery(stmt).WithPreparedCallback(std::bind(&WowConnection::HandleAuthSessionCallback, this, authSession, std::placeholders::_1)));
}

void WowConnection::HandleAuthSessionCallback(std::shared_ptr<RealmConnection> authSession, PreparedQueryResult result)
{
    // Stop if the account is not found
    if (!result)
    {
        // We can not log here, as we do not know the account. Thus, no accountId.
        SendAuthResponseError(AUTH_UNKNOWN_ACCOUNT);
        LOG_ERROR("network", "WowConnection::HandleAuthSession: Sent Auth Response (unknown account).");
        DelayedCloseSocket();
        return;
    }

    AccountInfo account(result->Fetch());

    // For hook purposes, we get Remoteaddress at this point.
    std::string address = sConfigMgr->GetOption<bool>("AllowLoggingIPAddressesInDatabase", true, true) ? GetRemoteIpAddress().to_string() : "0.0.0.0";

    LoginDatabasePreparedStatement* stmt = nullptr;

    // As we don't know if attempted login process by ip works, we update last_attempt_ip right away
    stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_LAST_ATTEMPT_IP);
    stmt->SetData(0, address);
    stmt->SetData(1, authSession->Account);
    LoginDatabase.Execute(stmt);
    // This also allows to check for possible "hack" attempts on account

    // even if auth credentials are bad, try using the session key we have - client cannot read auth response error without it
    _authCrypt.Init(account.SessionKey);

    // First reject the connection if packet contains invalid data or realm state doesn't allow logging in
    if (sWorld->IsClosed())
    {
        SendAuthResponseError(AUTH_REJECT);
        LOG_ERROR("network", "WowConnection::HandleAuthSession: World closed, denying client ({}).", GetRemoteIpAddress().to_string());
        DelayedCloseSocket();
        return;
    }

    if (authSession->RealmID != realm.Id.Realm)
    {
        SendAuthResponseError(REALM_LIST_REALM_NOT_FOUND);
        LOG_ERROR("network", "WowConnection::HandleAuthSession: Client {} requested connecting with realm id {} but this realm has id {} set in config.",
            GetRemoteIpAddress().to_string(), authSession->RealmID, realm.Id.Realm);
        DelayedCloseSocket();
        return;
    }

    // Must be done before User is created
    bool wardenActive = sWorld->getBoolConfig(CONFIG_WARDEN_ENABLED);
    if (wardenActive && account.OS != "Win" && account.OS != "OSX")
    {
        SendAuthResponseError(AUTH_REJECT);
        LOG_ERROR("network", "WowConnection::HandleAuthSession: Client {} attempted to log in using invalid client OS ({}).", address, account.OS);
        DelayedCloseSocket();
        return;
    }

    // Check that Key and account name are the same on client and server
    uint8 t[4] = { 0x00,0x00,0x00,0x00 };

    Acore::Crypto::SHA1 sha;
    sha.UpdateData(authSession->Account);
    sha.UpdateData(t);
    sha.UpdateData(authSession->LocalChallenge);
    sha.UpdateData(_authSeed);
    sha.UpdateData(account.SessionKey);
    sha.Finalize();

    if (sha.GetDigest() != authSession->Digest)
    {
        SendAuthResponseError(AUTH_FAILED);
        LOG_ERROR("network", "WowConnection::HandleAuthSession: Authentication failed for account: {} ('{}') address: {}", account.m_accountId, authSession->Account, address);
        DelayedCloseSocket();
        return;
    }

    if (IpLocationRecord const* location = sIPLocation->GetLocationRecord(address))
        _ipCountry = location->CountryCode;

    ///- Re-check ip locking (same check as in auth).
    if (account.IsLockedToIP)
    {
        if (account.LastIP != address)
        {
            SendAuthResponseError(AUTH_FAILED);
            LOG_DEBUG("network", "WowConnection::HandleAuthSession: Sent Auth Response (Account IP differs. Original IP: {}, new IP: {}).", account.LastIP, address);
            // We could log on hook only instead of an additional db log, however action logger is config based. Better keep DB logging as well
            sScriptMgr->OnFailedAccountLogin(account.m_accountId);
            DelayedCloseSocket();
            return;
        }
    }
    else if (!account.LockCountry.empty() && account.LockCountry != "00" && !_ipCountry.empty())
    {
        if (account.LockCountry != _ipCountry)
        {
            SendAuthResponseError(AUTH_FAILED);
            LOG_DEBUG("network", "WowConnection::HandleAuthSession: Sent Auth Response (Account country differs. Original country: {}, new country: {}).", account.LockCountry, _ipCountry);
            // We could log on hook only instead of an additional db log, however action logger is config based. Better keep DB logging as well
            sScriptMgr->OnFailedAccountLogin(account.m_accountId);
            DelayedCloseSocket();
            return;
        }
    }

    //! Negative mutetime indicates amount of minutes to be muted effective on next login - which is now.
    if (account.MuteTime < 0)
    {
        account.MuteTime = GameTime::GetGameTime().count() + std::llabs(account.MuteTime);

        auto* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_MUTE_TIME_LOGIN);
        stmt->SetData(0, account.MuteTime);
        stmt->SetData(1, account.m_accountId);
        LoginDatabase.Execute(stmt);
    }

    if (account.IsBanned)
    {
        SendAuthResponseError(AUTH_BANNED);
        LOG_ERROR("network", "WowConnection::HandleAuthSession: Sent Auth Response (Account banned).");
        sScriptMgr->OnFailedAccountLogin(account.m_accountId);
        DelayedCloseSocket();
        return;
    }

    // Check locked state for server
    AccountTypes allowedAccountType = sWorld->GetPlayerSecurityLimit();
    LOG_DEBUG("network", "Allowed Level: {} Player Level {}", allowedAccountType, account.Security);
    if (allowedAccountType > SEC_PLAYER && account.Security < allowedAccountType)
    {
        SendAuthResponseError(AUTH_UNAVAILABLE);
        LOG_DEBUG("network", "WowConnection::HandleAuthSession: User tries to login but his security level is not enough");
        sScriptMgr->OnFailedAccountLogin(account.m_accountId);
        DelayedCloseSocket();
        return;
    }

    LOG_DEBUG("network", "WowConnection::HandleAuthSession: Client '{}' authenticated successfully from {}.", authSession->Account, address);

    // Update the last_ip in the database as it was successful for login
    stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_LAST_IP);
    stmt->SetData(0, address);
    stmt->SetData(1, authSession->Account);

    LoginDatabase.Execute(stmt);

    // At this point, we can safely hook a successful login
    sScriptMgr->OnAccountLogin(account.m_accountId);

    _authed = true;

    sScriptMgr->OnLastIpUpdate(account.m_accountId, address);

    _worldSession = new User(account.m_accountId, account.m_accountFlags, std::move(authSession->Account), shared_from_this(), account.Security,
        account.Expansion, account.MuteTime, account.Locale, account.Recruiter, account.IsRectuiter, account.Security ? true : false, account.TotalTime);

    _worldSession->ReadAddonsInfo(authSession->AddonInfo);

    // Initialize Warden system only if it is enabled by config
    if (wardenActive)
    {
        _worldSession->InitWarden(account.SessionKey, account.OS);
    }

    sWorld->AddUser(_worldSession);

    AsyncRead();
}

void WowConnection::SendAuthResponseError(uint8 code)
{
    WDataStore packet(SMSG_AUTH_RESPONSE, 1);
    packet << uint8(code);

    SendPacketAndLogOpcode(packet);
}

bool WowConnection::HandlePing(WDataStore& recvPacket)
{
    using namespace std::chrono;

    uint32 ping;
    uint32 latency;

    // Get the ping packet content
    recvPacket >> ping;
    recvPacket >> latency;

    if (_LastPingTime == steady_clock::time_point())
    {
        _LastPingTime = steady_clock::now();
    }
    else
    {
        steady_clock::time_point now = steady_clock::now();
        steady_clock::duration diff = now - _LastPingTime;

        _LastPingTime = now;

        if (diff < seconds(27))
        {
            ++_OverSpeedPings;

            uint32 maxAllowed = sWorld->getIntConfig(CONFIG_MAX_OVERSPEED_PINGS);

            if (maxAllowed && _OverSpeedPings > maxAllowed)
            {
                std::unique_lock<std::mutex> sessionGuard(_worldSessionLock);

                if (_worldSession && AccountMgr::IsPlayerAccount(_worldSession->GetSecurity()))
                {
                    LOG_ERROR("network", "WowConnection::HandlePing: {} kicked for over-speed pings (address: {})",
                        _worldSession->GetPlayerInfo(), GetRemoteIpAddress().to_string());

                    return false;
                }
            }
        }
        else
        {
            _OverSpeedPings = 0;
        }
    }

    {
        std::lock_guard<std::mutex> sessionGuard(_worldSessionLock);

        if (_worldSession)
            _worldSession->SetLatency(latency);
        else
        {
            LOG_ERROR("network", "WowConnection::HandlePing: peer sent CMSG_PING, but is not authenticated or got recently kicked, address = {}", GetRemoteIpAddress().to_string());
            return false;
        }
    }

    WDataStore packet(SMSG_PONG, 4);
    packet << ping;
    SendPacketAndLogOpcode(packet);

    return true;
}

std::map<NETMESSAGE, MSGHANDLER> WowConnection::m_handlers;
std::map<NETMESSAGE, uint32_t> WowConnection::m_handlerPermissions;

int WowConnection::SetMessageHandler(NETMESSAGE msgId, MSGHANDLER handler, uint32_t security /*= DEFAULT_SECURITY*/) {
    if (msgId >= NUM_MSG_TYPES) {
        // TODO: handle error
        return 0;
    }
    if (!handler) {
        // TODO: handle error
        return 0;
    }
    if (m_handlers.contains(msgId)) {
        // TODO: handle error
        return 0;
    }
    m_handlers[msgId] = handler;
    m_handlerPermissions[msgId] = security;
    return 1;
}

int WowConnection::ClearMessageHandler(NETMESSAGE msgId) {
    if (msgId >= NUM_MSG_TYPES) {
        // TODO: handle error
        return 0;
    }
    m_handlers.erase(msgId);
    m_handlerPermissions.erase(msgId);
    return 1;
}
