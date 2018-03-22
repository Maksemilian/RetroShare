/*
 * libretroshare/src/gxs: rsgxsnettunnel.cc
 *
 * General Data service, interface for RetroShare.
 *
 * Copyright 2018-2018 by Cyril Soler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License Version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA.
 *
 * Please report all bugs and problems to "retroshare.project@gmail.com"
 *
 */

#include "util/rsdir.h"
#include "retroshare/rspeers.h"
#include "rsgxsnettunnel.h"

#define DEBUG_RSGXSNETTUNNEL 1

#define NOT_IMPLEMENTED() { std::cerr << __PRETTY_FUNCTION__ << ": not yet implemented." << std::endl; }

RsGxsNetTunnelService::RsGxsNetTunnelService(): mGxsNetTunnelMtx("GxsNetTunnel") {}

//===========================================================================================================================================//
//                                                     Interface with rest of the software                                                   //
//===========================================================================================================================================//

bool RsGxsNetTunnelService::manage(const RsGxsGroupId& group_id)
{
	RsFileHash hash = calculateGroupHash(group_id) ;

	RsStackMutex stack(mGxsNetTunnelMtx); /********** STACK LOCKED MTX ******/

    RsGxsNetTunnelGroupInfo& info(mGroups[group_id]) ;

    time_t now = time(NULL) ;

	if(info.group_status == RsGxsNetTunnelGroupInfo::RS_GXS_NET_TUNNEL_GRP_STATUS_VPIDS_AVAILABLE)
		return true;

    info.hash = hash ;
    info.last_contact = now ;
    info.group_status = RsGxsNetTunnelGroupInfo::RS_GXS_NET_TUNNEL_GRP_STATUS_TUNNELS_REQUESTED;

	mHandledHashes[hash] = group_id ;

#ifdef DEBUG_GXS_TUNNEL
    std::cerr << "Starting distant chat to " << to_gxs_id << ", hash = " << hash << ", from " << from_gxs_id << std::endl;
    std::cerr << "Asking turtle router to monitor tunnels for hash " << hash << std::endl;
#endif

    // Now ask the turtle router to manage a tunnel for that hash.

    mTurtle->monitorTunnels(hash,this,false) ;

	return true;
}

bool RsGxsNetTunnelService::release(const RsGxsGroupId& group_id)
{
	RsStackMutex stack(mGxsNetTunnelMtx); /********** STACK LOCKED MTX ******/

	// Here we need to clean the stuff that was created by this group id.

	auto it = mGroups.find(group_id) ;

	if(it == mGroups.end())
	{
		std::cerr << "RsGxsNetTunnelService::release(): Weird. Cannot release client group " << group_id << " that is not known." << std::endl;
		return false ;
	}

	mGroups.erase(it) ;

	RsFileHash hash = calculateGroupHash(group_id) ;

	mHandledHashes.erase(hash) ;
	return true ;
}

class ItemAutoDelete
{
public:
	ItemAutoDelete(RsItem *& item) : mItem(item) {}
	~ItemAutoDelete() { delete mItem; mItem=NULL ; }
	RsItem *& mItem;
};

