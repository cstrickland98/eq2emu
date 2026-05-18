/*  
    EQ2Emulator:  Everquest II Server Emulator
    Copyright (C) 2007  EQ2EMulator Development Team (http://www.eq2emulator.net)

    This file is part of EQ2Emulator.

    EQ2Emulator is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    EQ2Emulator is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with EQ2Emulator.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include "EQPacket.h"
#include "misc.h"
#include "op_codes.h"
#include "CRC16.h"
#include "opcodemgr.h"
#include "packet_dump.h"
#include <map>
#include "Log.h"
#include <time.h>

using namespace std;
extern map<int16,OpcodeManager*>EQOpcodeManager;

#if !defined(LOGIN)
struct Client546RootOpcodeMap {
	EmuOpcode opcode_name;
	int16 opcode;
};

static const Client546RootOpcodeMap client546RootOpcodeMap[] = {
	{ OP_ZoneInfoMsg, 29 },
	{ OP_DoneSendingInitialEntitiesMsg, 31 },
	{ OP_DoneLoadingZoneResourcesMsg, 32 },
	{ OP_DoneLoadingUIResourcesMsg, 33 },
	{ OP_PredictionUpdateMsg, 34 },
	{ OP_SetRemoteCmdsMsg, 35 },
	{ OP_RemoteCmdMsg, 36 },
	{ OP_GameWorldTimeMsg, 37 },
	{ OP_RequestCampMsg, 42 },
	{ OP_CampStartedMsg, 43 },
	{ OP_CampAbortedMsg, 44 },
	{ OP_WhoQueryReplyMsg, 46 },
	{ OP_ClientCmdMsg, 50 },
	{ OP_UpdateTargetMsg, 53 },
	{ OP_UpdateCharacterSheetMsg, 55 },
	{ OP_UpdateSpellBookMsg, 56 },
	{ OP_UpdateInventoryMsg, 58 },
	{ OP_AfterInvSpellUpdate, 59 },
	{ OP_UpdateRecipeBookMsg, 60 },
	{ OP_RequestRecipeDetailsMsg, 61 },
	{ OP_RecipeDetailsMsg, 62 },
	{ OP_UpdateSkillBookMsg, 63 },
	{ OP_UpdateOpportunityMsg, 65 },
	{ OP_ChangeZoneMsg, 67 },
	{ OP_TeleportWithinZoneMsg, 69 },
	{ OP_TeleportWithinZoneNoReloadMsg, 70 },
	{ OP_ReadyToZoneMsg, 73 },
	{ OP_SendLatestRequestMsg, 86 },
	{ OP_SetSocialMsg, 88 },
	{ OP_ClearDataMsg, 89 },
	{ OP_ESZoneInstanceStatusMsg, 90 },
	{ OP_ZonesStatusMsg, 94 },
	{ OP_DialogSelectMsg, 95 },
	{ OP_DialogCloseMsg, 96 },
	{ OP_QuestJournalOpenMsg, 99 },
	{ OP_QuestJournalInspectMsg, 100 },
	{ OP_QuestJournalSetVisibleMsg, 104 },
	{ OP_QuestJournalWaypointMsg, 105 },
	{ OP_GuildUpdateMsg, 110 },
	{ OP_UpdateHouseAccessDataMsg, 120 },
	{ OP_PlayerHouseBaseScreenMsg, 121 },
	{ OP_PlayerHousePurchaseScreenMsg, 122 },
	{ OP_PlayerHouseAccessUpdateMsg, 123 },
	{ OP_BuyPlayerHouseMsg, 128 },
	{ OP_EnterHouseMsg, 132 },
	{ OP_ExitHouseMsg, 133 },
	{ OP_HouseDefaultAccessSetMsg, 134 },
	{ OP_HouseAccessSetMsg, 135 },
	{ OP_HouseAccessRemoveMsg, 136 },
	{ OP_PayHouseUpkeepMsg, 137 },
	{ OP_HouseItemsList, 410 },
	{ OP_MoveableObjectPlacementCriteri, 138 },
	{ OP_EnterMoveObjectModeMsg, 139 },
	{ OP_PositionMoveableObject, 140 },
	{ OP_CancelMoveObjectModeMsg, 141 },
	{ OP_CustomizationReplyMsg, 147 },
	{ OP_TintWidgetsMsg, 148 },
	{ OP_UISettingsResponseMsg, 151 },
	{ OP_KeymapLoadMsg, 153 },
	{ OP_KeymapNoneMsg, 154 },
	{ OP_KeymapDataMsg, 155 },
	{ OP_EntityVerbsRequestMsg, 159 },
	{ OP_EntityVerbsReplyMsg, 160 },
	{ OP_EntityVerbsVerbMsg, 161 },
	{ OP_ChatRelationshipUpdateMsg, 162 },
	{ OP_LootItemsRequestMsg, 163 },
	{ OP_StoppedLootingMsg, 164 },
	{ OP_SitMsg, 165 },
	{ OP_StandMsg, 166 },
	{ OP_SatMsg, 167 },
	{ OP_StoodMsg, 168 },
	{ OP_ClearForTakeOffMsg, 169 },
	{ OP_ReadyForTakeOffMsg, 170 },
	{ OP_DefaultGroupOptionsRequestMsg, 175 },
	{ OP_DefaultGroupOptionsMsg, 176 },
	{ OP_DisplayGroupOptionsScreenMsg, 178 },
	{ OP_DisplayInnVisitScreenMsg, 179 },
	{ OP_PerformPlayerKnockbackMsg, 187 },
	{ OP_PerformCameraShakeMsg, 188 },
	{ OP_PopulateSkillMapsMsg, 189 },
	{ OP_SignalMsg, 191 },
	{ OP_ShowCreateFromRecipeUIMsg, 192 },
	{ OP_CancelCreateFromRecipeMsg, 193 },
	{ OP_BeginItemCreationMsg, 194 },
	{ OP_StopItemCreationMsg, 195 },
	{ OP_ShowItemCreationProcessUIMsg, 196 },
	{ OP_UpdateItemCreationProcessUIMsg, 197 },
	{ OP_DisplayTSEventReactionMsg, 198 },
	{ OP_LsClientAlertlogReplyMsg, 216 },
	{ OP_LsClientVerifylogReplyMsg, 217 },
	{ OP_UpdateClientPredFlagsMsg, 219 },
	{ OP_CreateBoatTransportsMsg, 224 },
	{ OP_ExamineInfoRequestMsg, 229 },
	{ OP_QuickbarInitMsg, 230 },
	{ OP_QuickbarUpdateMsg, 231 },
	{ OP_MacroInitMsg, 232 },
	{ OP_MacroUpdateMsg, 233 },
	{ OP_LevelChangedMsg, 235 },
	{ OP_DisplayWarningMsg, 236 },
	{ OP_EncounterBrokenMsg, 237 },
	{ OP_OnscreenMsgMsg, 238 },
	{ OP_GuildEventAddMsg, 242 },
	{ OP_GuildEventActionMsg, 243 },
	{ OP_GuildEventListMsg, 244 },
	{ OP_GuildBankUpdateMsg, 252 },
	{ OP_GuildBankEventListMsg, 253 },
	{ OP_RewardPackMsg, 255 },
	{ OP_ChatFiltersMsg, 269 },
	{ OP_MailSendMessageMsg, 272 },
	{ OP_MailGetHeadersReplyMsg, 274 },
	{ OP_MailGetMessageReplyMsg, 275 },
	{ OP_MailSendMessageReplyMsg, 276 },
	{ OP_WaypointReplyMsg, 281 },
	{ OP_WaypointSelectMsg, 282 },
	{ OP_WaypointUpdateMsg, 283 },
	{ OP_CharNameChangedMsg, 284 },
	{ OP_ShowZoneTeleporterDestinations, 285 },
	{ OP_SelectZoneTeleporterDestinatio, 286 },
	{ OP_ReloadLocalizedTxtMsg, 287 },
	{ OP_RequestGuildMembershipMsg, 288 },
	{ OP_GuildMembershipResponseMsg, 289 },
	{ OP_LeaveGuildNotifyMsg, 290 },
	{ OP_JoinGuildNotifyMsg, 291 },
	{ OP_AvatarUpdateMsg, 292 },
	{ OP_BioUpdateMsg, 293 },
	{ OP_CsCategoryRequestMsg, 298 },
	{ OP_CsCategoryResponseMsg, 299 },
	{ OP_KnowledgeWindowSlotMappingMsg, 300 },
	{ OP_LFGUpdateMsg, 301 },
	{ OP_PromoFlagsDetailsMsg, 306 },
	{ OP_ConsignViewGetPageMsg, 308 },
	{ OP_ConsignViewReleaseMsg, 309 },
	{ OP_ConsignRemoveItemsMsg, 310 },
	{ OP_UpdateDebugRadiiMsg, 311 },
	{ OP_ReportMsg, 313 },
	{ OP_UpdateRaidMsg, 314 },
	{ OP_ConsignViewSortMsg, 316 },
	{ OP_TitleUpdateMsg, 317 },
	{ OP_ClientFellMsg, 318 },
	{ OP_ClientInDeathRegionMsg, 319 },
	{ OP_CampClientMsg, 320 },
	{ OP_CSToolAccessResponseMsg, 321 },
	{ OP_DeleteGuildMsg, 322 },
	{ OP_TrackingUpdateMsg, 323 },
	{ OP_BeginTrackingMsg, 324 },
	{ OP_StopTrackingMsg, 325 },
	{ OP_AdvancementRequestMsg, 327 },
	{ OP_MapFogDataInitMsg, 328 },
	{ OP_MapFogDataUpdateMsg, 329 },
	{ OP_CloseGroupInviteWindowMsg, 330 },
	{ OP_MailEventNotificationMsg, 333 },
	{ OP_OfferQuestMsg, 334 },
	{ OP_DisplayMailScreenMsg, 336 },
	{ OP_FlightPathsMsg, 345 },
	{ OP_AuctionCharacterReply, 366 },
};

static bool GetClient546RootVeTypeOpcode(EmuOpcode opcode_name, int16* opcode) {
	if (!opcode)
		return false;

	const int map_size = sizeof(client546RootOpcodeMap) / sizeof(client546RootOpcodeMap[0]);
	for (int i = 0; i < map_size; ++i) {
		if (client546RootOpcodeMap[i].opcode_name == opcode_name) {
			*opcode = client546RootOpcodeMap[i].opcode;
			return true;
		}
	}

	return false;
}

static bool GetClient546RootVeTypeEmuOpcode(uint16 opcode, EmuOpcode* opcode_name) {
	if (!opcode_name)
		return false;

	const int map_size = sizeof(client546RootOpcodeMap) / sizeof(client546RootOpcodeMap[0]);
	for (int i = 0; i < map_size; ++i) {
		if (client546RootOpcodeMap[i].opcode == opcode) {
			*opcode_name = client546RootOpcodeMap[i].opcode_name;
			return true;
		}
	}

	return false;
}
#else
static bool GetClient546RootVeTypeOpcode(EmuOpcode opcode_name, int16* opcode) {
	(void)opcode_name;
	(void)opcode;
	return false;
}

static bool GetClient546RootVeTypeEmuOpcode(uint16 opcode, EmuOpcode* opcode_name) {
	(void)opcode;
	(void)opcode_name;
	return false;
}
#endif

uint8 EQApplicationPacket::default_opcode_size=2;

EQPacket::EQPacket(const uint16 op, const unsigned char *buf, uint32 len)
{
	this->opcode=op;
	this->pBuffer=NULL;
	this->size=0;
	version = 0;
	setTimeInfo(0,0);
	if (len>0) {
		this->size=len;
		pBuffer= new unsigned char[this->size];
		if (buf) {
			memcpy(this->pBuffer,buf,this->size);
		}  else {
			memset(this->pBuffer,0,this->size);
		}
	}
}

const char* EQ2Packet::GetOpcodeName() {
	int16 OpcodeVersion = GetOpcodeVersion(version);
	if (EQOpcodeManager.count(OpcodeVersion) > 0)
		return EQOpcodeManager[OpcodeVersion]->EmuToName(login_op);
	else
		return NULL;
}

int8 EQ2Packet::PreparePacket(int16 MaxLen) {
	int16 OpcodeVersion = GetOpcodeVersion(version);

	// stops a crash for incorrect version
	if (EQOpcodeManager.count(OpcodeVersion) == 0)
	{
		LogWrite(PACKET__ERROR, 0, "Packet", "Version %i is not listed in the opcodes table.", version);
		return -1;
	}

	packet_prepared = true;

	int16 login_opcode = 0;
	bool opcode_overridden = version == 546 && GetClient546RootVeTypeOpcode(login_op, &login_opcode);
	if (!opcode_overridden)
		login_opcode = EQOpcodeManager[OpcodeVersion]->EmuToEQ(login_op);
	if (login_opcode == 0xcdcd)
	{
		LogWrite(PACKET__ERROR, 0, "Packet", "Version %i is not listed in the opcodes table for opcode %s", version, EQOpcodeManager[OpcodeVersion]->EmuToName(login_op));
		return -1;
	}
	
	int16 orig_opcode = login_opcode;
	int8 offset = 0;
	//one of the int16s is for the seq, other is for the EQ2 opcode and compressed flag (OP_Packet is the header, not the opcode)
	int32 new_size = size + sizeof(int16) + sizeof(int8);
	bool oversized = false;
	if (login_opcode != 2) {
		new_size += sizeof(int8); //for opcode
		if (login_opcode >= 255) {
			new_size += sizeof(int16);
			oversized = true;
		}
		else
			login_opcode = ntohs(login_opcode);
	}
	uchar* new_buffer = new uchar[new_size];
	memset(new_buffer, 0, new_size);
	uchar* ptr = new_buffer + sizeof(int16); // sequence is first
	if (login_opcode != 2) {
		if (oversized) {
			ptr += sizeof(int8); //compressed flag
			int8 addon = 0xff;
			memcpy(ptr, &addon, sizeof(int8));
			ptr += sizeof(int8);
		}
		memcpy(ptr, &login_opcode, sizeof(int16));
		ptr += sizeof(int16);
	}
	else {
		memcpy(ptr, &login_opcode, sizeof(int8));
		ptr += sizeof(int8);
	}
	memcpy(ptr, pBuffer, size);
	
	safe_delete_array(pBuffer);
	pBuffer = new_buffer;
	offset = new_size - size - 1;
	size = new_size;
	
	return offset;
}

uint32 EQProtocolPacket::serialize(unsigned char *dest, int8 offset) const
{
	if (opcode>0xff)  {
		*(uint16 *)dest=opcode;
	} else {
		*(dest)=0;
		*(dest+1)=opcode;
	}
	memcpy(dest+2,pBuffer+offset,size-offset);

	return size+2;
}

uint32 EQApplicationPacket::serialize(unsigned char *dest) const
{
	uint8 OpCodeBytes = app_opcode_size;

	if (app_opcode_size==1)
		*(unsigned char *)dest=opcode;
	else
	{
		// Application opcodes with a low order byte of 0x00 require an extra 0x00 byte inserting prior to the opcode.
		if ((opcode & 0x00ff) == 0)
		{
			*(uint8*)dest = 0;
			*(uint16*)(dest + 1) = opcode;
			++OpCodeBytes;
		}
		else
			*(uint16*)dest = opcode;
	}

	memcpy(dest+app_opcode_size,pBuffer,size);

	return size+ OpCodeBytes;
}

EQPacket::~EQPacket()
{
	safe_delete_array(pBuffer);
	pBuffer=NULL;
}


void EQPacket::DumpRawHeader(uint16 seq, FILE* to) const
{
	/*if (timestamp.tv_sec) {
		char temp[20];
		tm t;
		const time_t sec = timestamp.tv_sec;
		localtime_s(&t, &sec);
		strftime(temp, 20, "%F %T", &t);
		fprintf(to, "%s.%06lu ", temp, timestamp.tv_usec);
	}*/

	DumpRawHeaderNoTime(seq, to);
}

