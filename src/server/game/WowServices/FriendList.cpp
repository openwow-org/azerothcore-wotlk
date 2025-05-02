/******************************************************************************
*
*   FriendList.cpp
*
*
*   Written by Tristan Cormier (3/6/2024)
*
***/


#include "FriendList.h"

#include "CharacterCache.h"
#include "Language.h"
#include "Opcodes.h"
#include "Player.h"
#include "WowConnection.h"

#include <map>


/******************************************************************************
*
*   Defines
*
***/

#define MAX_NAME_SIZE   305
#define MAX_NOTES_SIZE  512


/******************************************************************************
*
*   Private
*
***/

static std::map<WOWGUID, FriendList * > s_friendListMap;
static bool s_initialized;

//=============================================================================
static void AddFriendHandler (
    User *          user,
    NETMESSAGE      msgId,
    uint32_t        eventTime,
    WDataStore *    msg
) {
    char name[MAX_NAME_SIZE];
    msg->GetString(name, sizeof(name));

    char notes[MAX_NOTES_SIZE];
    msg->GetString(notes, sizeof(notes));

    user->AddFriend(name, notes);
}

//=============================================================================
static void AddIgnoreHandler (
    User *          user,
    NETMESSAGE      msgId,
    uint32_t        eventTime,
    WDataStore *    msg
) {
    char name[256];
    msg->GetString(name, sizeof(name));

    FormatCharacterName(name);

    user->AddIgnore(name);
}

//=============================================================================
static void ContactListHandler (
    User *          user,
    NETMESSAGE      msgId,
    uint32_t        eventTime,
    WDataStore *    msg
) {
    uint32_t flags;
    msg->Get(flags);

    user->SendContactList(flags);
}

//=============================================================================
static void DeleteFriendHandler (
    User *          user,
    NETMESSAGE      msgId,
    uint32_t        eventTime,
    WDataStore *    msg
) {
    WOWGUID guid;
    msg >> guid;

    user->RemoveFriend(guid);
}

//=============================================================================
static void DelIgnoreHandler (
    User *          user,
    NETMESSAGE      msgId,
    uint32_t        eventTime,
    WDataStore *    msg
) {
    WOWGUID guid;
    msg >> guid;

    user->DelIgnore(guid);
}

//=============================================================================
static void SetFriendNotesHandler (
    User *          user,
    NETMESSAGE      msgId,
    uint32_t        eventTime,
    WDataStore *    msg
) {
    char notes[MAX_NOTES_SIZE];
    msg->GetString(notes, sizeof(notes));

    WOWGUID guid;
    msg >> guid;

    user->SetFriendNotes(guid, notes);
}

//=============================================================================
static void WhoIsHandler (
    User *          user,
    NETMESSAGE      msgId,
    uint32_t        eventTime,
    WDataStore *    msg
) {
    char buffer[256];
    msg->GetString(buffer, -1);

    FormatCharacterName(buffer);

    // Query the database for the account name of the character
    auto queryResults = LoginDatabase.Query(
        "SELECT username FROM account WHERE id = {}",
        sCharacterCache->GetCharacterAccountIdByName(buffer)
    );

    if (!queryResults)
        strcpy(buffer, "Character not found");
    else
        strncpy(
            buffer,
            queryResults->Fetch()->Get<std::string>().c_str(),  // account name
            sizeof(buffer)
        );

    // Send the response message
    WDataStore outbound(SMSG_WHOIS, strlen(buffer)+1);
    outbound << buffer;
    user->Send(&outbound);
}


/******************************************************************************
*
*   Exports
*
***/

//=============================================================================
void FriendListInitialize () {
    if (s_initialized)
        return;

    // Register message handlers
    WowConnection::SetMessageHandler(CMSG_WHOIS, WhoIsHandler, GM_SECURITY);
    WowConnection::SetMessageHandler(CMSG_CONTACT_LIST, ContactListHandler);
    WowConnection::SetMessageHandler(CMSG_ADD_FRIEND, AddFriendHandler);
    WowConnection::SetMessageHandler(CMSG_DEL_FRIEND, DeleteFriendHandler);
    WowConnection::SetMessageHandler(CMSG_SET_CONTACT_NOTES, SetFriendNotesHandler);
    WowConnection::SetMessageHandler(CMSG_ADD_IGNORE, AddIgnoreHandler);
    WowConnection::SetMessageHandler(CMSG_DEL_IGNORE, DelIgnoreHandler);

    s_initialized = true;
}