bool RsGxsNetTunnelService::sendItem(RsItem *& item,const RsGxsNetTunnelVirtualPeerId& virtual_peer)
{
	// The item is serialized and encrypted using chacha20+SHA256, using the generic turtle encryption, and then sent to the turtle router.

	ItemAutoDelete iad(item) ;	// This ensures the item is deleted whatsoever when leaving

	// 1 - find the virtual peer and the proper master key to encrypt with, and check that all the info is known

	auto it = mVirtualPeers.find(virtual_peer) ;

	if(it == mVirtualPeers.end())
	{
		std::cerr << "(EE) RsGxsNetTunnelService::sendData(): cannot find virtual peer " << virtual_peer << ". Data is dropped." << std::endl;
		return false ;
	}

	auto it2 = mGroups.find(it->second.first);

	if(it2 == mGroups.end())
	{
		std::cerr << "(EE) RsGxsNetTunnelService::sendData(): cannot find virtual peer " << virtual_peer << ". Data is dropped." << std::endl;
		return false ;
	}

	auto it3 = it2->second.virtual_peers.find(it->second.second);

	if(it3 == it2->second.virtual_peers.end())
	{
		std::cerr << "(EE) RsGxsNetTunnelService::sendData(): cannot find turtle virtual peer " << it->second.second << ". Data is dropped." << std::endl;
		return false ;
	}

	if(it3->second.vpid_status != RsGxsNetTunnelVirtualPeerInfo::RS_GXS_NET_TUNNEL_VP_STATUS_ACTIVE)
	{
		std::cerr << "(EE) RsGxsNetTunnelService::sendData(): virtual peer " << it->second.second << " is not active. Data is dropped." << std::endl;
		return false ;
	}

    // 2 - encrypt and send the item.

	RsTurtleGenericDataItem *encrypted_turtle_item = NULL ;

	NOT_IMPLEMENTED();
	// if(!p3Turtle::encryptItem(item,it3->second.encryption_master_key,encrypted_turtle_item))
	// {
	// 	std::cerr << "(EE) RsGxsNetTunnelService::sendData(): cannot encrypt. Something's wrong. Data is dropped." << std::endl;
	// 	return false ;
	// }

	mTurtle->sendTurtleData(it->second.second,encrypted_turtle_item) ;

	return true ;
}

bool RsGxsNetTunnelService::getVirtualPeers(const RsGxsGroupId&, std::list<RsPeerId>& peers)
{
	// returns the virtual peers for this group
	NOT_IMPLEMENTED();
	return false ;
}

RsGxsNetTunnelVirtualPeerId RsGxsNetTunnelService::makeServerVirtualPeerIdForGroup(const RsGxsGroupId& group_id) const
{
	assert(RsPeerId::SIZE_IN_BYTES <= Sha1CheckSum::SIZE_IN_BYTES) ;

	// We compute sha1( SSL_id | group_id | mRandomBias ) and trunk it to 16 bytes in order to compute a RsPeerId

	RsPeerId ssl_id = rsPeers->getOwnId() ;

	unsigned char mem[RsPeerId::SIZE_IN_BYTES + RsGxsGroupId::SIZE_IN_BYTES + RS_GXS_TUNNEL_CONST_RANDOM_BIAS_SIZE];

	memcpy(mem                                                    ,ssl_id.toByteArray()  ,RsPeerId::SIZE_IN_BYTES) ;
	memcpy(mem+RsPeerId::SIZE_IN_BYTES                            ,group_id.toByteArray(),RsGxsGroupId::SIZE_IN_BYTES) ;
	memcpy(mem+RsPeerId::SIZE_IN_BYTES+RsGxsGroupId::SIZE_IN_BYTES,mRandomBias           ,RS_GXS_TUNNEL_CONST_RANDOM_BIAS_SIZE) ;

	return RsGxsNetTunnelVirtualPeerId(RsDirUtil::sha1sum(mem,RsPeerId::SIZE_IN_BYTES+RsGxsGroupId::SIZE_IN_BYTES+RS_GXS_TUNNEL_CONST_RANDOM_BIAS_SIZE).toByteArray());
}