const char* EQPacket::GetOpcodeName(){
	int16 OpcodeVersion = GetOpcodeVersion(version);
	EmuOpcode client_opcode = OP_Unknown;
	if (version == 546 && GetClient546RootVeTypeEmuOpcode(opcode, &client_opcode))
		return OpcodeNames[client_opcode];

	if(EQOpcodeManager.count(OpcodeVersion) > 0)
		return EQOpcodeManager[OpcodeVersion]->EQToName(opcode);
	else
		return NULL;
}
void EQPacket::DumpRawHeaderNoTime(uint16 seq, FILE *to) const
{
	if (src_ip) {
		string sIP,dIP;;
		sIP=long2ip(src_ip);
		dIP=long2ip(dst_ip);
		fprintf(to, "[%s:%d->%s:%d] ",sIP.c_str(),src_port,dIP.c_str(),dst_port);
	}
	if (seq != 0xffff)
		fprintf(to, "[Seq=%u] ",seq);
	
	string name;
	int16 OpcodeVersion = GetOpcodeVersion(version);
	EmuOpcode client_opcode = OP_Unknown;
	if (version == 546 && GetClient546RootVeTypeEmuOpcode(opcode, &client_opcode))
		name = OpcodeNames[client_opcode];
	else if(EQOpcodeManager.count(OpcodeVersion) > 0)
		name = EQOpcodeManager[OpcodeVersion]->EQToName(opcode);
	
	fprintf(to, "[OpCode 0x%04x (%s) Size=%u]\n",opcode,name.c_str(),size);
}

