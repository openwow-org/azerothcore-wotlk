#include "FriendList.h"

//#include "../../game/Server/WorldSocket.h"
#include <WorldSocket.h>
//#include "../../game/Server/Protocol/Opcodes.h"
#include <Opcodes.h>

static bool s_initialized;

//===========================================================================
static int FriendListStatusHandler(WorldSession* ses, uint32_t msgId, uint32_t eventTime, WorldPacket* msg)
{
    ses->SendNotification("Hello from %s",__FUNCTION__);
    return 0;
}

//===========================================================================
void FriendList::Initialize()
{
    if (s_initialized) {
        // TODO: handle error
        return;
    }
    WorldSocket::SetMessageHandler(CMSG_ADD_FRIEND, FriendListStatusHandler);
}