//=============================================================================
void FriendListDestroy () {
    if (!s_initialized)
        return;

    // Clear the global FriendList object hash table
    for (auto i = s_friendListMap.begin(); i != s_friendListMap.end(); i++) {
        s_friendListMap.erase(i);
    }

    // Unregister message handlers
    WowConnection::ClearMessageHandler(CMSG_WHOIS);
    WowConnection::ClearMessageHandler(CMSG_CONTACT_LIST);
    WowConnection::ClearMessageHandler(CMSG_ADD_FRIEND);
    WowConnection::ClearMessageHandler(CMSG_DEL_FRIEND);
    WowConnection::ClearMessageHandler(CMSG_SET_CONTACT_NOTES);
    WowConnection::ClearMessageHandler(CMSG_ADD_IGNORE);
    WowConnection::ClearMessageHandler(CMSG_DEL_IGNORE);

    s_initialized = false;
}


//=============================================================================
void FriendList::Friend::SetName (char const * name) {
    if (!name)
        name = "Unknown";

    m_name = (char * )realloc(m_name, strlen(name)+1);
    strcpy(m_name, name);
}

//=============================================================================
void FriendList::Friend::SetNotes (char const * notes) {
    if (!notes)
        notes = "";

    m_notes = (char * )realloc(m_notes, strlen(notes)+1);
    strcpy(m_notes, notes);
}

//=============================================================================
FriendList::Friend::~Friend () {
    if (m_name)
        FREE(m_name);
    if (m_notes)
        FREE(m_notes);
}


//=============================================================================
FRIEND_RESULT FriendList::AddFriend (Friend & frnd) {
    FRIEND_RESULT res;

    if (m_playerPtr->GetGUID() == frnd.m_GUID)
        res = FRIEND_SELF;
    else if (GetFriend(frnd.m_GUID))
        res = FRIEND_ALREADY;
    else if (GetNumFriends() < NUM_MAX_FRIENDS) {
        res = frnd.m_status == FRIEND_STATUS_ONLINE ? FRIEND_ADDED_ONLINE : FRIEND_ADDED_OFFLINE;
        m_friends.push_back(frnd);
        SaveContact(frnd.m_GUID, frnd.m_flags, frnd.m_notes);
    }
    else
      res = FRIEND_LIST_FULL;

    return res;
}

//=============================================================================
FRIEND_RESULT FriendList::RemoveFriend (WOWGUID const & guid) {
    for (auto pFriend = m_friends.begin(); pFriend != m_friends.end(); pFriend++) {
        if (pFriend->m_GUID == guid) {
            m_friends.erase(pFriend);
            return FRIEND_REMOVED;
        }
    }
    return FRIEND_NOT_FOUND;
}

//=============================================================================
FriendList::Friend const * FriendList::GetFriend (WOWGUID const & guid) {
    for (auto i = m_friends.begin(); i != m_friends.end(); i++) {
        if (i->m_GUID == guid) {
            return &*i;
        }
    }
    return nullptr;
}

//=============================================================================
void FriendList::SetFriendNotes (WOWGUID const & guid, char const * notes) {
    for (auto pFriend = m_friends.begin(); pFriend != m_friends.end(); pFriend++) {
        if (pFriend->m_GUID == guid) {
            // Allocate bytes for the notes
            // or null terminate the buffer
            if (notes && * notes) {
                pFriend->m_notes = (char*)realloc(pFriend->m_notes, strlen(notes)+1);
                strcpy(pFriend->m_notes, notes);
            }
            else {
                * pFriend->m_notes = '\0';
            }

            // Save the notes to the database
            CharacterDatabase.Execute(
                "UPDATE character_social "
                "SET note = '{}' "
                "WHERE friend = {} AND guid = {}",
                pFriend->m_notes,
                pFriend->m_GUID.GetCounter(),
                m_playerPtr->GetGUID().GetCounter()
            );
            break;
        }
    }
}