void EQPacket::DumpRaw(FILE *to) const
{
	DumpRawHeader();
	if (pBuffer && size)
		dump_message_column(pBuffer, size, " ", to);
	fprintf(to, "\n");
}

EQProtocolPacket::EQProtocolPacket(const unsigned char *buf, uint32 len, int in_opcode)
{
	uint32 offset = 0;
	if(in_opcode>=0) {
		opcode = in_opcode;
	}
	 else {
		// Ensure there are at least 2 bytes for the opcode
		if (len < 2 || buf == nullptr) {
			// Not enough data to read opcode; set defaults or handle error appropriately
			opcode = 0;   // or set to a designated invalid opcode
			offset = len; // no payload available
		} else {
			offset = 2;
			opcode = ntohs(*(const uint16 *)buf);
		}
	}
	
	// Check that there is payload data after the header
	if (len > offset) {
		size = len - offset;
		pBuffer = new unsigned char[size];
		if(buf)
			memcpy(pBuffer, buf + offset, size);
		else
			memset(pBuffer, 0, size);
	} else {
		pBuffer = nullptr;
		size = 0;
	}
	
	version = 0;
	eq2_compressed = false;
	packet_prepared = false;
	packet_encrypted = false;
	sent_time = 0;
	attempt_count = 0;
	sequence = 0;
}