void RsGxsNetTunnelService::dump() const
{
	RS_STACK_MUTEX(mGxsNetTunnelMtx);

	static std::string group_status_str[3] = {
	    std::string("[RS_GXS_NET_TUNNEL_GRP_STATUS_UNKNOWN          ]"),
	    std::string("[RS_GXS_NET_TUNNEL_GRP_STATUS_TUNNELS_REQUESTED]"),
	    std::string("[RS_GXS_NET_TUNNEL_GRP_STATUS_VPIDS_AVAILABLE  ]")
	};

	static std::string vpid_status_str[3] = {
	    	 std::string("[RS_GXS_NET_TUNNEL_VP_STATUS_UNKNOWN   ]"),
		     std::string("[RS_GXS_NET_TUNNEL_VP_STATUS_TUNNEL_OK ]"),
		     std::string("[RS_GXS_NET_TUNNEL_VP_STATUS_ACTIVE    ]")
	};

	std::cerr << "GxsNetTunnelService dump: " << std::endl;
	std::cerr << "Managed GXS groups: " << std::endl;

	for(auto it(mGroups.begin());it!=mGroups.end();++it)
	{
		std::cerr << "  " << it->first << " hash: " << it->second.hash << "  status: " << group_status_str[it->second.group_status] << "]  Last contact: " << time(NULL) - it->second.last_contact << " secs ago" << std::endl;

		for(auto it2(it->second.virtual_peers.begin());it2!=it->second.virtual_peers.end();++it2)
			std::cerr << "    turtle:" << it2->first << "  status: " <<  vpid_status_str[it2->second.vpid_status] << " s: "
			          << (int)it2->second.side << " last seen " << time(NULL)-it2->second.last_contact
			          << " ekey: " << RsUtil::BinToHex(it2->second.encryption_master_key,RS_GXS_TUNNEL_CONST_EKEY_SIZE)
			          << " pending (" << it2->second.incoming_items.size() << "," << it2->second.outgoing_items.size() << ")" << std::endl;
	}

	std::cerr << "Virtual peers: " << std::endl;
	for(auto it(mVirtualPeers.begin());it!=mVirtualPeers.end();++it)
		std::cerr << "  GXS Peer:" << it->first << "  group_id: " << it->second.first << " Turtle:" << it->second.second << std::endl;

	std::cerr << "Hashes: " << std::endl;
	for(auto it(mHandledHashes.begin());it!=mHandledHashes.end();++it)
		std::cerr << "   hash: " << it->first << " GroupId: " << it->second << std::endl;
}

//===========================================================================================================================================//
//                                                         Interaction with Turtle Router                                                    //
//===========================================================================================================================================//

void RsGxsNetTunnelService::connectToTurtleRouter(p3turtle *tr)
{
	mTurtle = tr ;
	mTurtle->registerTunnelService(this) ;
}

bool RsGxsNetTunnelService::handleTunnelRequest(const RsFileHash &hash,const RsPeerId& peer_id)
{
	NOT_IMPLEMENTED();

	// at this point we need to talk to the client services
	// There's 2 ways to do that:
	//    1 - client services "register" and we ask them one by one.
	//    2 - client service derives from RsGxsNetTunnelService and the client is interrogated using an overloaded virtual method

	return true ;
}
void RsGxsNetTunnelService::receiveTurtleData(RsTurtleGenericTunnelItem *item,const RsFileHash& hash,const RsPeerId& virtual_peer_id,RsTurtleGenericTunnelItem::Direction direction)
{
	NOT_IMPLEMENTED();
}
void RsGxsNetTunnelService::addVirtualPeer(const TurtleFileHash& hash, const TurtleVirtualPeerId& vpid,RsTurtleGenericTunnelItem::Direction dir)
{
	auto it = mHandledHashes.find(hash) ;

	if(it == mHandledHashes.end())
	{
		std::cerr << "RsGxsNetTunnelService::addVirtualPeer(): error! hash " << hash << " is not handled. Cannot add vpid " << vpid << " in direction " << dir << std::endl;
		return ;
	}

	const RsGxsGroupId group_id(it->second) ;

	RsGxsNetTunnelGroupInfo& ginfo( mGroups[group_id] ) ;
	ginfo.group_status = RsGxsNetTunnelGroupInfo::RS_GXS_NET_TUNNEL_GRP_STATUS_VPIDS_AVAILABLE ;

	RsGxsNetTunnelVirtualPeerInfo& vpinfo( ginfo.virtual_peers[vpid] ) ;

	vpinfo.vpid_status = RsGxsNetTunnelVirtualPeerInfo::RS_GXS_NET_TUNNEL_VP_STATUS_TUNNEL_OK ;
	vpinfo.net_service_virtual_peer.clear();
	vpinfo.side = dir ;
	vpinfo.last_contact = time(NULL) ;

	generateEncryptionKey(group_id,vpid,vpinfo.encryption_master_key );
}