//=============================================================================
uint32_t FriendList::GetNumFriends () {
    return static_cast<uint32_t>(m_friends.size());
}


//=============================================================================
FRIEND_RESULT FriendList::AddIgnore (WOWGUID const & guid) {
    uint32_t numIgnores = GetNumIgnores();
    if (numIgnores == NUM_MAX_IGNORE)
        return FRIEND_IGNORE_FULL;

    uint32_t i = 0;
    while (i < numIgnores) {
        if (m_ignore[i] == guid)
            return FRIEND_IGNORE_ALREADY;
        if (m_ignore[i] == m_playerPtr->GetGUID())
            return FRIEND_IGNORE_SELF;
        ++i;
    }

    m_ignore[i] = guid;

    // Save the ignored contact to the database
    CharacterDatabase.Execute(
        "INSERT INTO character_social (guid, friend, flags) "
        "VALUES ({}, {}, {})",
        m_playerPtr->GetGUID().GetCounter(),
        guid.GetCounter(),
        CONTACT_IGNORED
    );

    return FRIEND_IGNORE_ADDED;
}

//=============================================================================
FRIEND_RESULT FriendList::DelIgnore (WOWGUID const & guid) {
    for (uint32_t i = 0; i < GetNumIgnores(); i++) {
        if (m_ignore[i] == guid) {
            m_ignore[i] = WOWGUID();

            // Delete the ignored contact from the database
            CharacterDatabase.Execute(
                "DELETE FROM character_social "
                "WHERE guid = {} AND friend = {}",
                m_playerPtr->GetGUID().GetCounter(),
                guid.GetCounter()
            );

            return FRIEND_IGNORE_REMOVED;
        }
    }

    return FRIEND_IGNORE_NOT_FOUND;;
}

//=============================================================================
bool FriendList::IsIgnored (WOWGUID const & guid) {
    for (uint32_t i = 0; i < GetNumIgnores(); i++) {
        if (m_ignore[i] == guid)
            return true;
    }

    return false;
}

//=============================================================================
uint32_t FriendList::GetNumIgnores () {
    uint32_t result = 0;
    while (result < NUM_MAX_IGNORE) {
        if (!m_ignore[result])
            break;

      ++result;
    }

    return result;
}


//=============================================================================
uint32_t FriendList::GetNumMutes () {
    uint32_t result = 0;
    while (result < NUM_MAX_IGNORE) {
        if (!m_mute[result])
            break;

        ++result;
    }

    return result;
}


//=============================================================================
void FriendList::AddContacts () {
    ASSERT(m_playerPtr);

    auto results = CharacterDatabase.Query(
        "SELECT friend, flags, note "
        "FROM character_social "
        "WHERE guid = '{}' "
        "ORDER BY flags",
        m_playerPtr->GetGUID().GetCounter()
    );

    if (!results)
        return;

    // Fetch the query results
    Field * fields = results->Fetch();

    uint32_t numContacts = results->GetRowCount();

    // Initialize indexes
    uint32_t iFriends = 0;
    uint32_t  iIgnore = 0;
    uint32_t    iMute = 0;

    uint32_t flags = CONTACT_FRIEND | CONTACT_IGNORED | CONTACT_MUTED;

    // Prepare the response message
    WDataStore msg(SMSG_CONTACT_LIST);
    msg << flags;
    msg << numContacts;

    // Write data to the message buffer for every contact
    for (uint32_t i = 0; i < numContacts; i++) {
        WOWGUID guid = WOWGUID(HighGuid::Player, fields[0].Get<uint32_t>());
        flags = fields[1].Get<uint8_t>();

        msg << guid;
        msg << flags;

        if ((flags & CONTACT_FRIEND) != 0 && iFriends < NUM_MAX_FRIENDS) {
            Friend frnd;
            frnd.m_GUID = guid;
            frnd.m_flags = flags;
            frnd.SetNotes(fields[2].Get<std::string>().c_str());

            msg << frnd.m_notes;

            // Check if this friend is online
            if (Player * plr = ObjectAccessor::FindConnectedPlayer(guid)) {
                frnd.SetName(plr->GetName().c_str());

                if (plr->isAFK())
                    frnd.m_status = FRIEND_STATUS_AFK;
                else if (plr->isDND())
                    frnd.m_status = FRIEND_STATUS_DND;
                else
                    frnd.m_status = FRIEND_STATUS_ONLINE;

                frnd.m_areaId = plr->GetAreaId();
                frnd.m_level = plr->GetLevel();
                frnd.m_classId = plr->GetClass();
          }

          msg << frnd.m_status;

          // Send player info for this friend if they are online
          if (frnd.m_status != FRIEND_STATUS_OFFLINE) {
              msg << frnd.m_areaId;
              msg << frnd.m_level;
              msg << frnd.m_classId;
          }

          m_friends.push_back(frnd);
          iFriends++;
        }
        else if ((flags & CONTACT_IGNORED) != 0 && iIgnore < NUM_MAX_IGNORE) {
            m_ignore[iIgnore++] = guid;
        }
        else if ((flags & CONTACT_MUTED) != 0 && iMute < NUM_MAX_MUTE) {
            m_mute[iMute++] = guid;
        }

        // Fetch the next row of results
        results->NextRow();
    }

    // Send the response message
    m_playerPtr->User()->Send(&msg);
}