bool EQ2Packet::AppCombine(EQ2Packet* rhs){
	bool result = false;
	uchar* tmpbuffer = 0;
	bool over_sized_packet = false;
	int32 new_size = 0;
	//bool whee = false;
//	DumpPacket(this);
//		DumpPacket(rhs);
	/*if(rhs->size >= 255){
		DumpPacket(this);
		DumpPacket(rhs);
		whee = true;
	}*/
	if (opcode==OP_AppCombined && ((size + rhs->size + 3) < 255)){
		int16 tmp_size = rhs->size - 2;
		if(tmp_size >= 255){
			new_size = size+tmp_size+3;
			over_sized_packet = true;
		}
		else
			new_size = size+tmp_size+1;
		tmpbuffer = new uchar[new_size];
		uchar* ptr = tmpbuffer;
		memcpy(ptr, pBuffer, size);
		ptr += size;
		if(over_sized_packet){
			memset(ptr, 255, sizeof(int8));
			ptr += sizeof(int8);
			tmp_size = htons(tmp_size);
			memcpy(ptr, &tmp_size, sizeof(int16));
			ptr += sizeof(int16);
		}
		else{
			memcpy(ptr, &tmp_size, sizeof(int8));
			ptr += sizeof(int8);
		}
		memcpy(ptr, rhs->pBuffer+2, rhs->size-2);
		delete[] pBuffer;
		size = new_size;
		pBuffer=tmpbuffer;
		safe_delete(rhs);
		result=true;
	}
	else if (rhs->size > 2 && size > 2 && (size + rhs->size + 6) < 255) {
		int32 tmp_size = size - 2;
		int32 tmp_size2 = rhs->size - 2;
		opcode=OP_AppCombined;
		bool over_sized_packet2 = false;
		new_size = size;
		if(tmp_size >= 255){
			new_size += 5;
			over_sized_packet = true;
		}
		else
			new_size += 3;
		if(tmp_size2 >= 255){
			new_size += tmp_size2+3;
			over_sized_packet2 = true;
		}
		else
			new_size += tmp_size2+1;
		tmpbuffer = new uchar[new_size];
		tmpbuffer[2]=0;
		tmpbuffer[3]=0x19;
		uchar* ptr = tmpbuffer+4;
		if(over_sized_packet){
			memset(ptr, 255, sizeof(int8));
			ptr += sizeof(int8);
			tmp_size = htons(tmp_size);
			memcpy(ptr, &tmp_size, sizeof(int16));
			ptr += sizeof(int16);
		}
		else{
			memcpy(ptr, &tmp_size, sizeof(int8));
			ptr += sizeof(int8);
		}
		memcpy(ptr, pBuffer+2, size-2);
		ptr += (size-2);
		if(over_sized_packet2){
			memset(ptr, 255, sizeof(int8));
			ptr += sizeof(int8);
			tmp_size2 = htons(tmp_size2);
			memcpy(ptr, &tmp_size2, sizeof(int16));
			ptr += sizeof(int16);
		}
		else{
			memcpy(ptr, &tmp_size2, sizeof(int8));
			ptr += sizeof(int8);
		}
		memcpy(ptr, rhs->pBuffer+2, rhs->size-2);
		size = new_size;
		delete[] pBuffer;
		pBuffer=tmpbuffer;
		safe_delete(rhs);
		result=true;
	}
	/*if(whee){
		DumpPacket(this);
		cout << "fsdfsdf";
	}*/
	//DumpPacket(this);
	return result;
}