void RsGxsNetTunnelService::removeVirtualPeer(const TurtleFileHash& hash, const TurtleVirtualPeerId& vpid)
{
	auto it = mHandledHashes.find(hash) ;

	if(it == mHandledHashes.end())
	{
		std::cerr << "RsGxsNetTunnelService::removeVirtualPeer(): error! hash " << hash << " is not handled. Cannot remove vpid " << vpid << std::endl;
		return ;
	}

	const RsGxsGroupId group_id(it->second) ;

	RsGxsNetTunnelGroupInfo& ginfo( mGroups[group_id] ) ;

	ginfo.virtual_peers.erase(vpid);

	if(ginfo.virtual_peers.empty())
		ginfo.group_status = RsGxsNetTunnelGroupInfo::RS_GXS_NET_TUNNEL_GRP_STATUS_TUNNELS_REQUESTED ;
}

RsFileHash RsGxsNetTunnelService::calculateGroupHash(const RsGxsGroupId& group_id) const
{
	return RsDirUtil::sha1sum(group_id.toByteArray(),RsGxsGroupId::SIZE_IN_BYTES) ;
}

void RsGxsNetTunnelService::generateEncryptionKey(const RsGxsGroupId& group_id,const TurtleVirtualPeerId& vpid,unsigned char key[RS_GXS_TUNNEL_CONST_EKEY_SIZE]) const
{
	// The key is generated as H(group_id | vpid)
	// Because group_id is not known it shouldn't be possible to recover the key by observing the traffic.

	assert(Sha256CheckSum::SIZE_IN_BYTES == 32) ;

	unsigned char mem[RsGxsGroupId::SIZE_IN_BYTES + TurtleVirtualPeerId::SIZE_IN_BYTES] ;

	memcpy(mem                            ,group_id.toByteArray(),RsGxsGroupId::SIZE_IN_BYTES) ;
	memcpy(mem+RsGxsGroupId::SIZE_IN_BYTES,vpid.toByteArray()    ,TurtleVirtualPeerId::SIZE_IN_BYTES) ;

	memcpy( key, RsDirUtil::sha256sum(mem,RsGxsGroupId::SIZE_IN_BYTES+TurtleVirtualPeerId::SIZE_IN_BYTES).toByteArray(), RS_GXS_TUNNEL_CONST_EKEY_SIZE ) ;
}

//===========================================================================================================================================//
//                                                               Service parts                                                               //
//===========================================================================================================================================//

#ifdef TODO
void RsGxsNetTunnelService::handleIncomingItem(const RsGxsTunnelId& tunnel_id,RsGxsTunnelItem *item)
{
    if(item == NULL)
	    return ;

    // We have 3 things to do:
    //
    // 1 - if it's a data item, send an ACK
    // 2 - if it's an ack item, mark the item as properly received, and remove it from the queue
    // 3 - if it's a status item, act accordingly.

    switch(item->PacketSubType())
    {

    case RS_PKT_SUBTYPE_GXS_TUNNEL_DATA:		handleRecvTunnelDataItem(tunnel_id,dynamic_cast<RsGxsTunnelDataItem*>(item)) ;
	    break ;

    case RS_PKT_SUBTYPE_GXS_TUNNEL_DATA_ACK:	handleRecvTunnelDataAckItem(tunnel_id,dynamic_cast<RsGxsTunnelDataAckItem*>(item)) ;
	    break ;

    case RS_PKT_SUBTYPE_GXS_TUNNEL_STATUS:		handleRecvStatusItem(tunnel_id,dynamic_cast<RsGxsTunnelStatusItem*>(item)) ;
	    break ;

    default:
	    std::cerr << "(EE) impossible situation. DH items should be handled at the service level" << std::endl;
    }

    delete item ;
}
#endif