//=============================================================================
void FriendList::SaveContact (WOWGUID const & guid, uint32_t flags, const char * notes) {
    uint32_t loFriend = guid.GetCounter();
    uint32_t loGuid = m_playerPtr->GetGUID().GetCounter();

    CharacterDatabase.Execute(
        "REPLACE INTO character_social "
        "(guid, friend, flags, note) "
        "VALUES ({}, {}, {}, '{}')",
        loGuid,
        loFriend,
        flags,
        notes ? notes : ""
    );
}


//=============================================================================
FriendList::FriendList (Player * plr) : m_playerPtr(plr) {
    ASSERT(plr);

    // Add this FriendList instance to the global hash table
    s_friendListMap[plr->GetGUID()] = this;

    // Add contacts from the database
    AddContacts();

    // Send a FRIEND_STATUS_ONLINE message
    // to all users that have this player on their friends list
    for (auto it = s_friendListMap.begin(); it != s_friendListMap.end(); it++) {
        if (it->second != this && it->second->GetFriend(plr->GetGUID())) {
            WDataStore msg(SMSG_FRIEND_STATUS);
            msg << static_cast<uint8_t>(FRIEND_ONLINE);
            msg << plr->GetGUID();
            msg << static_cast<uint8_t>(FRIEND_STATUS_ONLINE);
            msg << plr->GetAreaId();
            msg << plr->GetLevel();
            msg << plr->GetClass();
            it->second->m_playerPtr->User()->Send(&msg);
        }
    }
}

//=============================================================================
FriendList::~FriendList () {
    // Save friends to the database
    // and notify every user with this character on their friends list that he just disconnected
    for (auto pFriend = m_friends.begin(); pFriend != m_friends.end(); pFriend++) {
        SaveContact(pFriend->m_GUID, CONTACT_FRIEND, pFriend->m_notes);
        if (FriendList * friendList = s_friendListMap.at(pFriend->m_GUID)) {
            if (friendList->GetFriend(m_playerPtr->GetGUID())) {
                friendList->m_playerPtr->User()->SendFriendStatus(
                    FRIEND_OFFLINE,
                    m_playerPtr->GetGUID()
                );
            }
        }
    }

    // Save ignored contacts to the database
    for (uint32_t i = 0; i < GetNumIgnores(); i++) {
        SaveContact(m_ignore[i], CONTACT_IGNORED, "");
    }

    // Save muted contacts to the database
    for (uint32_t i = 0; i < GetNumMutes(); i++) {
        SaveContact(m_mute[i], CONTACT_MUTED, "");
    }

    // Remove this FriendList object reference from the global hash table
    s_friendListMap.erase(m_playerPtr->GetGUID());
}