bool EQProtocolPacket::combine(const EQProtocolPacket *rhs)
{
	bool result=false;
	//if(dont_combine)
	//	return false;
	//if (opcode==OP_Combined && size+rhs->size+5<256) {	
	if (opcode == OP_Combined && size + rhs->size + 5 < 256) {
		auto tmpbuffer = new unsigned char[size + rhs->size + 3];
		memcpy(tmpbuffer, pBuffer, size);
		uint32 offset = size;
		tmpbuffer[offset++] = rhs->Size();
		offset += rhs->serialize(tmpbuffer + offset);
		size = offset;
		delete[] pBuffer;
		pBuffer = tmpbuffer;
		result = true;
	}
	else if (size + rhs->size + 7 < 256) {
		auto tmpbuffer = new unsigned char[size + rhs->size + 6];
		uint32 offset = 0;
		tmpbuffer[offset++] = Size();
		offset += serialize(tmpbuffer + offset);
		tmpbuffer[offset++] = rhs->Size();
		offset += rhs->serialize(tmpbuffer + offset);
		size = offset;
		delete[] pBuffer;
		pBuffer = tmpbuffer;
		opcode = OP_Combined;
		result = true;
	}
	return result;
}

EQApplicationPacket::EQApplicationPacket(const unsigned char *buf, uint32 len, uint8 opcode_size)
{
uint32 offset=0;
	app_opcode_size=(opcode_size==0) ? EQApplicationPacket::default_opcode_size : opcode_size;

	if (app_opcode_size==1) {
		opcode=*(const unsigned char *)buf;
		offset++;
	} else {
		opcode=*(const uint16 *)buf;
		offset+=2;
	}

	if ((len-offset)>0) {
		pBuffer=new unsigned char[len-offset];
		memcpy(pBuffer,buf+offset,len-offset);
		size=len-offset;
	} else {
		pBuffer=NULL;
		size=0;
	}
	
	emu_opcode = OP_Unknown;
}

bool EQApplicationPacket::combine(const EQApplicationPacket *rhs)
{
cout << "CALLED AP COMBINE!!!!\n";
	return false;
}

void EQApplicationPacket::SetOpcode(EmuOpcode emu_op) {
	if(emu_op == OP_Unknown) {
		opcode = 0;
		emu_opcode = OP_Unknown;
		return;
	}

	int16 client_opcode = 0;
	if (version == 546 && GetClient546RootVeTypeOpcode(emu_op, &client_opcode))
		opcode = client_opcode;
	else
		opcode = EQOpcodeManager[GetOpcodeVersion(version)]->EmuToEQ(emu_op);
	
	if(opcode == OP_Unknown) {
		LogWrite(PACKET__DEBUG, 0, "Packet", "Unable to convert Emu opcode %s (%d) into an EQ opcode.", OpcodeNames[emu_op], emu_op);
	}

	//save the emu opcode we just set.
	emu_opcode = emu_op;
}

const EmuOpcode EQApplicationPacket::GetOpcodeConst() const {
	if(emu_opcode != OP_Unknown) {
		return(emu_opcode);
	}
	if(opcode == 10000) {
		return(OP_Unknown);
	}

	EmuOpcode emu_op = OP_Unknown;
	if (version == 546 && GetClient546RootVeTypeEmuOpcode(opcode, &emu_op))
		return(emu_op);

	emu_op = EQOpcodeManager[GetOpcodeVersion(version)]->EQToEmu(opcode);
	if(emu_op == OP_Unknown) {
		LogWrite(PACKET__DEBUG, 1, "Packet", "Unable to convert EQ opcode 0x%.4X (%i) to an emu opcode (%s)", opcode, opcode, __FUNCTION__);
	}
	
	return(emu_op);
}

EQApplicationPacket *EQProtocolPacket::MakeApplicationPacket(uint8 opcode_size) const {
	EQApplicationPacket *res = new EQApplicationPacket;
	res->app_opcode_size=(opcode_size==0) ? EQApplicationPacket::default_opcode_size : opcode_size;
	if (res->app_opcode_size==1) {
		res->pBuffer= new unsigned char[size+1];
		memcpy(res->pBuffer+1,pBuffer,size);
		*(res->pBuffer)=htons(opcode)&0xff;
		res->opcode=opcode&0xff;
		res->size=size+1;
	} else {
		res->pBuffer= new unsigned char[size];
		memcpy(res->pBuffer,pBuffer,size);
		res->opcode=opcode;
		res->size=size;
	}
	res->copyInfo(this);
	return(res);
}
bool EQProtocolPacket::ValidateCRC(const unsigned char *buffer, int length, uint32 Key)
{
bool valid=false;
	// OP_SessionRequest, OP_SessionResponse, OP_OutOfSession are not CRC'd
	if (buffer[0]==0x00 && (buffer[1]==OP_SessionRequest || buffer[1]==OP_SessionResponse || buffer[1]==OP_OutOfSession)) {
		valid=true;
	} else if(buffer[2] == 0x00 && buffer[3] == 0x19){
		valid = true;
	}
	else {
		uint16 comp_crc=CRC16(buffer,length-2,Key);
		uint16 packet_crc=ntohs(*(const uint16 *)(buffer+length-2));
#ifdef EQN_DEBUG
		if (packet_crc && comp_crc != packet_crc) {
			cout << "CRC mismatch: comp=" << hex << comp_crc << ", packet=" << packet_crc << dec << endl;
		}
#endif
		valid = (!packet_crc || comp_crc == packet_crc);
	}
	return valid;
}

uint32 EQProtocolPacket::Decompress(const unsigned char *buffer, const uint32 length, unsigned char *newbuf, uint32 newbufsize)
{
uint32 newlen=0;
uint32 flag_offset=0;
	newbuf[0]=buffer[0];
	if (buffer[0]==0x00) {
		flag_offset=2;
		newbuf[1]=buffer[1];
	} else
		flag_offset=1;

	if (length>2 && buffer[flag_offset]==0x5a)  {
		LogWrite(PACKET__DEBUG, 0, "Packet", "In Decompress 1");
		newlen=Inflate(const_cast<unsigned char *>(buffer+flag_offset+1),length-(flag_offset+1)-2,newbuf+flag_offset,newbufsize-flag_offset)+2;

		// something went bad with zlib
		if (newlen == -1)
		{
			LogWrite(PACKET__ERROR, 0, "Packet", "Debug Bad Inflate!");
			DumpPacket(buffer, length);
			memcpy(newbuf, buffer, length);
			return length;
		}

		newbuf[newlen++]=buffer[length-2];
		newbuf[newlen++]=buffer[length-1];
	} else if (length>2 && buffer[flag_offset]==0xa5) {
		LogWrite(PACKET__DEBUG, 0, "Packet", "In Decompress 2");
		memcpy(newbuf+flag_offset,buffer+flag_offset+1,length-(flag_offset+1));
		newlen=length-1;
	} else {
		memcpy(newbuf,buffer,length);
		newlen=length;
	}

	return newlen;
}

uint32 EQProtocolPacket::Compress(const unsigned char *buffer, const uint32 length, unsigned char *newbuf, uint32 newbufsize) {
uint32 flag_offset=1,newlength;
	//dump_message_column(buffer,length,"Before: ");
	newbuf[0]=buffer[0];
	if (buffer[0]==0) {
		flag_offset=2;
		newbuf[1]=buffer[1];
	}
	if (length>30) {
		newlength=Deflate(const_cast<unsigned char *>(buffer+flag_offset),length-flag_offset,newbuf+flag_offset+1,newbufsize);
		*(newbuf+flag_offset)=0x5a;
		newlength+=flag_offset+1;
	} else {
		memmove(newbuf+flag_offset+1,buffer+flag_offset,length-flag_offset);
		*(newbuf+flag_offset)=0xa5;
		newlength=length+1;
	}
	//dump_message_column(newbuf,length,"After: ");

	return newlength;
}

void EQProtocolPacket::ChatDecode(unsigned char *buffer, int size, int DecodeKey)
{
	if (buffer[1]!=0x01 && buffer[0]!=0x02 && buffer[0]!=0x1d) {
		int Key=DecodeKey;
		unsigned char *test=(unsigned char *)malloc(size);
		buffer+=2;
		size-=2;

        	int i;
		for (i = 0 ; i+4 <= size ; i+=4)
		{
			int pt = (*(int*)&buffer[i])^(Key);
			Key = (*(int*)&buffer[i]);
			*(int*)&test[i]=pt;
		}
		unsigned char KC=Key&0xFF;
		for ( ; i < size ; i++)
		{
			test[i]=buffer[i]^KC;
		}
		memcpy(buffer,test,size);	
		free(test);
	}
}

void EQProtocolPacket::ChatEncode(unsigned char *buffer, int size, int EncodeKey)
{
	if (buffer[1]!=0x01 && buffer[0]!=0x02 && buffer[0]!=0x1d) {
		int Key=EncodeKey;
		char *test=(char*)malloc(size);
		int i;
		buffer+=2;
		size-=2;
		for ( i = 0 ; i+4 <= size ; i+=4)
		{
			int pt = (*(int*)&buffer[i])^(Key);
			Key = pt;
			*(int*)&test[i]=pt;
		}
		unsigned char KC=Key&0xFF;
		for ( ; i < size ; i++)
		{
			test[i]=buffer[i]^KC;
		}
		memcpy(buffer,test,size);	
		free(test);
	}
}

bool EQProtocolPacket::IsProtocolPacket(const unsigned char* in_buff, uint32_t len, bool bTrimCRC) {
	bool ret = false;
	uint16_t opcode = ntohs(*(uint16_t*)in_buff);
	uint32_t offset = 2;

	switch (opcode) {
	case OP_SessionRequest:
	case OP_SessionDisconnect:
	case OP_KeepAlive:
	case OP_SessionStatResponse:
	case OP_Packet:
	case OP_Combined:
	case OP_Fragment:
	case OP_Ack:
	case OP_OutOfOrderAck:
	case OP_OutOfSession:
		{
			ret = true;
			break;
		}
	}

	return ret;
}



void DumpPacketHex(const EQApplicationPacket* app)
{
	DumpPacketHex(app->pBuffer, app->size);
}

void DumpPacketAscii(const EQApplicationPacket* app)
{
	DumpPacketAscii(app->pBuffer, app->size);
}
void DumpPacket(const EQProtocolPacket* app) {
	DumpPacketHex(app->pBuffer, app->size);
}
void DumpPacket(const EQApplicationPacket* app, bool iShowInfo) {
	if (iShowInfo) {
		cout << "Dumping Applayer: 0x" << hex << setfill('0') << setw(4) << app->GetOpcode() << dec;
		cout << " size:" << app->size << endl;
	}
	DumpPacketHex(app->pBuffer, app->size);
//	DumpPacketAscii(app->pBuffer, app->size);
}

void DumpPacketBin(const EQApplicationPacket* app) {
	DumpPacketBin(app->pBuffer, app->size);
}


