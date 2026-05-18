#!/usr/bin/env python3
"""Decode higher-level structure from EQ2 Frida packet logs."""

from __future__ import annotations

import argparse
import csv
import json
import string
import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


PRINTABLE = set(bytes(string.printable, "ascii"))

CLIENT_DERIVED_OPCODE_NAMES = {
    0: "OP_LoginRequestMsg",
    1: "OP_LoginByNumRequestMsg",
    2: "OP_WSLoginRequestMsg",
    3: "OP_ESLoginRequestMsg",
    4: "OP_LoginReplyMsg",
    5: "OP_WSStatusReplyMsg",
    6: "OP_WorldStatusChangeMsg",
    7: "OP_AllWSDescRequestMsg",
    8: "OP_WorldListMsg",
    9: "OP_AllCharactersDescRequestMsg",
    10: "OP_AllCharactersDescReplyMsg",
    11: "OP_CreateCharacterRequestMsg",
    12: "OP_CreateCharacterReplyMsg",
    13: "OP_WSCreateCharacterRequestMsg",
    14: "OP_WSCreateCharacterReplyMsg",
    15: "OP_ReskinCharacterRequestMsg",
    16: "OP_DeleteCharacterRequestMsg",
    17: "OP_DeleteCharacterReplyMsg",
    18: "OP_PlayCharacterRequestMsg",
    19: "OP_PlayCharacterReplyMsg",
    20: "OP_ServerPlayCharacterRequestMsg",
    21: "OP_ServerPlayCharacterReplyMsg",
    22: "OP_ESInitMsg",
    23: "OP_ESReadyForClientsMsg",
    24: "OP_CreateZoneInstanceMsg",
    25: "OP_ZoneInstanceCreateReplyMsg",
    26: "OP_ZoneInstanceDestroyedMsg",
    27: "OP_ExpectClientAsCharacterRequest",
    28: "OP_ExpectClientAsCharacterReplyMsg",
    29: "OP_ZoneInfoMsg",
    30: "OP_UnnamedCountedU16ListMsg",
    31: "OP_DoneSendingInitialEntitiesMsg",
    32: "OP_DoneLoadingZoneResourcesMsg",
    33: "OP_DoneLoadingUIResourcesMsg",
    34: "VePredictionUpdateMsg",
    35: "OP_SetRemoteCmdsMsg",
    36: "OP_RemoteCmdMsg",
    37: "OP_GameWorldTimeMsg",
    38: "OP_MOTDOrString16Msg",
    39: "OP_ZoneMOTDOrTwoString16Msg",
    40: "OP_UnidentifiedFixedFieldMsg",
    41: "OP_UnresolvedMOTDAvatarU32U8Msg",
    42: "OP_RequestCampMsg",
    43: "OP_CampStartedMsg",
    44: "OP_CampAbortedMsg",
    45: "OP_EnvironmentMapStateMsg",
    46: "OP_WhoQueryReplyMsg",
    47: "OP_MonitorReplyMsg",
    48: "OP_MonitorCharacterListMsg",
    49: "OP_MonitorCharacterListRequestMsg",
    50: "OP_ClientCmdMsg",
    51: "OP_DispatchClientCmdMsg",
    52: "OP_DispatchESMsg",
    53: "OP_UpdateTargetMsg",
    54: "OP_UpdateTargetLocMsg",
    55: "OP_UpdateCharacterSheetMsg",
    56: "OP_UpdateSpellBookMsg",
    58: "OP_UpdateInventoryMsg",
    59: "OP_AfterInvSpellUpdate",
    60: "OP_UpdateRecipeBookMsg",
    61: "OP_RequestRecipeDetailsMsg",
    62: "OP_RecipeDetailsMsg",
    63: "OP_UpdateSkillBookMsg",
    64: "OP_UpdateSkillsMsg",
    65: "OP_UpdateOpportunityMsg",
    67: "OP_ChangeZoneMsg",
    68: "OP_ClientTeleportRequestMsg",
    69: "OP_TeleportWithinZoneMsg",
    70: "OP_TeleportWithinZoneNoReloadMsg",
    71: "OP_MigrateClientToZoneRequestMsg",
    72: "OP_MigrateClientToZoneReplyMsg",
    73: "OP_ReadyToZoneMsg",
    74: "OP_RemoveClientFromGroupMsg",
    75: "OP_RemoveGroupFromGroupMsg",
    76: "OP_MakeGroupLeaderMsg",
    77: "OP_GroupCreatedMsg",
    78: "OP_GroupDestroyedMsg",
    79: "OP_GroupMemberAddedMsg",
    80: "OP_GroupMemberRemovedMsg",
    81: "OP_GroupRemovedFromGroupMsg",
    82: "OP_GroupLeaderChangedMsg",
    83: "OP_GroupResendOOZDataMsg",
    84: "OP_GroupSettingsChangedMsg",
    85: "OP_OutOfZoneMemberDataMsg",
    86: "OP_SendLatestRequestMsg",
    87: "OP_ClearDataMsg",
    88: "OP_SetSocialMsg",
    89: "OP_ClearDataMsg",
    90: "OP_ESZoneInstanceStatusMsg",
    91: "OP_ZonesStatusRequestMsg",
    92: "OP_ZonesStatusMsg",
    93: "OP_ESWeatherRequestMsg",
    94: "OP_ZonesStatusMsg",
    95: "OP_DialogSelectMsg",
    96: "OP_DialogCloseMsg",
    97: "OP_RemoveSpellEffectMsg",
    98: "OP_RemoveConcentrationMsg",
    99: "OP_QuestJournalOpenMsg",
    100: "OP_QuestJournalInspectMsg",
    101: "OP_UnresolvedQuestJournalThreeU32Type101Msg",
    102: "OP_UnresolvedQuestJournalEmptyType102Msg",
    103: "OP_UnresolvedQuestJournalSelectionType103Msg",
    104: "OP_QuestJournalSetVisibleMsg",
    105: "OP_QuestJournalWaypointMsg",
    106: "OP_CreateGuildRequestMsg",
    107: "OP_CreateGuildReplyMsg",
    108: "OP_GuildsayMsg",
    109: "OP_GuildKickMsg",
    110: "OP_GuildUpdateMsg",
    111: "OP_UnresolvedGuildAdjacentU32Type111Msg",
    112: "OP_FellowshipExpMsg",
    113: "OP_UnresolvedGuildConsignmentU32U32U8Msg",
    114: "OP_ConsignmentCloseStoreMsg",
    115: "OP_ConsignItemRequestMsg",
    116: "OP_ConsignItemResponseMsg",
    117: "OP_PurchaseConsignmentResponseMsg",
    118: "OP_HouseDeletedRemotelyMsg",
    119: "OP_UpdateHouseDataMsg",
    120: "OP_UpdateHouseAccessDataMsg",
    121: "OP_PlayerHouseBaseScreenMsg",
    122: "OP_PlayerHousePurchaseScreenMsg",
    123: "OP_PlayerHouseAccessUpdateMsg",
    124: "OP_PlayerHouseDisplayStatusMsg",
    125: "OP_PlayerHouseCloseUIMsg",
    126: "OP_BuyPlayerHouseStatusMsg",
    127: "OP_BuyPlayerHouseTintMsg",
    128: "OP_BuyPlayerHouseMsg",
    129: "OP_UnresolvedHouseU32U8Type129Msg",
    130: "OP_UnresolvedHouseSingleU32Type130Msg",
    131: "OP_UnresolvedHouseSingleU32Type131Msg",
    132: "OP_EnterHouseMsg",
    133: "OP_ExitHouseMsg",
    134: "OP_HouseDefaultAccessSetMsg",
    135: "OP_HouseAccessSetMsg",
    136: "OP_HouseAccessRemoveMsg",
    137: "OP_PayHouseUpkeepMsg",
    138: "OP_MoveableObjectPlacementCriteria",
    139: "OP_EnterMoveObjectModeMsg",
    140: "OP_PositionMoveableObject",
    141: "OP_CancelMoveObjectModeMsg",
    142: "OP_HouseCustomizationScreenMsg",
    143: "OP_CustomizationPurchaseRequestMsg",
    144: "OP_CustomizationSetRequestMsg",
    145: "OP_UnresolvedShaderTintSelectionType145Msg",
    146: "OP_UnresolvedShaderTintSelectionType146Msg",
    147: "OP_CustomizationReplyMsg",
    148: "OP_TintWidgetsMsg",
    149: "OP_ExamineConsignmentRequestMsg",
    150: "OP_ExamineConsignmentResponseMsg",
    151: "OP_UISettingsResponseMsg",
    152: "OP_UIResetMsg",
    153: "OP_KeymapLoadMsg",
    154: "OP_KeymapNoneMsg",
    155: "OP_KeymapDataMsg",
    156: "OP_KeymapSaveMsg",
    157: "OP_DispatchSpellCmdMsg",
    158: "OP_UnregisteredEntityVerbGapType158Msg",
    159: "OP_EntityVerbsRequestMsg",
    160: "OP_EntityVerbsReplyMsg",
    161: "OP_EntityVerbsVerbMsg",
    162: "OP_ChatRelationshipUpdateMsg",
    163: "OP_LootItemsRequestMsg",
    164: "OP_StoppedLootingMsg",
    165: "OP_SitMsg",
    166: "OP_StandMsg",
    167: "OP_SatMsg",
    168: "OP_StoodMsg",
    169: "OP_ClearForTakeOffMsg",
    170: "OP_ReadyForTakeOffMsg",
    171: "OP_ShowIllusionsMsg",
    172: "OP_HideIllusionsMsg",
    173: "OP_ExamineItemRequestMsg",
    174: "OP_ReadBookPageMsg",
    175: "OP_DefaultGroupOptionsRequestMsg",
    176: "OP_DefaultGroupOptionsMsg",
    177: "OP_GroupOptionsMsg",
    178: "OP_DisplayGroupOptionsScreenMsg",
    179: "OP_DisplayInnVisitScreenMsg",
    180: "OP_DumpSchedulerMsg",
    181: "OP_LSRequestPlayerDescMsg",
    182: "OP_LSCheckAcctLockMsg",
    183: "OP_WSAcctLockStatusMsg",
    184: "OP_RequestHelpRepathMsg",
    185: "OP_RequestTargetLocMsg",
    186: "OP_UpdateMotdMsg",
    187: "OP_PerformPlayerKnockbackMsg",
    188: "OP_PerformCameraShakeMsg",
    189: "OP_PopulateSkillMapsMsg",
    190: "OP_CancelledFeignMsg",
    191: "OP_SignalMsg",
    192: "OP_ShowCreateFromRecipeUIMsg",
    193: "OP_CancelCreateFromRecipeMsg",
    194: "OP_BeginItemCreationMsg",
    195: "OP_StopItemCreationMsg",
    196: "OP_ShowItemCreationProcessUIMsg",
    197: "OP_UpdateItemCreationProcessUIMsg",
    198: "OP_DisplayTSEventReactionMsg",
    199: "OP_ShowRecipeBookMsg",
    200: "OP_KnowledgebaseRequestMsg",
    201: "OP_KnowledgebaseResponseMsg",
    202: "OP_CSTicketHeaderRequestMsg",
    203: "OP_CSTicketInfoMsg",
    204: "OP_CSTicketCommentRequestMsg",
    205: "OP_CSTicketCommentResponseMsg",
    206: "OP_CSTicketCreateMsg",
    207: "OP_CSTicketAddCommentMsg",
    208: "OP_CSTicketDeleteMsg",
    209: "OP_CSTicketChangeNotificationMsg",
    210: "OP_WorldDataUpdateMsg",
    211: "OP_KnownLanguagesMsg",
    212: "OP_LsRequestClientCrashLogMsg",
    213: "OP_LsClientBaselogReplyMsg",
    214: "OP_LsClientCrashlogReplyMsg",
    215: "OP_LsClientEq2CrashLogReplyMsg",
    216: "OP_LsClientAlertlogReplyMsg",
    217: "OP_LsClientVerifylogReplyMsg",
    218: "OP_ClientTeleportToLocationMsg",
    219: "OP_UpdateClientPredFlagsMsg",
    220: "OP_ChangeServerControlFlagMsg",
    221: "OP_CSToolsResponseMsg",
    222: "OP_AddSocialStructureStandingMsg",
    223: "OP_BoatTransportOrStandingMsg",
    224: "OP_CreateBoatTransportsMsg",
    225: "OP_PositionBoatTransportMsg",
    226: "OP_MigrateBoatTransportMsg",
    227: "OP_MigrateBoatTransportReplyMsg",
    228: "OP_DisplayDebugNLLPointsMsg",
    229: "OP_ExamineInfoRequestMsg",
    230: "OP_QuickbarInitMsg",
    231: "OP_QuickbarUpdateMsg",
    232: "OP_MacroInitMsg",
    233: "OP_MacroUpdateMsg",
    234: "OP_QuestionnaireMsg",
    235: "OP_LevelChangedMsg",
    236: "OP_DisplayWarningMsg",
    237: "OP_EncounterBrokenMsg",
    238: "OP_OnscreenMsgMsg",
    239: "OP_ModifyGuildMsg",
    240: "OP_GuildEventMsg",
    241: "OP_UnresolvedGuildEventU32U32StringU32Msg",
    242: "OP_GuildEventAddMsg",
    243: "OP_GuildEventActionMsg",
    244: "OP_GuildEventListMsg",
    245: "OP_GuildEventDetailsMsg",
    246: "OP_RequestGuildInfoMsg",
    247: "OP_UnresolvedGuildInfoAdjacentMsg",
    248: "OP_GuildBankActionMsg",
    249: "OP_GuildBankActionResponseMsg",
    250: "OP_GuildBankItemDetailsResponseMsg",
    251: "OP_GuildBankSmallUpdateMsg",
    252: "OP_GuildBankUpdateMsg",
    253: "OP_GuildBankEventListMsg",
    254: "OP_RequestGuildBankEventDetailsMsg",
    255: "OP_RewardPackMsg",
    256: "OP_RenameGuildMsg",
    257: "OP_ZoneToFriendRequestMsg",
    258: "OP_ZoneToFriendReplyMsg",
    259: "OP_ChatCreateChannelMsg",
    260: "OP_ChatJoinChannelMsg",
    261: "OP_ChatWhoChannelMsg",
    262: "OP_ChatLeaveChannelMsg",
    263: "OP_ChatTellChannelMsg",
    264: "OP_ChatTellUserMsg",
    265: "OP_ChatToggleFriendMsg",
    266: "OP_ChatToggleIgnoreMsg",
    267: "OP_ChatSendFriendsMsg",
    268: "OP_ChatSendIgnoresMsg",
    269: "OP_ChatFiltersMsg",
    270: "OP_MailGetHeadersMsg",
    271: "OP_MailGetMessageMsg",
    272: "OP_MailSendMessageMsg",
    273: "OP_MailDeleteMessageMsg",
    274: "OP_MailGetHeadersReplyMsg",
    275: "OP_MailGetMessageReplyMsg",
    276: "OP_MailSendMessageReplyMsg",
    277: "OP_MailCommitSendMessageMsg",
    278: "OP_MailSendSystemMessageMsg",
    279: "OP_MailRemoveAttachFromMailMsg",
    280: "OP_WaypointRequestMsg",
    281: "OP_WaypointReplyMsg",
    282: "OP_WaypointSelectMsg",
    283: "OP_WaypointUpdateMsg",
    284: "OP_CharNameChangedMsg",
    285: "OP_ShowZoneTeleporterDestinationsMsg",
    286: "OP_SelectZoneTeleporterDestinationMsg",
    287: "OP_ReloadLocalizedTxtMsg",
    288: "OP_RequestGuildMembershipMsg",
    289: "OP_GuildMembershipResponseMsg",
    290: "OP_LeaveGuildNotifyMsg",
    291: "OP_JoinGuildNotifyMsg",
    292: "OP_AvatarUpdateMsg",
    293: "OP_BioUpdateMsg",
    294: "OP_QuestReward",
    295: "OP_WSServerLockMsg",
    296: "OP_LSServerLockMsg",
    297: "OP_WSServerHideMsg",
    298: "OP_CsCategoryRequestMsg",
    299: "OP_CsCategoryResponseMsg",
    300: "OP_KnowledgeWindowSlotMappingMsg",
    301: "OP_LFGUpdateMsg",
    302: "OP_AFKUpdateMsg",
    303: "OP_AnonUpdateMsg",
    304: "OP_UpdateActivePublicZonesMsg",
    305: "OP_UnknownNpcMsg",
    306: "OP_PromoFlagsDetailsMsg",
    307: "OP_ConsignViewCreateMsg",
    308: "OP_ConsignViewGetPageMsg",
    309: "OP_ConsignViewReleaseMsg",
    310: "OP_ConsignRemoveItemsMsg",
    311: "OP_UpdateDebugRadiiMsg",
    312: "OP_SnoopMsg",
    313: "OP_ReportMsg",
    314: "OP_UpdateRaidMsg",
    315: "OP_UpdateArenaMsg",
    316: "OP_ConsignViewSortMsg",
    317: "OP_TitleUpdateMsg",
    318: "OP_ClientFellMsg",
    319: "OP_ClientInDeathRegionMsg",
    320: "OP_CampClientMsg",
    321: "OP_CSToolAccessResponseMsg",
    322: "OP_DeleteGuildMsg",
    323: "OP_TrackingUpdateMsg",
    324: "OP_BeginTrackingMsg",
    325: "OP_StopTrackingMsg",
    326: "OP_GetAvatarAccessRequestForCSToolsMsg",
    327: "OP_AdvancementRequestMsg",
    328: "OP_MapFogDataInitMsg",
    329: "OP_MapFogDataUpdateMsg",
    330: "OP_CloseGroupInviteWindowMsg",
    331: "OP_CorruptedClientMsg",
    332: "OP_WorldDataChangeMsg",
    333: "OP_MailEventNotificationMsg",
    334: "OP_OfferQuestMsg",
    335: "OP_RestartZoneMsg",
    336: "OP_DisplayMailScreenMsg",
    337: "OP_CharacterLinkdeadMsg",
    338: "OP_CharTransferStartRequestMsg",
    339: "OP_UnresolvedCharTransferBaseStringMsg",
    340: "OP_CharTransferRollbackRequestMsg",
    341: "OP_CharTransferCommitRequestMsg",
    342: "OP_CharTransferRollbackReplyMsg",
    343: "OP_CharTransferCommitReplyMsg",
    344: "OP_UnresolvedU32String16Type344Msg",
    345: "OP_FlightPathsMsg",
    346: "OP_UnresolvedU32String16Type346Msg",
    347: "OP_UnresolvedCharTransferCommonMsg",
    348: "OP_CharTransferStartReplyMsg",
    349: "OP_CharTransferRequestMsg",
    350: "OP_UnresolvedCharTransferCommonLeadingFlagMsg",
    351: "OP_UnresolvedCharTransferCommonTrailingFlagMsg",
    352: "OP_UnresolvedCharTransferCommonOnlyMsg",
    353: "OP_UnresolvedU32U8Type353Msg",
    354: "OP_UnresolvedU32Type354Msg",
    355: "OP_UnresolvedCharTransferEnvelopeMsg",
    356: "OP_GetCharacterSerializedReplyMsg",
    357: "OP_CreateCharFromCBBRequestMsg",
    358: "OP_UnresolvedCharTransferValidationMsg",
    359: "OP_HousingDataChangedMsg",
    360: "OP_HousingRestoreMsg",
    361: "OP_AuctionItem",
    362: "OP_AuctionItemReply",
    363: "OP_AuctionCoin",
    364: "OP_AuctionCoinReply",
    365: "OP_AuctionCharacter",
    366: "OP_AuctionCharacterReply",
    367: "OP_AuctionCommitMsg",
    368: "OP_AuctionAbortMsg",
    369: "OP_CharTransferValidateRequestMsg",
    370: "OP_CharTransferValidateReplyMsg",
    371: "OP_RaceRestrictionMsg",
    372: "OP_SetInstanceDisplayNameMsg",
    373: "OP_GetAuctionAssetIDMsg",
    374: "OP_GetAuctionAssetIDReplyMsg",
    375: "OP_ResendWorldChannelsMsg",
    376: "OP_DisplayExchangeScreenMsg",
    377: "OP_ArenaGameTypesMsg",
    378: "OP_EqHearChatCmd",
    379: "OP_EqDisplayTextCmd",
    380: "OP_EqCreateGhostCmd",
    381: "OP_EqCreateWidgetCmd",
    382: "OP_EqCreateSignWidgetCmd",
    383: "OP_EqDestroyGhostCmd",
    384: "OP_EqUpdateGhostCmd",
    385: "OP_EqSetControlGhostCmd",
    386: "OP_EqSetPOVGhostCmd",
    387: "OP_EqHearCombatCmd",
    388: "OP_EqHearSpellCastCmd",
    389: "OP_EqHearSpellInterruptCmd",
    390: "OP_EqHearSpellFizzleCmd",
    391: "OP_EqHearConsiderCmd",
    392: "OP_EqUpdateSubClassesCmd",
    393: "OP_EqCreateListBoxCmd",
    394: "OP_EqSetDebugPathPointsCmd",
    395: "OP_EqCannedEmoteCmd",
    396: "OP_EqStateCmd",
    397: "OP_EqPlaySoundCmd",
    398: "OP_EqPlaySound3DCmd",
    399: "OP_EqPlayVoiceCmd",
    400: "OP_EqHearDrowningCmd",
    401: "OP_InviteRequestMsg",
    402: "OP_InviteResponseMsg",
    403: "OP_InviteTargetResponseMsg",
    404: "OP_InspectPlayerRequestMsg",
    405: "OP_DispatchMsg",
    406: "OP_DisplayEventMsg",
    407: "OP_PrePossessionMsg",
    408: "OP_PostPossessionMsg",
    409: "OP_HouseItemsDetailsMsg",
    410: "OP_HouseItemsList",
}

CLIENT_CMD_DERIVED_OPCODE_NAMES = {
    411: "OP_EqHearChatCmd",
    412: "OP_EqDisplayTextCmd",
    413: "OP_EqCreateGhostCmd",
    414: "OP_EqCreateWidgetCmd",
    415: "OP_EqCreateSignWidgetCmd",
    416: "OP_EqDestroyGhostCmd",
    417: "OP_EqUpdateGhostCmd",
    418: "OP_EqSetControlGhostCmd",
    419: "OP_EqSetPOVGhostCmd",
    420: "OP_EqHearCombatCmd",
    421: "OP_EqHearSpellCastCmd",
    422: "OP_EqHearSpellInterruptCmd",
    423: "OP_EqHearSpellFizzleCmd",
    424: "OP_EqHearConsiderCmd",
    427: "OP_EqSetDebugPathPointsCmd",
    429: "OP_EqCannedEmoteCmd",
    430: "OP_EqStateCmd",
    431: "OP_EqPlaySoundCmd",
    432: "OP_EqPlaySound3DCmd",
    433: "OP_EqPlayVoiceCmd",
    434: "OP_EqHearDrowningCmd",
    435: "OP_EqHearDeathCmd",
    436: "OP_EqGroupMemberRemovedCmd",
    438: "OP_EqReceiveOfferCmd",
    439: "OP_EqInspectPCResultsCmd",
    441: "OP_EqDialogOpenCmd",
    442: "OP_EqDialogCloseCmd",
    443: "OP_EqFactionUpdateCmd",
    444: "OP_EqCollectionUpdateCmd",
    445: "OP_EqCollectionFilterCmd",
    446: "OP_EqCollectionItemCmd",
    447: "OP_EqQuestJournalUpdateCmd",
    448: "OP_EqQuestJournalReplyCmd",
    450: "OP_EqUpdateMerchantCmd",
    451: "OP_EqUpdateStoreCmd",
    452: "OP_EqUpdatePlayerTradeCmd",
    453: "OP_EqHelpPathCmd",
    454: "OP_EqHelpPathClearCmd",
    455: "OP_EqUpdateBankCmd",
    456: "OP_EqExamineInfoCmd",
    457: "OP_EqUpdateLootCmd",
    458: "OP_EqJunctionListCmd",
    459: "OP_EqShowDeathWindowCmd",
    460: "OP_EqDisplaySpellFailCmd",
    461: "OP_EqSpellCastStartCmd",
    462: "OP_EqSpellCastEndCmd",
    463: "OP_EqResurrectedCmd",
    464: "OP_EqChoiceWinCmd",
    465: "OP_EqSetDefaultVerbCmd",
    466: "OP_EqInstructionWindowCloseCmd",
    467: "OP_EqInstructionWindowCmd",
    468: "OP_EqInstructionWindowGoalCmd",
    469: "OP_EqInstructionWindowTaskCmd",
    470: "OP_EqEnableGameEventCmd",
    471: "OP_EqShowWindowCmd",
    472: "OP_EqEnableWindowCmd",
    473: "OP_EqFlashWindowCmd",
    474: "OP_EqHearPlayFlavorCmd",
    475: "OP_EqUpdateSignWidgetCmd",
    477: "OP_EqShowBookCmd",
    478: "OP_EqQuestionnaireCmd",
    480: "OP_EqHearHealCmd",
    481: "OP_EqChatChannelUpdateCmd",
    482: "OP_EqWhoChannelQueryReplyCmd",
    483: "OP_EqAvailWorldChannelsCmd",
    484: "OP_EqUpdateTargetCmd",
    485: "OP_EqConsignmentItemsCmd",
    486: "OP_EqStartBrokerCmd",
    487: "OP_EqMapExplorationCmd",
    488: "OP_EqStoreLogCmd",
    489: "OP_EqSpellMoveToRangeAndRetryCmd",
    490: "OP_EqUpdatePlayerMailCmd",
    491: "VeArenaResultsCmd",
    492: "VeGuildUpdateCmd",
    493: "VeGuildBankUpdateCmd",
    494: "OP_EqHearSpellNoLandCmd",
    495: "OP_Lottery",
}

LOGIN_REPLY_CODE_MEANINGS = {
    0: "accepted",
    1: "invalid username or password",
    2: "account is already playing",
    6: "client version mismatch",
    7: "no scheduled playtimes",
    8: "account lacks required server features",
    10: "accepted/world-list flag path used by EQ2Emu",
    11: "client build mismatch",
    12: "password update required",
}

CREATE_CHARACTER_REPLY_CODE_MEANINGS = {
    1: "success",
    2: "no servers available",
    3: "character creation request pending",
    4: "maximum characters reached",
    5: "invalid race",
    6: "invalid class",
    7: "invalid gender",
    8: "name too short or too long",
    9: "name contains non-letter characters",
    10: "reserved or naughty name",
    11: "name already taken",
    12: "servers overloaded",
    13: "unknown error / rerun patcher",
    14: "missing Station Exchange features",
}

DELETE_CHARACTER_REPLY_CODE_MEANINGS = {
    1: "success",
    3: "character not found",
}

PLAY_CHARACTER_REPLY_CODE_MEANINGS = {
    1: "success",
    2: "requested world not found",
    3: "zone unavailable or no server for character zone",
    4: "game server rejected or failed to fetch character",
    5: "requested character not found",
    6: "account in use or character already in world",
    7: "server timeout or backlog",
    9: "technical difficulty loading character",
    10: "Station Exchange feature mismatch",
    11: "invalid character class",
}

CLIENT_DERIVED_PACKETPARSER_DRIFT_IDS = set(range(20, 411))
CLIENT_COMPRESSED_LOG_BLOB_TYPES = {213, 214, 215, 216, 217}


@dataclass
class Packet:
    session: int
    seq: int
    direction: str = "unknown"
    type_id: int | None = None
    type_name: str | None = None
    length: int | None = None
    body_hex: str | None = None
    field_events: list[dict[str, Any]] = field(default_factory=list)
    string_events: list[dict[str, Any]] = field(default_factory=list)


def parse_json_line(line: str) -> dict[str, Any] | None:
    start = line.find("{")
    if start < 0:
        return None
    try:
        value = json.loads(line[start:])
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None


def iter_records(paths: Iterable[Path]) -> Iterable[dict[str, Any]]:
    session = 0
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                record = parse_json_line(line)
                if record is None:
                    continue
                if record.get("event") == "logger_started":
                    session += 1
                if session == 0:
                    session = 1
                record["_session"] = session
                record["_source"] = str(path)
                yield record


def set_type(packet: Packet, record: dict[str, Any]) -> None:
    type_id = record.get("type_id")
    if isinstance(type_id, int):
        packet.type_id = type_id
    elif isinstance(type_id, str) and type_id.isdigit():
        packet.type_id = int(type_id)

    type_name = record.get("type_name")
    if isinstance(type_name, str) and type_name:
        packet.type_name = type_name


def build_packets(records: Iterable[dict[str, Any]]) -> dict[tuple[int, int], Packet]:
    packets: dict[tuple[int, int], Packet] = {}
    for record in records:
        seq = record.get("seq")
        session = record.get("_session")
        if not isinstance(seq, int) or not isinstance(session, int):
            continue

        key = (session, seq)
        packet = packets.setdefault(key, Packet(session=session, seq=seq))
        event = record.get("event")

        direction = record.get("direction")
        if isinstance(direction, str) and direction:
            packet.direction = direction
        set_type(packet, record)

        if event == "packet_clear":
            length = record.get("length")
            body_hex = record.get("body_hex")
            if isinstance(length, int):
                packet.length = length
            if isinstance(body_hex, str) and is_hex(body_hex):
                if packet.body_hex is None or len(body_hex) > len(packet.body_hex):
                    packet.body_hex = body_hex
        elif event in {"field_read", "field_write"}:
            packet.field_events.append(record)
        elif event in {"string_read", "string_write"}:
            packet.string_events.append(record)

    return packets


def load_opcode_names(path: Path | None) -> dict[int, str]:
    if path is None or not path.exists():
        return {}

    names: dict[int, str] = {}
    with path.open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            try:
                opcode = int(row["opcode"])
            except (KeyError, ValueError):
                continue
            name = row.get("name", "")
            if name:
                names[opcode] = name
    return names


def load_packetparser_structs(
    path: Path | None,
) -> tuple[dict[int, list[dict[str, Any]]], dict[str, dict[str, Any]]]:
    if path is None or not path.exists():
        return {}, {}

    values = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(values, list):
        return {}, {}

    by_opcode: dict[int, list[dict[str, Any]]] = {}
    by_name: dict[str, dict[str, Any]] = {}
    for item in values:
        if not isinstance(item, dict):
            continue

        name = item.get("name")
        if isinstance(name, str) and name:
            by_name[name] = item

        opcode = item.get("opcode")
        if isinstance(opcode, int):
            by_opcode.setdefault(opcode, []).append(item)

    for candidates in by_opcode.values():
        candidates.sort(
            key=lambda item: (
                1 if item.get("opcode_type") else 0,
                str(item.get("opcode_type", "")),
                str(item.get("name", "")),
            )
        )
    return by_opcode, by_name


def is_hex(text: str) -> bool:
    if len(text) % 2 != 0:
        return False
    try:
        bytes.fromhex(text)
    except ValueError:
        return False
    return True


def read_packed_u16(data: bytes, offset: int = 0) -> tuple[int, int] | None:
    if offset >= len(data):
        return None
    first = data[offset]
    if first != 0xFF:
        return first, 1
    if offset + 3 > len(data):
        return None
    return int.from_bytes(data[offset + 1 : offset + 3], "little"), 3


def read_u(data: bytes, offset: int, size: int) -> int | None:
    if offset + size > len(data):
        return None
    return int.from_bytes(data[offset : offset + size], "little", signed=False)


def read_i(data: bytes, offset: int, size: int) -> int | None:
    if offset + size > len(data):
        return None
    return int.from_bytes(data[offset : offset + size], "little", signed=True)


def read_float(data: bytes, offset: int) -> float | None:
    if offset + 4 > len(data):
        return None
    return struct.unpack_from("<f", data, offset)[0]


def reverse_eq2_packed_stream(data: bytes) -> bytes:
    """Mirror EQ2Emu's Reverse() pass used by Pack/Unpack for version > 373."""
    buffer = bytearray(data)
    remaining = len(buffer)
    real_pos = 0
    orig_pos = 0
    reverse_count = 0
    while remaining > 0:
        if real_pos >= len(buffer):
            break
        code = buffer[real_pos]
        real_pos += 1
        remaining -= 1
        if code >= 0x80:
            for _index in range(7):
                if code & 1:
                    if remaining == 0:
                        return bytes(buffer)
                    remaining -= 1
                    real_pos += 1
                    reverse_count += 1
                code >>= 1
        if reverse_count > 0:
            start = orig_pos + 1
            end = start + reverse_count
            buffer[start:end] = reversed(buffer[start:end])
            reverse_count = 0
        orig_pos = real_pos
    return bytes(buffer)


def unpack_eq2_packed_stream(
    packed: bytes, expected_size: int, *, reverse: bool = True
) -> tuple[bytes, bool, str | None]:
    """Unpack the zero-run/literal stream returned by EQ2Emu Pack(), excluding the u32 size header."""
    if expected_size < 0:
        return b"", False, "negative expected size"
    data = reverse_eq2_packed_stream(packed) if reverse else packed
    output = bytearray(expected_size)
    source_remaining = len(data)
    real_pos = 0
    out_pos = 0
    while source_remaining and out_pos < expected_size:
        if real_pos >= len(data):
            return bytes(output), False, "source exhausted before code byte"
        source_remaining -= 1
        code = data[real_pos]
        real_pos += 1
        if code >= 0x80:
            for _index in range(7):
                if code & 1:
                    if out_pos >= expected_size:
                        return bytes(output), False, "literal exceeds expected output"
                    if source_remaining == 0 or real_pos >= len(data):
                        return bytes(output), False, "source exhausted before literal byte"
                    source_remaining -= 1
                    output[out_pos] = data[real_pos]
                    out_pos += 1
                    real_pos += 1
                else:
                    if out_pos < expected_size:
                        out_pos += 1
                code >>= 1
        else:
            if out_pos + code > expected_size:
                return bytes(output), False, "zero run exceeds expected output"
            out_pos += code
    if source_remaining != 0:
        return bytes(output), False, "packed stream has trailing bytes"
    return bytes(output), True, None


def read_len_string(data: bytes, offset: int, len_size: int) -> tuple[str, int, int] | None:
    if len_size < 1 or len_size > 4 or offset + len_size > len(data):
        return None
    size = read_u(data, offset, len_size)
    if size is None or offset + len_size + size > len(data):
        return None
    raw = data[offset + len_size : offset + len_size + size]
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        text = raw.decode("latin-1", errors="replace")
    return text, size, len_size + size


def printable_ratio(data: bytes) -> float:
    if not data:
        return 1.0
    printable = sum(1 for byte in data if byte in PRINTABLE or byte >= 0x80)
    return printable / len(data)


def add_field(
    fields: list[dict[str, Any]],
    offset: int,
    size: int,
    kind: str,
    name: str,
    value: Any,
) -> None:
    fields.append(
        {
            "offset": offset,
            "size": size,
            "kind": kind,
            "name": name,
            "value": value,
        }
    )


PRIMITIVE_TYPES: dict[str, tuple[int, bool]] = {
    "int8": (1, False),
    "int16": (2, False),
    "int32": (4, False),
    "int64": (8, False),
    "sint8": (1, True),
    "sint16": (2, True),
    "sint32": (4, True),
    "sint64": (8, True),
}


def parse_int(text: Any, default: int = 1) -> int:
    if text is None:
        return default
    try:
        return int(str(text), 0)
    except ValueError:
        return default


def is_truthy_struct_value(value: Any) -> bool:
    if value is None:
        return False
    if isinstance(value, (list, tuple)):
        return any(is_truthy_struct_value(item) for item in value)
    if isinstance(value, dict):
        return any(is_truthy_struct_value(item) for item in value.values())
    return bool(value)


def indexed_variable_name(name: str, array_index: int | None) -> str:
    if array_index is None:
        return name
    return name.replace("%i", str(array_index))


def packetparser_condition_passes(
    field_def: dict[str, Any], values: dict[str, Any], array_index: int | None
) -> bool:
    must_be_set = field_def.get("IfVariableSet")
    if isinstance(must_be_set, str) and must_be_set:
        if not is_truthy_struct_value(values.get(indexed_variable_name(must_be_set, array_index))):
            return False

    must_not_be_set = field_def.get("IfVariableNotSet")
    if isinstance(must_not_be_set, str) and must_not_be_set:
        if is_truthy_struct_value(values.get(indexed_variable_name(must_not_be_set, array_index))):
            return False

    return True


def remember_struct_value(
    values: dict[str, Any], name: str, value: Any, array_index: int | None
) -> None:
    values[name] = value
    if array_index is not None:
        values[f"{name}_{array_index}"] = value


def decode_oversized_integer(
    data: bytes, offset: int, type_name: str, field_def: dict[str, Any]
) -> tuple[Any, int, str] | None:
    if "OversizedValue" not in field_def or "OversizedByte" not in field_def:
        return None

    marker = parse_int(field_def.get("OversizedByte"), default=-1) & 0xFF
    if offset >= len(data):
        return None

    first = data[offset]
    signed = type_name.startswith("sint")
    if first != marker:
        if type_name in {"int16", "sint16"}:
            value = int.from_bytes(bytes([first]), "little", signed=signed)
            return value, 1, f"{type_name}_oversized"
        if type_name in {"int32", "sint32"} and offset + 2 <= len(data):
            value = int.from_bytes(data[offset : offset + 2], "little", signed=signed)
            return value, 2, f"{type_name}_oversized"
        return None

    size = 2 if type_name in {"int16", "sint16"} else 4
    if offset + 1 + size > len(data):
        return None
    value = int.from_bytes(data[offset + 1 : offset + 1 + size], "little", signed=signed)
    return value, 1 + size, f"{type_name}_oversized"


def decode_packetparser_scalar(
    data: bytes, offset: int, type_name: str, field_def: dict[str, Any]
) -> tuple[Any, int, str] | None:
    normalized_type = type_name.strip()
    if normalized_type == "EQ2_16Bit_string":
        normalized_type = "EQ2_16Bit_String"

    oversized = decode_oversized_integer(data, offset, normalized_type, field_def)
    if oversized is not None:
        return oversized

    if normalized_type in PRIMITIVE_TYPES:
        size, signed = PRIMITIVE_TYPES[normalized_type]
        value = read_i(data, offset, size) if signed else read_u(data, offset, size)
        if value is None:
            return None
        return value, size, normalized_type

    if normalized_type == "float":
        value = read_float(data, offset)
        if value is None:
            return None
        return round(value, 6), 4, "float"

    if normalized_type == "EQ2_8Bit_String":
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return None
        text, _, consumed = decoded
        return text, consumed, "string_u8"

    if normalized_type == "EQ2_16Bit_String":
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return None
        text, _, consumed = decoded
        return text, consumed, "string_u16"

    if normalized_type == "EQ2_32Bit_String":
        decoded = read_len_string(data, offset, 4)
        if decoded is None:
            return None
        text, _, consumed = decoded
        return text, consumed, "string_u32"

    if normalized_type == "EQ2_Color":
        if offset + 3 > len(data):
            return None
        return {
            "red": data[offset],
            "green": data[offset + 1],
            "blue": data[offset + 2],
        }, 3, "EQ2_Color"

    return None


def decode_packetparser_field(
    data: bytes,
    offset: int,
    field_def: dict[str, Any],
    fields: list[dict[str, Any]],
    values: dict[str, Any],
    structs_by_name: dict[str, dict[str, Any]],
    prefix: str,
    array_index: int | None,
    depth: int,
) -> int | None:
    if depth > 24:
        return offset
    if not packetparser_condition_passes(field_def, values, array_index):
        return offset

    name = str(field_def.get("ElementName") or field_def.get("Substruct") or field_def.get("SubStruct") or "field")
    full_name = f"{prefix}.{name}" if prefix else name
    type_name = field_def.get("Type")
    substruct_name = field_def.get("Substruct") or field_def.get("SubStruct")

    if type_name == "Array":
        count_name = field_def.get("ArraySizeVariable")
        count = values.get(str(count_name), 0)
        if not isinstance(count, int):
            count = 0
        start = offset
        children = field_def.get("children") if isinstance(field_def.get("children"), list) else []
        for item_index in range(max(0, count)):
            item_prefix = f"{full_name}[{item_index}]"
            for child in children:
                if not isinstance(child, dict):
                    continue
                next_offset = decode_packetparser_field(
                    data,
                    offset,
                    child,
                    fields,
                    values,
                    structs_by_name,
                    item_prefix,
                    item_index,
                    depth + 1,
                )
                if next_offset is None:
                    add_field(fields, offset, 0, "array_parse_stop", item_prefix, "not enough data")
                    return offset
                offset = next_offset
        add_field(fields, start, offset - start, "array", full_name, f"{count} items")
        return offset

    if isinstance(substruct_name, str) and substruct_name:
        substruct = structs_by_name.get(substruct_name)
        if substruct is None:
            add_field(fields, offset, 0, "unsupported_substruct", full_name, substruct_name)
            return None
        count = parse_int(field_def.get("Size"), default=1)
        start = offset
        children = substruct.get("fields") if isinstance(substruct.get("fields"), list) else []
        for item_index in range(max(1, count)):
            item_prefix = full_name if count == 1 else f"{full_name}[{item_index}]"
            for child in children:
                if not isinstance(child, dict):
                    continue
                next_offset = decode_packetparser_field(
                    data,
                    offset,
                    child,
                    fields,
                    values,
                    structs_by_name,
                    item_prefix,
                    item_index,
                    depth + 1,
                )
                if next_offset is None:
                    add_field(fields, offset, 0, "substruct_parse_stop", item_prefix, "not enough data")
                    return offset
                offset = next_offset
        add_field(fields, start, offset - start, "substruct", full_name, substruct_name)
        return offset

    if not isinstance(type_name, str):
        add_field(fields, offset, 0, "unsupported", full_name, dict(field_def))
        return None

    if type_name == "char":
        count = parse_int(field_def.get("Size"), default=1)
        if offset + count > len(data):
            return None
        raw = data[offset : offset + count]
        text = raw.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
        add_field(fields, offset, count, f"char[{count}]", full_name, text)
        remember_struct_value(values, name, text, array_index)
        return offset + count

    if type_name == "EQ2_Item":
        add_field(fields, offset, 0, "unsupported", full_name, "EQ2_Item size is runtime-defined")
        return None

    count = parse_int(field_def.get("Size"), default=1)
    if type_name.startswith("EQ2_") and "String" in type_name:
        count = max(1, count)

    start = offset
    values_read: list[Any] = []
    kind = type_name
    for _ in range(max(1, count)):
        decoded = decode_packetparser_scalar(data, offset, type_name, field_def)
        if decoded is None:
            return None
        value, consumed, kind = decoded
        values_read.append(value)
        offset += consumed

    value: Any = values_read[0] if len(values_read) == 1 else values_read
    add_field(fields, start, offset - start, kind, full_name, value)
    remember_struct_value(values, name, value, array_index)
    return offset


def select_packetparser_struct(
    type_id: int, structs_by_opcode: dict[int, list[dict[str, Any]]]
) -> tuple[dict[str, Any] | None, int]:
    candidates = structs_by_opcode.get(type_id, [])
    if not candidates:
        return None, 0
    plain = [item for item in candidates if not item.get("opcode_type")]
    return (plain or candidates)[0], len(candidates)


def decode_packetparser_struct(
    data: bytes,
    start: int,
    type_id: int,
    fields: list[dict[str, Any]],
    structs_by_opcode: dict[int, list[dict[str, Any]]],
    structs_by_name: dict[str, dict[str, Any]],
) -> tuple[int, dict[str, Any] | None]:
    struct_def, candidate_count = select_packetparser_struct(type_id, structs_by_opcode)
    if struct_def is None:
        return start, None

    offset = start
    values: dict[str, Any] = {}
    start_field_count = len(fields)
    struct_fields = struct_def.get("fields") if isinstance(struct_def.get("fields"), list) else []
    for field_def in struct_fields:
        if not isinstance(field_def, dict):
            continue
        next_offset = decode_packetparser_field(
            data,
            offset,
            field_def,
            fields,
            values,
            structs_by_name,
            "",
            None,
            0,
        )
        if next_offset is None:
            break
        offset = next_offset

    if len(fields) == start_field_count:
        return start, None

    return offset, {
        "name": struct_def.get("name", ""),
        "opcode_name": struct_def.get("opcode_name", ""),
        "opcode_type": struct_def.get("opcode_type", ""),
        "candidate_count": candidate_count,
    }


def decode_type_0(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Login-ish message seen in the capture."""
    offset = start
    labels = [
        "access_code_or_station_name",
        "unknown_string_1",
        "account_name",
        "password",
    ]
    for label in labels:
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", label, text)
        offset += consumed

    for label in ["unknown_u32_0", "unknown_u32_1"]:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", label, value)
        offset += 4

    value = read_u(data, offset, 2)
    if value is not None:
        add_field(fields, offset, 2, "u16", "client_build_or_revision", value)
        offset += 2
    return offset


def decode_type_2(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Client registry type 2 / OP_WSLoginRequestMsg layout."""
    offset = start

    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "account_or_session_id", value)
    offset += 4

    for name in ("version_0", "version_1", "version_2"):
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", name, value)
        offset += 2

    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag_0", value)
    offset += 1

    offset = decode_string16_fields(data, offset, fields, ("string_0", "string_1"))

    for name in ("flag_1", "flag_2", "flag_3"):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", name, value)
        offset += 1

    for name in ("u32_0", "u32_1"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", name, value)
        offset += 4

    return offset


def decode_type_3(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Client registry type 3 / OP_ESLoginRequestMsg layout."""
    offset = start

    for name in ("string_0", "string_1", "string_2"):
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", name, text)
        offset += consumed

    first_bool = read_u(data, offset, 1)
    if first_bool is None:
        return offset
    add_field(fields, offset, 1, "u8", "bool_0", first_bool)
    offset += 1

    second_bool = read_u(data, offset, 1)
    if second_bool is None:
        return offset
    add_field(fields, offset, 1, "u8", "bool_1", second_bool)
    offset += 1

    if first_bool == 0:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "optional_u32", value)
        offset += 4

    for name in ("version_0", "version_1", "version_2"):
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", name, value)
        offset += 2

    return offset


def decode_type_4(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Client-primary OP_LoginReplyMsg layout from the constructed message vtable."""
    offset = start

    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "reply_code", value)
    offset += 1

    for name in ("unknown_string_0",):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", name, text)
        offset += consumed

    layout = [
        ("u8", "parental_control_flag", 1, False),
        ("bytes", "unknown_8_bytes", 8, False),
        ("u32", "unknown_u32_0", 4, False),
        ("u32", "account_id", 4, False),
    ]
    for kind, name, size, _signed in layout:
        value = data[offset : offset + size].hex() if kind == "bytes" else read_u(data, offset, size)
        if value is None or offset + size > len(data):
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", "unknown_string_1", text)
    offset += consumed

    for name in ("reset_appearance", "do_not_force_soga"):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", name, value)
        offset += 1
    if offset < len(data):
        add_field(
            fields,
            offset,
            len(data) - offset,
            "bytes",
            "ignored_server_tail",
            data[offset:].hex(),
        )
        offset = len(data)
    return offset


def decode_type_5(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Client registry type 5 / likely OP_WSStatusReplyMsg layout."""
    offset = start
    for kind, name, size in (
        ("u16", "u16_0", 2),
        ("u8", "u8_0", 1),
        ("u8", "u8_1", 1),
        ("u8", "u8_2", 1),
        ("u32", "u32_0", 4),
        ("u32", "u32_1", 4),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_float_triplet(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = start
    for component in ("r", "g", "b"):
        value = read_float(data, offset)
        if value is None:
            return offset
        add_field(fields, offset, 4, "float", f"{prefix}.{component}", value)
        offset += 4
    return offset


def decode_create_character_profile(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = start
    server_id = read_u(data, offset, 4)
    if server_id is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.server_id", server_id)
    offset += 4

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.character_name", text)
    offset += consumed

    for name in (
        "race",
        "gender",
        "deity",
        "class",
        "level",
        "legacy_unknown_0",
        "legacy_unknown_1",
        "customization_version",
    ):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.{name}", value)
        offset += 1

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.race_file", text)
    offset += consumed

    for name in ("skin_color", "eye_color", "hair_color1", "hair_color2", "hair_highlight"):
        next_offset = decode_float_triplet(data, offset, fields, f"{prefix}.{name}")
        if next_offset == offset:
            return offset
        offset = next_offset

    for file_name, colors in (
        ("hair_file", ("hair_type_color", "hair_type_highlight_color")),
        ("face_file", ("hair_face_color", "hair_face_highlight_color")),
        ("chest_file", ("shirt_color", "unknown_chest_color")),
        (
            "legs_file",
            (
                "pants_color",
                "unknown_legs_color",
                "unknown9",
                "eye_type",
                "ear_type",
                "eye_brow_type",
                "cheek_type",
                "lip_type",
                "chin_type",
                "nose_type",
            ),
        ),
    ):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"{prefix}.{file_name}", text)
        offset += consumed
        for color_name in colors:
            next_offset = decode_float_triplet(data, offset, fields, f"{prefix}.{color_name}")
            if next_offset == offset:
                return offset
            offset = next_offset

    for name in ("body_size", "body_age"):
        value = read_float(data, offset)
        if value is None:
            return offset
        add_field(fields, offset, 4, "float", f"{prefix}.{name}", value)
        offset += 4
    return offset


def decode_create_character_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        ((1, "u8", "legacy_flag"), (4, "u32", "u32_0"), (4, "u32", "account_id")),
    )
    if offset == start:
        return offset
    return decode_create_character_profile(data, offset, fields, "profile")


def decode_ws_create_character_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "account_id", value)
    offset = decode_create_character_profile(data, start + 4, fields, "profile")
    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", "trailing_flag", value)
        offset += 1
    return offset


def decode_reskin_character_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        ((4, "u32", "u32_0"), (4, "u32", "u32_1")),
    )
    if offset == start:
        return offset
    offset = decode_create_character_profile(data, offset, fields, "profile")
    for name in ("trailing_flag_0", "trailing_flag_1"):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", name, value)
        offset += 1
    return offset


def decode_play_character_reply(data: bytes, start: int, fields: list[dict[str, Any]], include_u32_before_success: bool) -> int:
    response = read_u(data, start, 1)
    if response is None:
        return start
    add_field(fields, start, 1, "u8", "response", response)
    offset = start + 1
    if include_u32_before_success:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "u32_0", value)
        offset += 4
    if response != 1:
        return offset
    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", "address", text)
    offset += consumed
    for size, kind, name in ((2, "u16", "port"), (4, "u32", "account_id")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    if not include_u32_before_success:
        value = read_u(data, offset, 4)
        if value is not None:
            add_field(fields, offset, 4, "u32", "passcode", value)
            offset += 4
    return offset


def decode_server_play_character_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(data, start, fields, ((4, "u32", "u32_0"), (4, "u32", "u32_1")))
    if offset == start:
        return offset
    offset = decode_string16_fields(data, offset, fields, ("text_0", "text_1", "text_2", "text_3"))
    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "id_count", count)
    offset += 4
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"id[{index}]", value)
        offset += 4
    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", "trailing_flag", value)
        offset += 1
    return offset


def decode_es_init(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    offset = start + 4
    for name, len_size in (("text8_0", 1), ("text8_1", 1), ("text16_0", 2), ("text16_1", 2)):
        decoded = read_len_string(data, offset, len_size)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, f"string_u{len_size * 8}", name, text)
        offset += consumed
    offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_1"), (4, "u32", "u32_2")))
    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "guild_count", count)
    offset += 4
    if count:
        if offset < len(data):
            add_field(fields, offset, len(data) - offset, "bytes", "guild_and_character_lists_raw", f"{len(data) - offset} bytes")
            offset = len(data)
        return offset
    group_count = read_u(data, offset, 2)
    if group_count is None:
        return offset
    add_field(fields, offset, 2, "u16", "character_group_count", group_count)
    offset += 2
    for group_index in range(group_count):
        group_id = read_u(data, offset, 4)
        if group_id is None:
            return offset
        add_field(fields, offset, 4, "u32", f"group[{group_index}].id", group_id)
        offset += 4
        member_count = read_u(data, offset, 2)
        if member_count is None:
            return offset
        add_field(fields, offset, 2, "u16", f"group[{group_index}].member_count", member_count)
        offset += 2
        for member_index in range(member_count):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"group[{group_index}].member[{member_index}]", value)
            offset += 4
    return offset


def decode_five_string16_four_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text_0", "text_1", "text_2", "text_3", "text_4"))
    if offset == start:
        return offset
    return decode_scalar_layout(
        data,
        offset,
        fields,
        ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (4, "u32", "u32_2"), (4, "u32", "u32_3")),
    )


def decode_string16_u8(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text",))
    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", "flag", value)
        offset += 1
    return offset


def decode_expect_client_as_character_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(data, start, fields, ((4, "u32", "u32_0"), (4, "u32", "u32_1")))
    if offset == start:
        return offset
    offset = decode_string16_fields(data, offset, fields, ("text_0",))
    has_optional_block = read_u(data, offset, 1)
    if has_optional_block is None:
        return offset
    add_field(fields, offset, 1, "u8", "has_optional_block", has_optional_block)
    offset += 1

    offset = decode_string16_fields(data, offset, fields, ("text_1", "text_2", "text_3"))
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (4, "u32", "u32_2"),
            (4, "u32", "u32_3"),
            (4, "u32", "u32_4"),
            (4, "u32", "u32_5"),
        ),
    )
    if has_optional_block:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (4, "u32", "optional_u32_0"),
                (4, "u32", "optional_u32_1"),
                (4, "u32", "optional_u32_2"),
                (4, "u32", "optional_u32_3"),
            ),
        )
    offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_6"),))
    offset = decode_string16_fields(data, offset, fields, ("text_4",))
    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "id_count", count)
    offset += 4
    for index in range(min(count, 1024)):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"id[{index}]", value)
        offset += 4
    if count > 1024:
        remaining = (count - 1024) * 4
        if offset + remaining <= len(data):
            add_field(fields, offset, remaining, "bytes", "id_remaining", f"{count - 1024} u32 values")
            offset += remaining
    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", "trailing_flag", value)
        offset += 1
    return offset


def decode_zone_info_slideshow(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        (
            (4, "u32", f"{prefix}.u32_0"),
            (4, "u32", f"{prefix}.u32_1"),
            (1, "u8", f"{prefix}.slide_count"),
        ),
    )
    slide_count_field = next((field for field in fields if field["offset"] == offset - 1 and field["name"] == f"{prefix}.slide_count"), None)
    if slide_count_field is None:
        return offset
    slide_count = int(slide_count_field["value"])
    for slide_index in range(slide_count):
        for name in ("u32_0", "u32_1", "u32_2", "u32_3", "u32_4", "u32_5"):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"{prefix}.slide[{slide_index}].{name}", value)
            offset += 4
        for name in ("text8_0", "text8_1"):
            decoded = read_len_string(data, offset, 1)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u8", f"{prefix}.slide[{slide_index}].{name}", text)
            offset += consumed
        value = read_u(data, offset, 8)
        if value is None:
            return offset
        add_field(fields, offset, 8, "u64", f"{prefix}.slide[{slide_index}].u64_0", value)
        offset += 8
        nested_count = read_u(data, offset, 1)
        if nested_count is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.slide[{slide_index}].nested_count", nested_count)
        offset += 1
        for nested_index in range(nested_count):
            for value_index in range(4):
                value = read_u(data, offset, 4)
                if value is None:
                    return offset
                add_field(
                    fields,
                    offset,
                    4,
                    "u32",
                    f"{prefix}.slide[{slide_index}].nested[{nested_index}].u32_{value_index}",
                    value,
                )
                offset += 4
    return offset


def decode_zone_info(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for name in (
        "text8_0",
        "text8_1",
        "text8_2",
        "text8_3",
        "text8_4",
        "text8_5",
        "text8_6",
        "text8_7",
        "text8_8",
    ):
        if name == "text8_2":
            offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (4, "u32", "u32_1")))
        elif name == "text8_3":
            offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_2"),))
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", name, text)
        offset += consumed

    if offset + 12 > len(data):
        return offset
    add_field(fields, offset, 12, "bytes", "raw_0c", data[offset : offset + 12].hex())
    offset += 12

    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (2, "u16", "u16_0"),
            (1, "u8", "flag_0"),
            (1, "u8", "flag_1"),
            (1, "u8", "flag_2"),
            (1, "u8", "flag_3"),
            (1, "u8", "flag_4"),
        ),
    )
    offset = decode_zone_info_slideshow(data, offset, fields, "slideshow")
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        ((4, "u32", "u32_3"), (4, "u32", "u32_4"), (4, "u32", "u32_5"), (4, "u32", "u32_6")),
    )
    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "entry_count", count)
    offset += 4
    for index in range(min(count, 1024)):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"entry[{index}].text", text)
        offset += consumed
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"entry[{index}].u16", value)
        offset += 2
    if count > 1024 and offset < len(data):
        add_field(fields, offset, len(data) - offset, "bytes", "entries_remaining_raw", f"{len(data) - offset} bytes")
        offset = len(data)
    return offset


def decode_type_28(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Likely OP_ExpectClientAsCharacterReplyMsg layout."""
    offset = start

    for kind, name, size in (
        ("u8", "reply_code", 1),
        ("u32", "account_id", 4),
        ("u32", "character_id", 4),
        ("u16", "u16_0", 2),
        ("u32", "u32_0", 4),
        ("u32", "u32_1", 4),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    offset = decode_string16_fields(data, offset, fields, ("character_name",))

    for name in ("u32_2", "u32_3", "u32_4", "u32_5"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", name, value)
        offset += 4

    for name in ("flag_0", "flag_1"):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", name, value)
        offset += 1

    return offset


def decode_type_30(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Client registry type 30 counted u16 list."""
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "value_count", count)
    offset = start + 4
    if count > (len(data) - offset) // 2:
        return offset
    for index in range(count):
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"value[{index}]", value)
        offset += 2
    return offset


def decode_prediction_buffer(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Client registry type 34 / VePredictionUpdateMsg nested blob envelope."""
    packed = read_packed_u16(data, start)
    if packed is None:
        return start
    subtype_id, consumed = packed
    add_field(fields, start, consumed, "packed_u16", "prediction_subtype_id", subtype_id)
    offset = start + consumed

    size_offset = offset + 4
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        ((4, "u32", "u32_0"), (4, "u32", "prediction_blob_size")),
    )
    blob_size = read_u(data, size_offset, 4)
    if blob_size is None:
        return offset
    if offset + blob_size > len(data):
        return offset
    add_field(fields, offset, blob_size, "bytes", "prediction_blob", f"{blob_size} bytes")
    return offset + blob_size


def decode_set_remote_cmds(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Client registry type 35 / OP_SetRemoteCmdsMsg two string8-style lists."""
    offset = start
    for list_name in ("primary", "secondary"):
        count = read_u(data, offset, 2)
        if count is None:
            return offset
        add_field(fields, offset, 2, "u16", f"{list_name}_count", count)
        offset += 2
        for index in range(count):
            decoded = read_len_string(data, offset, 1)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u8", f"{list_name}[{index}]", text)
            offset += consumed
    return offset


def decode_type_8(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "world_count", count)
    offset += 1
    for index in range(count):
        prefix = f"world[{index}]"
        world_id = read_i(data, offset, 4)
        if world_id is None:
            return offset
        add_field(fields, offset, 4, "i32", f"{prefix}.world_id", world_id)
        offset += 4

        next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.display_name",))
        if next_offset == offset:
            return offset
        offset = next_offset
        next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.secondary_name",))
        if next_offset == offset:
            return offset
        offset = next_offset

        for name in ("tag", "locked", "hidden", "unknown"):
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"{prefix}.{name}", value)
            offset += 1

        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"{prefix}.num_players", value)
        offset += 2

        for name in ("load", "number_online_flag", "feature_set_or_unknown2"):
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"{prefix}.{name}", value)
            offset += 1

        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.allowed_races", value)
        offset += 4
    return offset


def decode_net_appearance_v11(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    """NetAppearance block serialized by the DoF character-list entry helper."""
    version = read_u(data, start, 1)
    if version is None:
        return start
    add_field(fields, start, 1, "u8", f"{prefix}.net_appearance_version", version)
    block_size = 337
    if start + block_size > len(data):
        return start + 1

    def add_u(offset: int, size: int, name: str) -> None:
        value = read_u(data, offset, size)
        if value is not None:
            add_field(fields, offset, size, f"u{size * 8}", name, value)

    def add_triplet(offset: int, name: str, signed: bool) -> None:
        if offset + 3 > len(data):
            return
        values = [
            read_i(data, offset + index, 1) if signed else read_u(data, offset + index, 1)
            for index in range(3)
        ]
        add_field(fields, offset, 3, "i8[3]" if signed else "u8[3]", name, values)

    add_u(start + 1, 2, f"{prefix}.model_type_obj_0x000")
    add_triplet(start + 3, f"{prefix}.skin_color_obj_0x004", signed=True)
    add_triplet(start + 6, f"{prefix}.eye_color_obj_0x007", signed=True)

    offset = start + 9
    for group_index in range(0x1B):
        group_name = f"{prefix}.appearance_group[{group_index}]"
        add_u(offset, 2, f"{group_name}.type")
        add_triplet(offset + 2, f"{group_name}.color", signed=False)
        add_triplet(offset + 5, f"{group_name}.highlight", signed=False)
        offset += 8

    add_triplet(offset, f"{prefix}.primary_morph.unknown9_obj_0x0f8", signed=True)
    offset += 3
    for morph_name in (
        "eye_type",
        "ear_type",
        "eye_brow_type",
        "cheek_type",
        "lip_type",
        "chin_type",
        "nose_type",
    ):
        add_triplet(offset, f"{prefix}.primary_morph.{morph_name}", signed=True)
        offset += 3
    add_u(offset, 1, f"{prefix}.primary_morph.body_size")
    offset += 1
    add_u(offset, 1, f"{prefix}.primary_morph.body_age")
    offset += 1

    add_u(offset, 2, f"{prefix}.mount_model_obj_0x12c")
    offset += 2
    for color_name in (
        "mount_color1_obj_0x12e",
        "mount_color2_obj_0x131",
        "hair_color1_obj_0x134",
        "hair_color2_obj_0x137",
        "hair_color3_obj_0x13a",
    ):
        add_triplet(offset, f"{prefix}.{color_name}", signed=True)
        offset += 3

    add_u(offset, 2, f"{prefix}.unknown_u16_obj_0x146")
    offset += 2
    add_u(offset, 2, f"{prefix}.unknown_u16_obj_0x148")
    offset += 2
    add_u(offset, 2, f"{prefix}.unknown_u16_obj_0x14a")
    offset += 2
    add_u(offset, 4, f"{prefix}.appearance_flags_obj_0x14c")
    offset += 4

    add_u(offset, 2, f"{prefix}.soga_model_type_obj_0x002")
    offset += 2
    add_triplet(offset, f"{prefix}.soga_skin_color_obj_0x00a", signed=False)
    offset += 3
    add_triplet(offset, f"{prefix}.soga_eye_color_obj_0x00d", signed=False)
    offset += 3

    add_triplet(offset, f"{prefix}.soga_morph.unknown12_obj_0x112", signed=True)
    offset += 3
    for morph_name in (
        "soga_eye_type",
        "soga_ear_type",
        "soga_eye_brow_type",
        "soga_cheek_type",
        "soga_lip_type",
        "soga_chin_type",
        "soga_nose_type",
    ):
        add_triplet(offset, f"{prefix}.soga_morph.{morph_name}", signed=True)
        offset += 3
    add_u(offset, 1, f"{prefix}.soga_morph.body_size")
    offset += 1
    add_u(offset, 1, f"{prefix}.soga_morph.body_age")
    offset += 1

    for color_name in (
        "soga_hair_color1_obj_0x13d",
        "soga_hair_color2_obj_0x140",
        "soga_hair_color3_obj_0x143",
    ):
        add_triplet(offset, f"{prefix}.{color_name}", signed=False)
        offset += 3

    add_u(offset, 2, f"{prefix}.soga_hair_type_obj_0x046")
    offset += 2
    add_triplet(offset, f"{prefix}.soga_hair_type_color_obj_0x0ec", signed=False)
    offset += 3
    add_triplet(offset, f"{prefix}.soga_hair_type_highlight_obj_0x0ef", signed=False)
    offset += 3
    add_u(offset, 2, f"{prefix}.soga_facial_hair_type_obj_0x048")
    offset += 2
    add_triplet(offset, f"{prefix}.soga_hair_face_color_obj_0x0f2", signed=False)
    offset += 3
    add_triplet(offset, f"{prefix}.soga_hair_face_highlight_obj_0x0f5", signed=False)
    return start + block_size


def decode_type_10(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """OP_AllCharactersDescReplyMsg with DoF character entries and trailer."""
    count = read_u(data, start, 1)
    if count is None:
        return start
    add_field(fields, start, 1, "u8", "character_count", count)
    offset = start + 1

    for index in range(count):
        prefix = f"character[{index}]"
        for kind, name, size, signed in (
            ("i32", "character_id", 4, True),
            ("i32", "server_id", 4, True),
        ):
            value = read_i(data, offset, size) if signed else read_u(data, offset, size)
            if value is None:
                return offset
            add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
            offset += size

        next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.name",))
        if next_offset == offset:
            return offset
        offset = next_offset

        for name in ("race", "class", "gender"):
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"{prefix}.{name}", value)
            offset += 1

        value = read_i(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "i32", f"{prefix}.level", value)
        offset += 4

        next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.zone_name",))
        if next_offset == offset:
            return offset
        offset = next_offset

        for u32_index in range(6):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"{prefix}.u32_{u32_index}", value)
            offset += 4

        next_offset = decode_string16_fields(
            data, offset, fields, (f"{prefix}.text_0", f"{prefix}.text_1")
        )
        if next_offset == offset:
            return offset
        offset = next_offset

        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_6", value)
        offset += 4

        next_offset = decode_net_appearance_v11(data, offset, fields, prefix)
        if next_offset == offset:
            return offset
        offset = next_offset

    for kind, name, size, signed in (
        ("u32", "account_id", 4, False),
        ("i32", "sentinel_minus_one", 4, True),
    ):
        value = read_i(data, offset, size) if signed else read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    linked_count = read_u(data, offset, 2)
    if linked_count is None:
        return offset
    add_field(fields, offset, 2, "u16", "linked_server_or_character_id_count", linked_count)
    offset += 2
    for index in range(linked_count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"linked_server_or_character_id[{index}]", value)
        offset += 4

    for kind, name, size in (
        ("u32", "max_character_count_or_version", 4),
        ("u8", "unknown_u8_0", 1),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_type_40(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    """Client registry type 40 fixed-field message."""
    offset = start
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    offset += 4

    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", "text_0", text)
    offset += consumed

    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag_0", value)
    offset += 1

    for index in range(1, 9):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4

    for index in range(1, 3):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"flag_{index}", value)
        offset += 1

    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", "text_1", text)
    offset += consumed

    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", "flag_3", value)
        offset += 1
    return offset


def decode_remote_cmd(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    command_handler = read_u(data, offset, 2)
    if command_handler is None:
        return offset
    add_field(fields, offset, 2, "u16", "command_handler", command_handler)
    offset += 2

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", "arguments", text)
    return offset + consumed


def decode_game_world_time(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    year = read_u(data, offset, 2)
    if year is None:
        return offset
    add_field(fields, offset, 2, "u16", "year", year)
    offset += 2

    for name in ("month", "day", "hour", "minute", "unknown"):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", name, value)
        offset += 1
    return offset


def decode_string16_fields(
    data: bytes, start: int, fields: list[dict[str, Any]], names: tuple[str, ...]
) -> int:
    offset = start
    for name in names:
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", name, text)
        offset += consumed
    return offset


def decode_request_camp(data: bytes, start: int, fields: list[dict[str, Any]], first_name: str = "quit") -> int:
    offset = start
    for name in (first_name, "camp_desktop"):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", name, value)
        offset += 1
    return offset


def decode_state_blob(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        (
            (4, "u32", "u32_0"),
            (1, "u8", "flag_0"),
            (1, "u8", "flag_1"),
            (1, "u8", "flag_2"),
            (1, "u8", "flag_3"),
            (1, "u8", "string_gate"),
        ),
    )
    if offset == start:
        return offset

    string_gate = read_u(data, offset - 1, 1)
    if string_gate == 0:
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", "gated_text", text)
        offset += consumed

    optional_u32_flag = read_u(data, offset, 1)
    if optional_u32_flag is None:
        return offset
    add_field(fields, offset, 1, "u8", "optional_u32_flag", optional_u32_flag)
    offset += 1
    if optional_u32_flag:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "optional_u32", value)
        offset += 4

    for name in ("raw16_0", "raw16_1"):
        if offset + 0x10 > len(data):
            return offset
        add_field(fields, offset, 0x10, "bytes", name, data[offset : offset + 0x10].hex())
        offset += 0x10

    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (4, "u32", "u32_1"),
            (1, "u8", "flag_4"),
            (1, "u8", "flag_5"),
            (1, "u8", "flag_6"),
        ),
    )

    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", "text", text)
    offset += consumed

    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        ((1, "u8", "flag_7"), (1, "u8", "flag_8"), (1, "u8", "flag_9")),
    )
    count = read_u(data, offset, 2)
    if count is None:
        return offset
    add_field(fields, offset, 2, "u16", "id_count", count)
    offset += 2
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"id[{index}]", value)
        offset += 4
    return offset


def decode_fixed_c_string_field(
    data: bytes, start: int, fields: list[dict[str, Any]], name: str, size: int
) -> int:
    if start + size > len(data):
        return start
    raw = data[start : start + size]
    text = raw.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
    add_field(fields, start, size, f"char[{size}]", name, text)
    return start + size


def decode_who_query_character_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_fixed_c_string_field(data, start, fields, f"{prefix}.char_name", 40)
    if offset == start:
        return start
    for size, kind, name in (
        (1, "u8", "unknown3"),
        (1, "u8", "level"),
        (1, "u8", "admin_level"),
        (2, "u16", "class_id"),
        (1, "u8", "unknown4"),
        (1, "u8", "race"),
        (1, "u8", "flags"),
        (4, "u32", "unknown5"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size
    next_offset = decode_fixed_c_string_field(data, offset, fields, f"{prefix}.zone", 80)
    if next_offset == offset:
        return offset
    offset = next_offset
    if offset + 28 > len(data):
        return offset
    add_field(fields, offset, 28, "bytes", f"{prefix}.unknown6", data[offset : offset + 28].hex())
    return offset + 28


def decode_who_query_reply(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for kind, name, size in (
        ("u32", "account_id", 4),
        ("u32", "unknown_u32", 4),
        ("u8", "response", 1),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    response = fields[-1]["value"]
    if response not in {2, 3}:
        return offset

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "character_count", count)
    offset += 1

    for index in range(count):
        next_offset = decode_who_query_character_entry(data, offset, fields, f"character[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset

    display_zone = read_u(data, offset, 1)
    if display_zone is not None:
        add_field(fields, offset, 1, "u8", "display_zone", display_zone)
        offset += 1
    return offset


def decode_fixed_u32_and_raw(
    data: bytes,
    start: int,
    fields: list[dict[str, Any]],
    raw_layout: tuple[str, int, str],
) -> int:
    offset = start
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "unknown_u32_0", value)
    offset += 4

    name, size, label = raw_layout
    if offset + size > len(data):
        return offset
    add_field(fields, offset, size, "bytes", name, label)
    return offset + size


def decode_fixed4c_array(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 2)
    if count is None:
        return start
    add_field(fields, start, 2, "u16", "entry_count", count)
    offset = start + 2
    size = count * 0x4C
    if offset + size > len(data):
        return offset
    add_field(fields, offset, size, "bytes", "entries_raw", f"{count} * 0x4c")
    return offset + size


def decode_raw_blob_with_size(
    data: bytes,
    start: int,
    fields: list[dict[str, Any]],
    *,
    count_name: str | None,
    count_size: int,
    size_name: str,
    blob_name: str,
    trailing_u8_name: str | None = None,
    omit_size_when_count_zero: bool = False,
) -> int:
    offset = start
    count = None
    if count_name is not None:
        count = read_u(data, offset, count_size)
        if count is None:
            return offset
        add_field(fields, offset, count_size, f"u{count_size * 8}", count_name, count)
        offset += count_size
        if omit_size_when_count_zero and count == 0:
            return offset

    blob_size = read_u(data, offset, 4)
    if blob_size is None:
        return offset
    add_field(fields, offset, 4, "u32", size_name, blob_size)
    offset += 4

    if offset + blob_size > len(data):
        return offset
    add_field(fields, offset, blob_size, "bytes", blob_name, f"{blob_size} bytes")
    offset += blob_size

    if trailing_u8_name is not None:
        trailing = read_u(data, offset, 1)
        if trailing is None:
            return offset
        add_field(fields, offset, 1, "u8", trailing_u8_name, trailing)
        offset += 1
    return offset


def decode_inspect_pc_results_cmd(
    data: bytes, start: int, end: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for name in (
        "client_cmd[0].name",
        "client_cmd[0].surname",
        "client_cmd[0].title",
    ):
        if offset >= end:
            return offset
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        if offset + consumed > end:
            return offset
        add_field(fields, offset, consumed, "string_u16", name, text)
        offset += consumed

    for name in ("client_cmd[0].adventure_level", "client_cmd[0].adventure_class"):
        if offset + 2 > end:
            return offset
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", name, value)
        offset += 2

    for index in range(22):
        if offset >= end:
            return offset
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        if offset + consumed > end:
            return offset
        add_field(
            fields,
            offset,
            consumed,
            "string_u16",
            f"client_cmd[0].equipment_name[{index}]",
            text,
        )
        offset += consumed
    return offset


def decode_client_cmd_blob(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    blob_size = read_u(data, start, 4)
    if blob_size is None:
        return start
    add_field(fields, start, 4, "u32", "blob_size", blob_size)
    offset = start + 4

    if offset + blob_size > len(data):
        return offset

    if blob_size:
        packed = read_packed_u16(data, offset)
        if packed is not None:
            nested_type_id, consumed = packed
            add_field(
                fields,
                offset,
                consumed,
                "packed_u16",
                "client_cmd[0].ve_type_id",
                nested_type_id,
            )
            nested_name = CLIENT_CMD_DERIVED_OPCODE_NAMES.get(
                nested_type_id
            ) or CLIENT_DERIVED_OPCODE_NAMES.get(nested_type_id)
            if nested_name is not None:
                add_field(
                    fields,
                    offset,
                    consumed,
                    "opcode_name",
                    "client_cmd[0].ve_type_name",
                    nested_name,
                )
            if nested_type_id == 439:
                decode_inspect_pc_results_cmd(
                    data, offset + consumed, offset + blob_size, fields
                )

    add_field(fields, offset, blob_size, "bytes", "client_cmd_blob", f"{blob_size} bytes")
    return offset + blob_size


SPELL_BOOK_V546_ENTRY_SIZE = 27
CHARACTER_SHEET_V546_SIZE = 4897
INVENTORY_ITEM_V546_ENTRY_SIZE = 108
RAID_MEMBER_V546_ENTRY_SIZE = 137
RAID_UPDATE_V546_SIZE = RAID_MEMBER_V546_ENTRY_SIZE * 24
RECIPE_BOOK_V546_ENTRY_SIZE = 12
SKILL_BOOK_V546_ENTRY_SIZE = 21


def parse_spell_book_v546_delta_entry(blob: bytes, offset: int) -> dict[str, Any]:
    unique_id_raw = read_u(blob, offset + 4, 4) or 0
    return {
        "spell_id": read_u(blob, offset, 4),
        "unique_id": int.from_bytes(unique_id_raw.to_bytes(4, "little"), "little", signed=True),
        "recast_available": read_u(blob, offset + 8, 4),
        "type": read_u(blob, offset + 12, 1),
        "recast_time": read_u(blob, offset + 13, 2),
        "unknown3": read_u(blob, offset + 15, 1),
        "unknown4": read_u(blob, offset + 16, 2),
        "icon": read_i(blob, offset + 18, 2),
        "icon_type": read_u(blob, offset + 20, 2),
        "icon2": read_u(blob, offset + 22, 2),
        "charges": read_u(blob, offset + 24, 1),
        "unknown5": read_u(blob, offset + 25, 1),
        "status": read_u(blob, offset + 26, 1),
    }


def decode_spell_book_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 2)
    if count is None:
        return start
    add_field(fields, start, 2, "u16", "spell_count", count)
    offset = start + 2

    packed_size = read_u(data, offset, 4)
    if packed_size is None:
        return offset
    add_field(fields, offset, 4, "u32", "packed_size", packed_size)
    offset += 4

    if offset + packed_size > len(data):
        return offset
    packed_blob = data[offset : offset + packed_size]
    add_field(fields, offset, packed_size, "bytes", "spell_book_packed_delta", f"{packed_size} bytes")

    expected_size = count * SPELL_BOOK_V546_ENTRY_SIZE
    unpacked, unpacked_ok, unpack_error = unpack_eq2_packed_stream(packed_blob, expected_size)
    summary: dict[str, Any] = {
        "entry_size": SPELL_BOOK_V546_ENTRY_SIZE,
        "expected_unpacked_size": expected_size,
        "unpacked_ok": unpacked_ok,
    }
    if unpack_error:
        summary["error"] = unpack_error
    if unpacked_ok:
        entries = []
        for index in range(min(count, 16)):
            entry_offset = index * SPELL_BOOK_V546_ENTRY_SIZE
            entries.append(parse_spell_book_v546_delta_entry(unpacked, entry_offset))
        summary["entries"] = entries
        if count > 16:
            summary["truncated_entries"] = count - 16
    add_field(fields, offset, packed_size, "eq2_packed", "spell_book_unpacked_delta", summary)
    return offset + packed_size


def read_fixed_c_string(blob: bytes, offset: int, size: int) -> str | None:
    if offset + size > len(blob):
        return None
    raw = blob[offset : offset + size]
    raw = raw.split(b"\x00", 1)[0]
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("latin-1", errors="replace")


def count_nonzero_records(blob: bytes, offset: int, count: int, size: int) -> int:
    total = 0
    for index in range(count):
        start = offset + index * size
        if start + size > len(blob):
            break
        if any(blob[start : start + size]):
            total += 1
    return total


def parse_character_sheet_spell_effect_v546(blob: bytes, offset: int) -> dict[str, Any]:
    return {
        "spell_id": read_u(blob, offset, 4),
        "cancellable": read_u(blob, offset + 4, 1),
        "total_time": read_float(blob, offset + 5),
        "expire_timestamp": read_u(blob, offset + 9, 4),
        "unknown2": read_u(blob, offset + 13, 1),
        "icon": read_u(blob, offset + 14, 2),
        "icon_type": read_u(blob, offset + 16, 2),
        "unknown3": read_u(blob, offset + 18, 1),
    }


def parse_character_sheet_maintained_effect_v546(blob: bytes, offset: int) -> dict[str, Any]:
    return {
        "name": read_fixed_c_string(blob, offset, 60),
        "target": read_u(blob, offset + 60, 4),
        "target_type": read_u(blob, offset + 64, 1),
        "spell_id": read_u(blob, offset + 65, 4),
        "slot_pos": read_u(blob, offset + 69, 4),
        "icon": read_u(blob, offset + 73, 2),
        "icon_type": read_u(blob, offset + 75, 2),
        "unknown3": read_u(blob, offset + 77, 1),
        "conc_used": read_u(blob, offset + 78, 1),
        "total_time": read_float(blob, offset + 79),
        "expire_timestamp": read_u(blob, offset + 83, 4),
    }


def parse_character_sheet_v546_summary(blob: bytes) -> dict[str, Any]:
    spell_effects_offset = 340
    maintained_effects_offset = 914
    spell_effect_size = 19
    maintained_effect_size = 87

    spell_effects = []
    for index in range(30):
        offset = spell_effects_offset + index * spell_effect_size
        chunk = blob[offset : offset + spell_effect_size]
        if any(chunk):
            spell_effects.append(parse_character_sheet_spell_effect_v546(blob, offset))
        if len(spell_effects) >= 8:
            break

    maintained_effects = []
    for index in range(30):
        offset = maintained_effects_offset + index * maintained_effect_size
        chunk = blob[offset : offset + maintained_effect_size]
        if any(chunk):
            maintained_effects.append(parse_character_sheet_maintained_effect_v546(blob, offset))
        if len(maintained_effects) >= 8:
            break

    name = read_fixed_c_string(blob, 0, 41)
    level = read_u(blob, 67, 2)
    hp_max = read_u(blob, 129, 4)
    looks_like_full_sheet = bool(name) and level is not None and 0 < level < 256 and hp_max is not None and hp_max < 10_000_000
    summary: dict[str, Any] = {
        "looks_like_full_sheet": looks_like_full_sheet,
        "identity": {
            "character_name": name,
            "last_name": read_fixed_c_string(blob, 101, 20),
            "race": read_u(blob, 41, 1),
            "gender": read_u(blob, 42, 1),
            "classes": {
                "class1": read_u(blob, 43, 4),
                "class2": read_u(blob, 47, 4),
                "class3": read_u(blob, 51, 4),
                "tradeskill_class1": read_u(blob, 55, 4),
                "tradeskill_class2": read_u(blob, 59, 4),
                "tradeskill_class3": read_u(blob, 63, 4),
            },
            "levels": {
                "level": level,
                "effective_level": read_u(blob, 69, 2),
                "tradeskill_level": read_u(blob, 71, 2),
                "gm_level": read_u(blob, 73, 4),
            },
        },
        "vital_stats": {
            "current_hp": read_u(blob, 125, 4),
            "max_hp": hp_max,
            "base_hp": read_u(blob, 133, 4),
            "current_power": read_u(blob, 137, 4),
            "max_power": read_u(blob, 141, 4),
            "base_power": read_u(blob, 145, 4),
            "conc_used": read_u(blob, 149, 1),
            "conc_max": read_u(blob, 150, 1),
        },
        "attributes": {
            "str": read_u(blob, 181, 2),
            "sta": read_u(blob, 183, 2),
            "agi": read_u(blob, 185, 2),
            "wis": read_u(blob, 187, 2),
            "int": read_u(blob, 189, 2),
            "str_base": read_u(blob, 191, 2),
            "sta_base": read_u(blob, 193, 2),
            "agi_base": read_u(blob, 195, 2),
            "wis_base": read_u(blob, 197, 2),
            "int_base": read_u(blob, 199, 2),
        },
        "coins": {
            "copper": read_u(blob, 296, 4),
            "silver": read_u(blob, 300, 4),
            "gold": read_u(blob, 304, 4),
            "plat": read_u(blob, 308, 4),
        },
        "effects": {
            "spell_effect_count": count_nonzero_records(blob, spell_effects_offset, 30, spell_effect_size),
            "spell_effects": spell_effects,
            "maintained_effect_count": count_nonzero_records(
                blob, maintained_effects_offset, 30, maintained_effect_size
            ),
            "maintained_effects": maintained_effects,
            "trauma": read_u(blob, 910, 1),
            "arcane": read_u(blob, 911, 1),
            "noxious": read_u(blob, 912, 1),
            "elemental": read_u(blob, 913, 1),
        },
        "state_flags": {
            "auto_attack": read_u(blob, 3532, 1),
            "ranged_auto_attack": read_u(blob, 3533, 1),
            "can_cast": read_u(blob, 3534, 1),
            "pre_zoning": read_u(blob, 3535, 1),
            "flags_anonymous": read_u(blob, 3540, 1),
            "flags_roleplaying": read_u(blob, 3541, 1),
            "flags_afk": read_u(blob, 3542, 1),
            "flags_lfg": read_u(blob, 3543, 1),
            "flags_lfw": read_u(blob, 3544, 1),
        },
        "pet_and_house": {
            "pet_id": read_u(blob, 4700, 4),
            "pet_name": read_fixed_c_string(blob, 4704, 32),
            "pet_health_pct": read_float(blob, 4745),
            "pet_power_pct": read_float(blob, 4749),
            "pet_movement": read_u(blob, 4754, 1),
            "pet_behavior": read_u(blob, 4755, 1),
            "status_points": read_u(blob, 4764, 4),
            "guild_status": read_u(blob, 4768, 4),
            "vault_slots": read_u(blob, 4774, 1),
            "house_zone": read_fixed_c_string(blob, 4775, 61),
            "bind_zone": read_fixed_c_string(blob, 4836, 61),
        },
    }
    return summary


def decode_character_sheet_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    packed_size = read_u(data, start, 4)
    if packed_size is None:
        return start
    add_field(fields, start, 4, "u32", "packed_size", packed_size)
    offset = start + 4

    if offset + packed_size > len(data):
        return offset
    packed_blob = data[offset : offset + packed_size]
    add_field(fields, offset, packed_size, "bytes", "character_sheet_packed_delta", f"{packed_size} bytes")

    unpacked, unpacked_ok, unpack_error = unpack_eq2_packed_stream(packed_blob, CHARACTER_SHEET_V546_SIZE)
    summary: dict[str, Any] = {
        "expected_unpacked_size": CHARACTER_SHEET_V546_SIZE,
        "unpacked_ok": unpacked_ok,
    }
    if unpack_error:
        summary["error"] = unpack_error
    if unpacked_ok:
        summary.update(parse_character_sheet_v546_summary(unpacked))
    add_field(fields, offset, packed_size, "eq2_packed", "character_sheet_unpacked_delta", summary)
    return offset + packed_size


def parse_raid_member_v546(blob: bytes, offset: int) -> dict[str, Any]:
    return {
        "zone_status": read_u(blob, offset, 1),
        "name": read_fixed_c_string(blob, offset + 1, 41),
        "spawn_id": read_u(blob, offset + 42, 4),
        "pet_id": read_u(blob, offset + 46, 4),
        "level_current": read_u(blob, offset + 50, 2),
        "level_max": read_u(blob, offset + 52, 2),
        "race_id": read_u(blob, offset + 54, 1),
        "class_id": read_u(blob, offset + 55, 1),
        "hp_current": read_i(blob, offset + 56, 4),
        "hp_max": read_i(blob, offset + 60, 4),
        "power_current": read_i(blob, offset + 64, 4),
        "power_max": read_i(blob, offset + 68, 4),
        "trauma_count": read_u(blob, offset + 72, 1),
        "arcane_count": read_u(blob, offset + 73, 1),
        "noxious_count": read_u(blob, offset + 74, 1),
        "elemental_count": read_u(blob, offset + 75, 1),
        "zone": read_fixed_c_string(blob, offset + 76, 60),
        "instance": read_u(blob, offset + 136, 1),
    }


def decode_raid_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    packed_size = read_u(data, start, 4)
    if packed_size is None:
        return start
    add_field(fields, start, 4, "u32", "packed_size", packed_size)
    offset = start + 4

    if offset + packed_size > len(data):
        return offset
    packed_blob = data[offset : offset + packed_size]
    add_field(fields, offset, packed_size, "bytes", "raid_update_packed_delta", f"{packed_size} bytes")

    unpacked, unpacked_ok, unpack_error = unpack_eq2_packed_stream(packed_blob, RAID_UPDATE_V546_SIZE)
    summary: dict[str, Any] = {
        "entry_size": RAID_MEMBER_V546_ENTRY_SIZE,
        "member_slots": 24,
        "expected_unpacked_size": RAID_UPDATE_V546_SIZE,
        "unpacked_ok": unpacked_ok,
    }
    if unpack_error:
        summary["error"] = unpack_error
    if unpacked_ok:
        members = []
        active_count = 0
        for index in range(24):
            member_offset = index * RAID_MEMBER_V546_ENTRY_SIZE
            chunk = unpacked[member_offset : member_offset + RAID_MEMBER_V546_ENTRY_SIZE]
            if any(chunk):
                active_count += 1
                if len(members) < 24:
                    group_index = index // 6
                    slot_index = index % 6
                    member = parse_raid_member_v546(unpacked, member_offset)
                    member["group"] = group_index
                    member["slot"] = slot_index
                    members.append(member)
        summary["active_member_slots"] = active_count
        summary["members"] = members
    add_field(fields, offset, packed_size, "eq2_packed", "raid_update_unpacked_delta", summary)
    return offset + packed_size


def parse_inventory_item_v546_delta_entry(blob: bytes, offset: int) -> dict[str, Any]:
    unknown6 = blob[offset + 91 : offset + 108] if offset + 108 <= len(blob) else b""
    return {
        "unique_id": read_u(blob, offset, 4),
        "bag_id": read_u(blob, offset + 4, 4),
        "inv_slot_id": read_u(blob, offset + 8, 4),
        "menu_type": read_u(blob, offset + 12, 4),
        "slot_id": read_u(blob, offset + 16, 1),
        "index": read_u(blob, offset + 17, 2),
        "icon": read_u(blob, offset + 19, 2),
        "count": read_u(blob, offset + 21, 1),
        "level": read_u(blob, offset + 22, 1),
        "tier": read_u(blob, offset + 23, 1),
        "num_slots": read_u(blob, offset + 24, 1),
        "item_id": read_i(blob, offset + 25, 4),
        "name": read_fixed_c_string(blob, offset + 29, 64),
        "unknown6": unknown6.hex(),
    }


def decode_inventory_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 2)
    if count is None:
        return start
    add_field(fields, start, 2, "u16", "item_count", count)
    offset = start + 2

    packed_size = read_u(data, offset, 4)
    if packed_size is None:
        return offset
    add_field(fields, offset, 4, "u32", "packed_size", packed_size)
    offset += 4

    if offset + packed_size > len(data):
        return offset
    packed_blob = data[offset : offset + packed_size]
    add_field(fields, offset, packed_size, "bytes", "inventory_packed_delta", f"{packed_size} bytes")

    expected_size = count * INVENTORY_ITEM_V546_ENTRY_SIZE
    unpacked, unpacked_ok, unpack_error = unpack_eq2_packed_stream(packed_blob, expected_size)
    summary: dict[str, Any] = {
        "entry_size": INVENTORY_ITEM_V546_ENTRY_SIZE,
        "expected_unpacked_size": expected_size,
        "unpacked_ok": unpacked_ok,
    }
    if unpack_error:
        summary["error"] = unpack_error
    if unpacked_ok:
        entries = []
        for index in range(min(count, 32)):
            entry_offset = index * INVENTORY_ITEM_V546_ENTRY_SIZE
            entries.append(parse_inventory_item_v546_delta_entry(unpacked, entry_offset))
        summary["entries"] = entries
        if count > 32:
            summary["truncated_entries"] = count - 32
    add_field(fields, offset, packed_size, "eq2_packed", "inventory_unpacked_delta", summary)
    offset += packed_size

    equip_flag = read_u(data, offset, 1)
    if equip_flag is None:
        return offset
    add_field(fields, offset, 1, "u8", "equip_flag", equip_flag)
    return offset + 1


def parse_recipe_book_v546_entry(blob: bytes, offset: int) -> dict[str, Any]:
    return {
        "recipe_id": read_u(blob, offset, 4),
        "recipe_data_crc": read_u(blob, offset + 4, 4),
        "unknown": read_u(blob, offset + 8, 4),
    }


def decode_recipe_book_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 2)
    if count is None:
        return start
    add_field(fields, start, 2, "u16", "recipe_count", count)
    offset = start + 2
    if count == 0:
        return offset

    packed_size = read_u(data, offset, 4)
    if packed_size is None:
        return offset
    add_field(fields, offset, 4, "u32", "packed_size", packed_size)
    offset += 4

    if offset + packed_size > len(data):
        return offset
    packed_blob = data[offset : offset + packed_size]
    add_field(fields, offset, packed_size, "bytes", "recipe_book_packed", f"{packed_size} bytes")

    expected_size = count * RECIPE_BOOK_V546_ENTRY_SIZE
    unpacked, unpacked_ok, unpack_error = unpack_eq2_packed_stream(packed_blob, expected_size)
    summary: dict[str, Any] = {
        "entry_size": RECIPE_BOOK_V546_ENTRY_SIZE,
        "expected_unpacked_size": expected_size,
        "unpacked_ok": unpacked_ok,
    }
    if unpack_error:
        summary["error"] = unpack_error
    if unpacked_ok:
        entries = []
        for index in range(min(count, 32)):
            entry_offset = index * RECIPE_BOOK_V546_ENTRY_SIZE
            entries.append(parse_recipe_book_v546_entry(unpacked, entry_offset))
        summary["entries"] = entries
        if count > 32:
            summary["truncated_entries"] = count - 32
    add_field(fields, offset, packed_size, "eq2_packed", "recipe_book_unpacked", summary)
    return offset + packed_size


def parse_skill_book_v546_delta_entry(blob: bytes, offset: int) -> dict[str, Any]:
    return {
        "skill_id": read_u(blob, offset, 4),
        "type": read_u(blob, offset + 4, 4),
        "current_val": read_u(blob, offset + 8, 2),
        "base_val": read_u(blob, offset + 10, 2),
        "max_val": read_u(blob, offset + 12, 2),
        "skill_delta": read_u(blob, offset + 14, 2),
        "skill_delta2": read_u(blob, offset + 16, 2),
        "display_minval": read_u(blob, offset + 18, 1),
        "display_maxval": read_u(blob, offset + 19, 1),
        "language_unknown": read_u(blob, offset + 20, 1),
    }


def decode_skill_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 2)
    if count is None:
        return start
    add_field(fields, start, 2, "u16", "skill_count", count)
    offset = start + 2

    packed_size = read_u(data, offset, 4)
    if packed_size is None:
        return offset
    add_field(fields, offset, 4, "u32", "packed_size", packed_size)
    offset += 4

    if offset + packed_size > len(data):
        return offset
    packed_blob = data[offset : offset + packed_size]
    add_field(fields, offset, packed_size, "bytes", "skill_book_packed_delta", f"{packed_size} bytes")

    expected_size = count * SKILL_BOOK_V546_ENTRY_SIZE
    unpacked, unpacked_ok, unpack_error = unpack_eq2_packed_stream(packed_blob, expected_size)
    summary: dict[str, Any] = {
        "entry_size": SKILL_BOOK_V546_ENTRY_SIZE,
        "expected_unpacked_size": expected_size,
        "unpacked_ok": unpacked_ok,
    }
    if unpack_error:
        summary["error"] = unpack_error
    if unpacked_ok:
        entries = []
        for index in range(min(count, 64)):
            entry_offset = index * SKILL_BOOK_V546_ENTRY_SIZE
            entries.append(parse_skill_book_v546_delta_entry(unpacked, entry_offset))
        summary["entries"] = entries
        if count > 64:
            summary["truncated_entries"] = count - 64
    add_field(fields, offset, packed_size, "eq2_packed", "skill_book_unpacked_delta", summary)
    return offset + packed_size


def decode_u32_u32_u16_raw20(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (2, "u16", "u16_0")),
    )
    if offset == start:
        return offset
    size = 0x14
    if offset + size > len(data):
        return offset
    add_field(fields, offset, size, "bytes", "tail_bytes", "20 bytes")
    return offset + size


def decode_large_zone_status_table(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        ((4, "u32", "header_u32_0"), (4, "u32", "header_u32_1")),
    )
    if offset == start:
        return offset
    pair_count = 0x19B
    for index in range(pair_count):
        for value_index in range(2):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"zone_status_pair[{index}].u32_{value_index}", value)
            offset += 4
    return decode_scalar_layout(
        data,
        offset,
        fields,
        tuple((4, "u32", f"trailing_u32_{index}") for index in range(5)),
    )


def decode_zone_servers_instances_status(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(data, start, fields, ((4, "u32", "u32_0"),))
    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    outer_count, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "outer_count", outer_count)
    offset += consumed
    for outer_index in range(min(outer_count, 1024)):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"outer[{outer_index}].name", text)
        offset += consumed
        for value_index in range(2):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"outer[{outer_index}].u32_{value_index}", value)
            offset += 4
        packed = read_packed_u16(data, offset)
        if packed is None:
            return offset
        inner_count, consumed = packed
        add_field(fields, offset, consumed, "packed_u16", f"outer[{outer_index}].inner_count", inner_count)
        offset += consumed
        for inner_index in range(min(inner_count, 1024)):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u16", f"outer[{outer_index}].inner[{inner_index}].name", text)
            offset += consumed
            for value_index in range(3):
                value = read_u(data, offset, 4)
                if value is None:
                    return offset
                add_field(
                    fields,
                    offset,
                    4,
                    "u32",
                    f"outer[{outer_index}].inner[{inner_index}].u32_{value_index}",
                    value,
                )
                offset += 4
        if inner_count > 1024 and offset < len(data):
            add_field(fields, offset, len(data) - offset, "bytes", f"outer[{outer_index}].inner_remaining_raw", f"{len(data) - offset} bytes")
            return len(data)
    if outer_count > 1024 and offset < len(data):
        add_field(fields, offset, len(data) - offset, "bytes", "outer_remaining_raw", f"{len(data) - offset} bytes")
        return len(data)
    return offset


def decode_examine_info_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    request_type = read_u(data, start, 1)
    if request_type is None:
        return start
    add_field(fields, start, 1, "u8", "request_type", request_type)
    offset = start + 1

    if request_type == 1:
        value = read_u(data, offset, 8)
        if value is None:
            return offset
        add_field(fields, offset, 8, "u64", "u64_0", value)
        offset += 8
    else:
        if request_type in {2, 3}:
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", "extra_u32", value)
            offset += 4
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "u32_0", value)
        offset += 4

    for index in range(2):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"flag_{index}", value)
        offset += 1
    return offset


def decode_u16_sized_raw(
    data: bytes, start: int, fields: list[dict[str, Any]], size_name: str, blob_name: str
) -> int:
    size = read_u(data, start, 2)
    if size is None:
        return start
    add_field(fields, start, 2, "u16", size_name, size)
    offset = start + 2
    if offset + size > len(data):
        return offset
    if size:
        add_field(fields, offset, size, "bytes", blob_name, f"{size} bytes")
    return offset + size


def decode_recipe_id_list(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "recipe_count", count)
    offset = start + 4
    for index in range(min(count, 256)):
        recipe_id = read_u(data, offset, 4)
        if recipe_id is None:
            return offset
        add_field(fields, offset, 4, "u32", f"recipe_id[{index}]", recipe_id)
        offset += 4
    if count > 256:
        remaining = (count - 256) * 4
        if offset + remaining <= len(data):
            add_field(fields, offset, remaining, "bytes", "recipe_ids_remaining", f"{count - 256} u32 ids")
            offset += remaining
    return offset


def decode_recipe_detail_entry(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = start
    for size, kind, name in ((4, "u32", "recipe_id"), (2, "u16", "icon")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size
    for name, size in (("recipe_name", 200), ("recipe_desc", 256)):
        next_offset = decode_fixed_c_string_field(data, offset, fields, f"{prefix}.{name}", size)
        if next_offset == offset:
            return offset
        offset = next_offset
    for name in ("book_volume", "unknownx", "technique", "knowledge", "level"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.{name}", value)
        offset += 4
    for name, size in (("recipe_book", 200), ("device", 40)):
        next_offset = decode_fixed_c_string_field(data, offset, fields, f"{prefix}.{name}", size)
        if next_offset == offset:
            return offset
        offset = next_offset
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.device_id", value)
    return offset + 4


def decode_recipe_details(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "recipe_count", count)
    offset = start + 4
    for index in range(count):
        next_offset = decode_recipe_detail_entry(data, offset, fields, f"recipe[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_dispatch_client_cmd(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    method = read_u(data, offset, 1)
    if method is None:
        return offset
    add_field(fields, offset, 1, "u8", "dispatch_method", method)
    offset += 1

    if method == 0:
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", "method0_string", text)
        offset += consumed
    elif method == 2:
        for name in ("method2_string_0", "method2_string_1"):
            decoded = read_len_string(data, offset, 1)
            if decoded is None:
                return offset
            text, _size, consumed = decoded
            add_field(fields, offset, consumed, "string_u8", name, text)
            offset += consumed
    elif method == 3:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "method3_u32", value)
        offset += 4
    elif method == 4:
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", "method4_string", text)
        offset += consumed
    elif method == 5:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "method5_u32", value)
        offset += 4

    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", "client_command", text)
    offset += consumed

    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    payload_size, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "payload_size", payload_size)
    offset += consumed
    if offset + payload_size <= len(data):
        add_field(fields, offset, payload_size, "bytes", "payload", f"{payload_size} bytes")
        offset += payload_size
    return offset


def decode_dispatch_es(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    method = read_u(data, offset, 1)
    if method is None:
        return offset
    add_field(fields, offset, 1, "u8", "dispatch_method", method)
    offset += 1

    if method in {1, 4}:
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", f"method{method}_string", text)
        offset += consumed
    elif method in {2, 3}:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"method{method}_u32", value)
        offset += 4

    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    payload_size, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "payload_size", payload_size)
    offset += consumed
    if offset + payload_size <= len(data):
        add_field(fields, offset, payload_size, "bytes", "payload", f"{payload_size} bytes")
        offset += payload_size
    return offset


def decode_three_floats(data: bytes, start: int, fields: list[dict[str, Any]], names: tuple[str, str, str]) -> int:
    offset = start
    for name in names:
        value = read_float(data, offset)
        if value is None:
            return offset
        add_field(fields, offset, 4, "float", name, value)
        offset += 4
    return offset


def decode_update_opportunity(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("name", "description"))
    for kind, name, size in (
        ("u32", "id", 4),
        ("u8", "wheel_type", 1),
        ("u8", "unknown", 1),
        ("u8", "order", 1),
        ("u16", "shift_icon", 2),
        ("u16", "starter_icon", 2),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    for name in ("time_total", "time_left"):
        value = read_float(data, offset)
        if value is None:
            return offset
        add_field(fields, offset, 4, "float", name, value)
        offset += 4

    for index in range(6):
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"icon[{index}]", value)
        offset += 2

    for index in range(6):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"countered[{index}]", value)
        offset += 1
    return offset


def decode_change_zone(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for name in ("account_id", "key"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", name, value)
        offset += 4

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", "ip_address", text)
    offset += consumed

    port = read_u(data, offset, 2)
    if port is not None:
        add_field(fields, offset, 2, "u16", "port", port)
        offset += 2
    return offset


def decode_migrate_client_to_zone_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(data, start, fields, ((4, "u32", "u32_0"), (4, "u32", "u32_1")))
    if offset == start:
        return offset
    offset = decode_string16_fields(data, offset, fields, ("text_0", "text_1", "text_2", "text_3", "text_4"))
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        ((4, "u32", "u32_2"), (4, "u32", "u32_3"), (4, "u32", "u32_4"), (4, "u32", "u32_5")),
    )
    offset = decode_string16_fields(data, offset, fields, ("text_5",))

    def decode_tail(tail_start: int, tail_fields: list[dict[str, Any]], has_optional: bool) -> int:
        tail_offset = tail_start
        if has_optional:
            tail_offset = decode_scalar_layout(
                data,
                tail_offset,
                tail_fields,
                (
                    (4, "u32", "optional_u32_0"),
                    (4, "u32", "optional_u32_1"),
                    (4, "u32", "optional_u32_2"),
                    (4, "u32", "optional_u32_3"),
                ),
            )
            tail_offset = decode_string16_fields(data, tail_offset, tail_fields, ("optional_text_0", "optional_text_1"))
            tail_offset = decode_scalar_layout(data, tail_offset, tail_fields, ((4, "u32", "optional_u32_4"),))
        tail_offset = decode_scalar_layout(data, tail_offset, tail_fields, ((4, "u32", "u32_6"),))
        tail_offset = decode_string16_fields(data, tail_offset, tail_fields, ("text_6",))
        value = read_u(data, tail_offset, 1)
        if value is None:
            return tail_offset
        add_field(tail_fields, tail_offset, 1, "u8", "flag", value)
        tail_offset += 1
        count = read_u(data, tail_offset, 4)
        if count is None:
            return tail_offset
        add_field(tail_fields, tail_offset, 4, "u32", "id_count", count)
        tail_offset += 4
        for index in range(min(count, 1024)):
            value = read_u(data, tail_offset, 4)
            if value is None:
                return tail_offset
            add_field(tail_fields, tail_offset, 4, "u32", f"id[{index}]", value)
            tail_offset += 4
        if count > 1024:
            remaining = (count - 1024) * 4
            if tail_offset + remaining <= len(data):
                add_field(tail_fields, tail_offset, remaining, "bytes", "id_remaining", f"{count - 1024} u32 values")
                tail_offset += remaining
        return decode_scalar_layout(
            data,
            tail_offset,
            tail_fields,
            ((4, "u32", "trailing_u32_0"), (4, "u32", "trailing_u32_1")),
        )

    no_optional_fields: list[dict[str, Any]] = []
    no_optional_offset = decode_tail(offset, no_optional_fields, has_optional=False)
    if no_optional_offset == len(data):
        fields.extend(no_optional_fields)
        return no_optional_offset

    optional_fields: list[dict[str, Any]] = []
    optional_offset = decode_tail(offset, optional_fields, has_optional=True)
    if optional_offset > no_optional_offset:
        fields.extend(optional_fields)
        return optional_offset
    fields.extend(no_optional_fields)
    return no_optional_offset


def decode_migrate_client_to_zone_reply(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        ((1, "u8", "response"), (4, "u32", "u32_0"), (4, "u32", "u32_1")),
    )
    if offset == start:
        return offset
    offset = decode_string16_fields(data, offset, fields, ("text16_0",))
    value = read_u(data, offset, 2)
    if value is None:
        return offset
    add_field(fields, offset, 2, "u16", "u16_0", value)
    offset += 2

    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", "text8_0", text)
    offset += consumed

    return decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (4, "u32", "u32_2"),
            (4, "u32", "u32_3"),
            (4, "u32", "u32_4"),
            (4, "u32", "u32_5"),
            (1, "u8", "flag_0"),
            (1, "u8", "flag_1"),
        ),
    )


def decode_ten_u32_six_u8(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        tuple((4, "u32", f"u32_{index}") for index in range(10)),
    )
    return decode_scalar_layout(
        data,
        offset,
        fields,
        tuple((1, "u8", f"flag_{index}") for index in range(6)),
    )


def decode_u32_six_u8(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(data, start, fields, ((4, "u32", "u32_0"),))
    return decode_scalar_layout(
        data,
        offset,
        fields,
        tuple((1, "u8", f"flag_{index}") for index in range(6)),
    )


def decode_group_member_entry(
    data: bytes, offset: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.id", value)
    offset += 4

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.name", text)
    offset += consumed

    for index in range(4):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    return offset


def decode_group_member_added(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for name in ("group_id", "unknown_u32"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", name, value)
        offset += 4

    for index in range(6):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"group_flag[{index}]", value)
        offset += 1

    member_count = read_u(data, offset, 1)
    if member_count is None:
        return offset
    add_field(fields, offset, 1, "u8", "member_count", member_count)
    offset += 1
    for index in range(member_count):
        offset = decode_group_member_entry(data, offset, fields, f"member[{index}]")

    subgroup_count = read_u(data, offset, 1)
    if subgroup_count is None:
        return offset
    add_field(fields, offset, 1, "u8", "subgroup_count", subgroup_count)
    offset += 1
    for group_index in range(subgroup_count):
        for field_index in range(2):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"subgroup[{group_index}].u32_{field_index}", value)
            offset += 4
        for flag_index in range(6):
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"subgroup[{group_index}].flag[{flag_index}]", value)
            offset += 1
        nested_count = read_u(data, offset, 1)
        if nested_count is None:
            return offset
        add_field(fields, offset, 1, "u8", f"subgroup[{group_index}].member_count", nested_count)
        offset += 1
        for member_index in range(nested_count):
            offset = decode_group_member_entry(
                data, offset, fields, f"subgroup[{group_index}].member[{member_index}]"
            )
    return offset


def decode_group_removed(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for name in ("group_id", "member_id"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", name, value)
        offset += 4

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", "name", text)
    offset += consumed

    for index in range(4):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return offset


def decode_clear_data(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for name in ("unknown_u32_0", "unknown_u32_1"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", name, value)
        offset += 4

    for index in range(4):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"status_u32[{index}]", value)
        offset += 4
    for index in range(4):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"status_u8[{index}]", value)
        offset += 1

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", "status_text", text)
    return offset + consumed


def decode_es_zone_instance_status(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    status = read_u(data, offset, 1)
    if status is None:
        return offset
    add_field(fields, offset, 1, "u8", "status", status)
    offset += 1

    offset = decode_string16_fields(data, offset, fields, ("zone_name", "instance_name"))
    value = read_u(data, offset, 2)
    if value is not None:
        add_field(fields, offset, 2, "u16", "unknown_u16", value)
        offset += 2
    return offset


def decode_zones_status(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    packed = read_packed_u16(data, start)
    if packed is None:
        return start
    count, consumed = packed
    add_field(fields, start, consumed, "packed_u16", "zone_count", count)
    offset = start + consumed
    for index in range(count):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"zone[{index}].name", text)
        offset += consumed
        for field_index in range(4):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"zone[{index}].u32_{field_index}", value)
            offset += 4
    return offset


def decode_u32_string16_list(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    first = read_u(data, start, 4)
    if first is None:
        return start
    add_field(fields, start, 4, "u32", f"{prefix}_u32", first)
    offset = start + 4

    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    count, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", f"{prefix}_count", count)
    offset += consumed
    for index in range(count):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"{prefix}[{index}]", text)
        offset += consumed
    return offset


def decode_counted_u32_list_with_u8(
    data: bytes, start: int, fields: list[dict[str, Any]], count_name: str
) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", count_name, count)
    offset = start + 4
    for index in range(min(count, 1024)):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"selection[{index}]", value)
        offset += 4
    if count > 1024:
        remaining = (count - 1024) * 4
        if offset + remaining <= len(data):
            add_field(fields, offset, remaining, "bytes", "selection_remaining", f"{count - 1024} u32 values")
            offset += remaining
    trailing = read_u(data, offset, 1)
    if trailing is not None:
        add_field(fields, offset, 1, "u8", "trailing_flag", trailing)
        offset += 1
    return offset


def decode_create_guild_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for kind, name, size in (("u16", "unknown_u16", 2), ("u32", "u32_0", 4), ("u32", "u32_1", 4)):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", "name", text)
    offset += consumed
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index + 2}", value)
        offset += 4
    return offset


def decode_guildsay(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for name in ("u32_0", "u32_1"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", name, value)
        offset += 4
    decoded_offset = decode_string16_fields(data, offset, fields, ("speaker",))
    if decoded_offset == offset:
        return offset
    offset = decoded_offset
    for name in ("flag_0", "flag_1"):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", name, value)
        offset += 1
    return decode_string16_fields(data, offset, fields, ("message",))


def decode_guild_or_fellowship_status(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text_0", "text_1"))
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    offset += 4
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag", value)
    offset += 1
    value = read_u(data, offset, 2)
    if value is not None:
        add_field(fields, offset, 2, "u16", "u16_0", value)
        offset += 2
    return offset


def decode_guild_update_payload(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = decode_string16_fields(data, start, fields, (f"{prefix}.guild_name", f"{prefix}.guild_motd"))
    if offset == start:
        return offset

    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.guild_id", value)
    offset += 4

    value = read_u(data, offset, 2)
    if value is None:
        return offset
    add_field(
        fields,
        offset,
        2,
        "u16",
        f"{prefix}.status_flags",
        {"low_byte": value & 0xFF, "high_byte": value >> 8, "raw": value},
    )
    offset += 2

    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (4, "u32", f"{prefix}.guild_level"),
            (4, "u32", f"{prefix}.formed_date"),
            (4, "u32", f"{prefix}.unique_accounts_or_member_count"),
            (8, "u64", f"{prefix}.exp_current"),
            (8, "u64", f"{prefix}.exp_to_next_level"),
            (4, "u32", f"{prefix}.event_filter_retain1"),
            (4, "u32", f"{prefix}.event_filter_retain2"),
            (4, "u32", f"{prefix}.event_filter_broadcast1"),
            (4, "u32", f"{prefix}.event_filter_broadcast2"),
            (4, "u32", f"{prefix}.event_filter_retain3_or_flags0"),
            (4, "u32", f"{prefix}.event_filter_broadcast3_or_flags1"),
        ),
    )
    for rank_index in range(8):
        offset = decode_string16_fields(data, offset, fields, (f"{prefix}.rank[{rank_index}].name",))
        value = read_u(data, offset, 8)
        if value is None:
            return offset
        add_field(fields, offset, 8, "u64", f"{prefix}.rank[{rank_index}].rank_flags", value)
        offset += 8
        for permission_index in range(4):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"{prefix}.rank[{rank_index}].permission[{permission_index}].mask", value)
            offset += 4
            value = read_u(data, offset, 8)
            if value is None:
                return offset
            add_field(fields, offset, 8, "u64", f"{prefix}.rank[{rank_index}].permission[{permission_index}].value", value)
            offset += 8
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        ((8, "u64", f"{prefix}.tail_u64"), (4, "u32", f"{prefix}.tail_u32")),
    )
    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.entry_count", count)
    offset += 4
    for index in range(min(count, 1024)):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.entry[{index}].id", value)
        offset += 4
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"{prefix}.entry[{index}].text", text)
        offset += consumed
    if count > 1024 and offset < len(data):
        add_field(fields, offset, len(data) - offset, "bytes", f"{prefix}.entries_remaining_raw", f"{len(data) - offset} bytes")
        offset = len(data)
    return offset


def decode_create_guild_reply(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(data, start, fields, ((4, "u32", "response"),))
    return decode_guild_update_payload(data, offset, fields, "guild")


def decode_consignment_store_payload(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = decode_scalar_layout(data, start, fields, ((4, "u32", f"{prefix}.u32_0"),))
    offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_0",))
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (8, "u64", f"{prefix}.u64_0"),
            (4, "u32", f"{prefix}.u32_1"),
            (4, "u32", f"{prefix}.u32_2"),
        ),
    )
    offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_1", f"{prefix}.text_2"))
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (4, "u32", f"{prefix}.u32_3"),
            (4, "u32", f"{prefix}.u32_4"),
            (4, "u32", f"{prefix}.u32_5"),
            (8, "u64", f"{prefix}.u64_1"),
        ),
    )
    blob_size = read_u(data, offset, 4)
    if blob_size is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.blob_size", blob_size)
    offset += 4
    if offset + blob_size > len(data):
        return offset
    if blob_size:
        add_field(fields, offset, blob_size, "bytes", f"{prefix}.blob", f"{blob_size} bytes")
    offset += blob_size

    offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_3", f"{prefix}.text_4"))
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (8, "u64", f"{prefix}.u64_2"),
            (4, "u32", f"{prefix}.u32_6"),
            (2, "u16", f"{prefix}.u16_0"),
            (4, "u32", f"{prefix}.u32_7"),
            (4, "u32", f"{prefix}.u32_8"),
            (4, "u32", f"{prefix}.u32_9"),
        ),
    )
    offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_5",))
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        ((4, "u32", f"{prefix}.u32_10"), (4, "u32", f"{prefix}.u32_11")),
    )
    offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_6",))
    return decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (2, "u16", f"{prefix}.u16_1"),
            (2, "u16", f"{prefix}.u16_2"),
            (2, "u16", f"{prefix}.u16_3"),
            (4, "u32", f"{prefix}.u32_12"),
            (2, "u16", f"{prefix}.u16_4"),
        ),
    )


def decode_consignment_close_store(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(data, start, fields, ((4, "u32", "u32_0"),))
    return decode_consignment_store_payload(data, offset, fields, "store")


def decode_purchase_consignment_response(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        (
            (1, "u8", "response"),
            (8, "u64", "u64_0"),
            (4, "u32", "u32_0"),
            (8, "u64", "u64_1"),
            (4, "u32", "u32_1"),
            (4, "u32", "u32_2"),
            (4, "u32", "u32_3"),
            (4, "u32", "u32_4"),
            (1, "u8", "flag"),
        ),
    )
    if offset == start:
        return offset
    offset = decode_string16_fields(data, offset, fields, ("text_0", "text_1"))
    blob_size = read_u(data, offset, 4)
    if blob_size is None:
        return offset
    add_field(fields, offset, 4, "u32", "blob_size", blob_size)
    offset += 4
    if offset + blob_size > len(data):
        return offset
    if blob_size:
        add_field(fields, offset, blob_size, "bytes", "response_blob", f"{blob_size} bytes")
    offset += blob_size
    value = read_u(data, offset, 8)
    if value is not None:
        add_field(fields, offset, 8, "u64", "u64_2", value)
        offset += 8
    return offset


def decode_string16_u32(data: bytes, start: int, fields: list[dict[str, Any]], string_name: str) -> int:
    offset = decode_string16_fields(data, start, fields, (string_name,))
    value = read_u(data, offset, 4)
    if value is not None:
        add_field(fields, offset, 4, "u32", "u32_0", value)
        offset += 4
    return offset


def decode_house_base_screen(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_u32(data, start, fields, "house_name")
    flag = read_u(data, offset, 1)
    if flag is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag", flag)
    offset += 1
    size = read_u(data, offset, 2)
    if size is None:
        return offset
    add_field(fields, offset, 2, "u16", "blob_size", size)
    offset += 2
    if offset + size <= len(data):
        add_field(fields, offset, size, "bytes", "blob", f"{size} bytes")
        offset += size
    return offset


def decode_house_purchase_screen(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_u32(data, start, fields, "house_name")
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_1", value)
    offset += 4
    flag = read_u(data, offset, 1)
    if flag is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag", flag)
    offset += 1
    if flag == 0:
        decoded_offset = decode_string16_fields(data, offset, fields, ("extra_text",))
        if decoded_offset == offset:
            return offset
        offset = decoded_offset
        extra = read_u(data, offset, 1)
        if extra is not None:
            add_field(fields, offset, 1, "u8", "extra_flag", extra)
            offset += 1
    return offset


def decode_house_display_status(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("house_name",))
    for kind, name, size in (
        ("u32", "u32_0", 4),
        ("u64", "u64_0", 8),
        ("u32", "u32_1", 4),
        ("u64", "u64_1", 8),
        ("u32", "u32_2", 4),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    offset = decode_string16_fields(data, offset, fields, ("status_text",))
    flag = read_u(data, offset, 1)
    if flag is not None:
        add_field(fields, offset, 1, "u8", "flag", flag)
        offset += 1
    return offset


def decode_house_close_ui(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    offset = start + 4
    flag = read_u(data, offset, 1)
    if flag is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag", flag)
    offset += 1
    if flag == 0:
        decoded_offset = decode_string16_fields(data, offset, fields, ("text",))
        if decoded_offset == offset:
            return offset
        offset = decoded_offset
        extra = read_u(data, offset, 1)
        if extra is not None:
            add_field(fields, offset, 1, "u8", "extra_flag", extra)
            offset += 1
    return offset


def decode_player_house_access_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(data, start, fields, ((4, "u32", "u32_0"),))
    if offset == start:
        return offset
    offset = decode_string16_fields(data, offset, fields, ("house_name",))
    offset = decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (8, "u64", "u64_0"),
            (4, "u32", "u32_1"),
            (8, "u64", "u64_1"),
            (4, "u32", "u32_2"),
            (4, "u32", "u32_3"),
            (1, "u8", "flag_0"),
            (1, "u8", "flag_1"),
            (1, "u8", "flag_2"),
            (1, "u8", "has_optional_entries"),
        ),
    )
    has_optional_entries = read_u(data, offset - 1, 1)
    if has_optional_entries:
        count = read_u(data, offset, 1)
        if count is None:
            return offset
        add_field(fields, offset, 1, "u8", "optional_entry_count", count)
        offset += 1
        for index in range(count):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"optional[{index}].u32", value)
            offset += 4
            offset = decode_string16_fields(data, offset, fields, (f"optional[{index}].text",))
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"optional[{index}].flag", value)
            offset += 1
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", "optional_trailing_flag", value)
        offset += 1

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "access_entry_count", count)
    offset += 1
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"access[{index}].u32_0", value)
        offset += 4
        offset = decode_string16_fields(data, offset, fields, (f"access[{index}].text",))
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (8, "u64", f"access[{index}].u64_0"),
                (4, "u32", f"access[{index}].u32_1"),
                (8, "u64", f"access[{index}].u64_1"),
                (4, "u32", f"access[{index}].u32_2"),
                (4, "u32", f"access[{index}].u32_3"),
            ),
        )

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "visitor_entry_count", count)
    offset += 1
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"visitor[{index}].u32_0", value)
        offset += 4
        offset = decode_string16_fields(data, offset, fields, (f"visitor[{index}].text",))
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (8, "u64", f"visitor[{index}].u64_0"),
                (4, "u32", f"visitor[{index}].u32_1"),
                (4, "u32", f"visitor[{index}].u32_2"),
            ),
        )

    return decode_scalar_layout(
        data,
        offset,
        fields,
        (
            (1, "u8", "trailing_flag"),
            (2, "u16", "u16_0"),
            (2, "u16", "u16_1"),
            (2, "u16", "u16_2"),
        ),
    )


def decode_position_moveable_object(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "spawn_id", value)
    offset += 4
    for name in ("x", "y", "z"):
        value = read_float(data, offset)
        if value is None:
            return offset
        add_field(fields, offset, 4, "float", name, value)
        offset += 4
    for name in ("angle_0", "angle_1"):
        value = read_float(data, offset)
        if value is None:
            return offset
        add_field(fields, offset, 4, "float", name, value)
        offset += 4
    value = read_u(data, offset, 4)
    if value is not None:
        add_field(fields, offset, 4, "u32", "unknown3", value)
        offset += 4
    return offset


def decode_enter_move_object_mode(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_scalar_layout(
        data,
        start,
        fields,
        (
            (4, "u32", "spawn_id"),
            (1, "u8", "placement_mode"),
            (2, "u16", "model_type"),
            (4, "float", "unknown2"),
            (4, "float", "max_distance"),
        ),
    )
    if offset == start:
        return offset
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "id_count", count)
    offset += 1
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"id[{index}]", value)
        offset += 4
    return offset


def decode_u16_sized_raw_u16(data: bytes, start: int, fields: list[dict[str, Any]], blob_name: str) -> int:
    size = read_u(data, start, 2)
    if size is None:
        return start
    add_field(fields, start, 2, "u16", "byte_count", size)
    offset = start + 2
    if offset + size > len(data):
        return offset
    if size:
        add_field(fields, offset, size, "bytes", blob_name, f"{size} bytes")
    offset += size
    value = read_u(data, offset, 2)
    if value is not None:
        add_field(fields, offset, 2, "u16", "u16_0", value)
        offset += 2
    return offset


def decode_counted_u32_string16_entries(
    data: bytes,
    start: int,
    fields: list[dict[str, Any]],
    *,
    count_size: int,
    count_kind: str,
    prefix: str,
) -> int:
    count = read_u(data, start, count_size)
    if count is None:
        return start
    add_field(fields, start, count_size, count_kind, "entry_count", count)
    offset = start + count_size
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}[{index}].u32", value)
        offset += 4
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"{prefix}[{index}].text", text)
        offset += consumed
    return offset


def decode_packed_count_u32_string16_entries(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    count_info = read_packed_u16(data, start)
    if count_info is None:
        return start
    count, consumed = count_info
    add_field(fields, start, consumed, "packed_u16", "entry_count", count)
    offset = start + consumed
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}[{index}].u32", value)
        offset += 4
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, string_size = decoded
        add_field(fields, offset, string_size, "string_u16", f"{prefix}[{index}].text", text)
        offset += string_size
    return offset


def decode_customization_purchase_request(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = decode_packed_count_u32_string16_entries(data, start, fields, "option")
    if offset == start:
        return offset
    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", "trailing_flag", value)
        offset += 1
    return offset


def decode_customization_set_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("customization_name",))
    if offset == start:
        return offset
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    offset += 4

    count_info = read_packed_u16(data, offset)
    if count_info is None:
        return offset
    count, consumed = count_info
    add_field(fields, offset, consumed, "packed_u16", "entry_count", count)
    offset += consumed

    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"entry[{index}].u32_0", value)
        offset += 4

        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"entry[{index}].text", text)
        offset += consumed

        for size, kind, name in (
            (8, "u64", "u64_0"),
            (4, "u32", "u32_1"),
            (8, "u64", "u64_1"),
            (4, "u32", "u32_2"),
            (1, "u8", "flag_0"),
            (1, "u8", "flag_1"),
        ):
            value = read_u(data, offset, size)
            if value is None:
                return offset
            add_field(fields, offset, size, kind, f"entry[{index}].{name}", value)
            offset += size
    return offset


def decode_tint_widgets(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 1)
    if count is None:
        return start
    add_field(fields, start, 1, "u8", "entry_count", count)
    offset = start + 1
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"entry[{index}].object_id", value)
        offset += 4
        for name in ("red", "green", "blue"):
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"entry[{index}].{name}", value)
            offset += 1
    return offset


def decode_examine_consignment_response(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for size, kind, name in (
        (4, "u32", "u32_0"),
        (8, "u64", "u64_0"),
        (1, "u8", "u8_0"),
        (1, "u8", "u8_1"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    blob_size = read_u(data, offset, 4)
    if blob_size is None:
        return offset
    add_field(fields, offset, 4, "u32", "blob_size", blob_size)
    offset += 4
    if offset + blob_size > len(data):
        return offset
    add_field(fields, offset, blob_size, "bytes", "response_blob", f"{blob_size} bytes")
    return offset + blob_size


def decode_dispatch_spell_cmd(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    method = read_u(data, start, 1)
    if method is None:
        return start
    add_field(fields, start, 1, "u8", "dispatch_method", method)
    offset = start + 1
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4

    if method == 0:
        blob_size = read_u(data, offset, 2)
        if blob_size is None:
            return offset
        add_field(fields, offset, 2, "u16", "payload_size", blob_size)
        offset += 2
        if offset + blob_size > len(data):
            return offset
        add_field(fields, offset, blob_size, "bytes", "payload", f"{blob_size} bytes")
        offset += blob_size
    return offset


def decode_entity_verb_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.command", text)
    offset += consumed

    value = read_float(data, offset)
    if value is None:
        return offset
    add_field(fields, offset, 4, "float", f"{prefix}.distance_or_value", value)
    offset += 4

    display_error = read_u(data, offset, 1)
    if display_error is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.display_error", display_error)
    offset += 1

    flag = read_u(data, offset, 1)
    if flag is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag", flag)
    offset += 1

    if display_error != 0:
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"{prefix}.error", text)
        offset += consumed

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.display_text", text)
    return offset + consumed


def decode_entity_verbs_reply(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    spawn_id = read_u(data, start, 4)
    if spawn_id is None:
        return start
    add_field(fields, start, 4, "u32", "spawn_id", spawn_id)
    offset = start + 4

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "verb_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_entity_verb_entry(data, offset, fields, f"verb[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_u32_string16(data: bytes, start: int, fields: list[dict[str, Any]], string_name: str) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    return decode_string16_fields(data, start + 4, fields, (string_name,))


def decode_chat_relationship_update(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for size, kind, name in ((4, "u32", "account_id"), (1, "u8", "relationship_type")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "name_count", count)
    offset += 4
    for index in range(count):
        for name in ("name", "location"):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u16", f"entry[{index}].{name}", text)
            offset += consumed
    return offset


def decode_conditional_u32_list(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    first = read_u(data, offset, 4)
    if first is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", first)
    offset += 4

    flag = read_u(data, offset, 1)
    if flag is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag", flag)
    offset += 1

    if flag == 0:
        count = read_u(data, offset, 1)
        if count is None:
            return offset
        add_field(fields, offset, 1, "u8", "entry_count", count)
        offset += 1
        for index in range(count):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"entry[{index}]", value)
            offset += 4

    trailing = read_u(data, offset, 4)
    if trailing is not None:
        add_field(fields, offset, 4, "u32", "u32_1", trailing)
        offset += 4
    return offset


def decode_loot_items_request(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    loot_id = read_u(data, offset, 4)
    if loot_id is None:
        return offset
    add_field(fields, offset, 4, "u32", "loot_id", loot_id)
    offset += 4

    loot_all = read_u(data, offset, 1)
    if loot_all is None:
        return offset
    add_field(fields, offset, 1, "u8", "loot_all", loot_all)
    offset += 1

    if loot_all == 0:
        item_count = read_u(data, offset, 1)
        if item_count is None:
            return offset
        add_field(fields, offset, 1, "u8", "item_count", item_count)
        offset += 1
        for index in range(item_count):
            item_id = read_u(data, offset, 4)
            if item_id is None:
                return offset
            add_field(fields, offset, 4, "u32", f"item_id[{index}]", item_id)
            offset += 4

    target_id = read_u(data, offset, 4)
    if target_id is not None:
        add_field(fields, offset, 4, "u32", "target_id", target_id)
        offset += 4
    return offset


def decode_display_inn_visit_screen(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "house_count", count)
    offset = start + 4
    for index in range(count):
        house_id = read_u(data, offset, 4)
        if house_id is None:
            return offset
        add_field(fields, offset, 4, "u32", f"house[{index}].house_id", house_id)
        offset += 4

        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"house[{index}].owner", text)
        offset += consumed

        for size, kind, name in ((4, "u32", "u32_0"), (1, "u8", "access_or_flag")):
            value = read_u(data, offset, size)
            if value is None:
                return offset
            add_field(fields, offset, size, kind, f"house[{index}].{name}", value)
            offset += size
    return offset


def decode_dump_scheduler(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "entry_count", count)
    offset = start + 4
    for index in range(count):
        for scalar_index in range(2):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"entry[{index}].u32_{scalar_index}", value)
            offset += 4
        for string_index in range(2):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u16", f"entry[{index}].text_{string_index}", text)
            offset += consumed
        for scalar_index in range(2, 4):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"entry[{index}].u32_{scalar_index}", value)
            offset += 4

    trailing = read_u(data, offset, 1)
    if trailing is not None:
        add_field(fields, offset, 1, "u8", "trailing_flag", trailing)
        offset += 1
    return offset


def decode_five_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(5):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return offset


def decode_perform_player_knockback(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for name in ("target_x", "target_y", "target_z", "vertical_movement", "horizontal_movement"):
        value = read_float(data, offset)
        raw = read_u(data, offset, 4)
        if value is None or raw is None:
            return offset
        add_field(fields, offset, 4, "float/u32", name, {"float": value, "u32": raw})
        offset += 4
    return offset


def decode_populate_skill_maps(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "entry_count", count)
    offset = start + 4
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"entry[{index}].skill_id", value)
        offset += 4
        for name in ("short_name", "name"):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u16", f"entry[{index}].{name}", text)
            offset += consumed
    return offset


def decode_u8_count_u32_u8_pairs(
    data: bytes, start: int, fields: list[dict[str, Any]], count_name: str, entry_prefix: str
) -> int:
    count = read_u(data, start, 1)
    if count is None:
        return start
    add_field(fields, start, 1, "u8", count_name, count)
    offset = start + 1
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{entry_prefix}[{index}].u32", value)
        offset += 4

        flag = read_u(data, offset, 1)
        if flag is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{entry_prefix}[{index}].flag", flag)
        offset += 1
    return offset


def decode_begin_item_creation(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4

    outer_count = read_u(data, offset, 1)
    if outer_count is None:
        return offset
    add_field(fields, offset, 1, "u8", "outer_count", outer_count)
    offset += 1

    for outer_index in range(outer_count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"outer[{outer_index}].u32", value)
        offset += 4
        next_offset = decode_u8_count_u32_u8_pairs(
            data,
            offset,
            fields,
            f"outer[{outer_index}].pair_count",
            f"outer[{outer_index}].pair",
        )
        if next_offset == offset:
            return offset
        offset = next_offset

    return decode_u8_count_u32_u8_pairs(data, offset, fields, "trailing_pair_count", "trailing_pair")


def decode_crafting_ui_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string16_fields(data, start, fields, (f"{prefix}.text",))
    if offset == start:
        return start
    for size, kind, name in (
        (4, "u32", f"{prefix}.u32"),
        (2, "u16", f"{prefix}.u16"),
        (1, "u8", f"{prefix}.u8"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_u8_count_crafting_ui_entries(
    data: bytes, start: int, fields: list[dict[str, Any]], count_name: str, prefix: str
) -> int:
    count = read_u(data, start, 1)
    if count is None:
        return start
    add_field(fields, start, 1, "u8", count_name, count)
    offset = start + 1
    for index in range(count):
        next_offset = decode_crafting_ui_entry(data, offset, fields, f"{prefix}[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_show_create_from_recipe_ui(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text_0",))
    if offset == start:
        return start

    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    offset += 4

    next_offset = decode_string16_fields(data, offset, fields, ("text_1",))
    if next_offset == offset:
        return offset
    offset = next_offset
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "u8_0", value)
    offset += 1

    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_1", value)
    offset += 4

    for index in range(2, 4):
        next_offset = decode_string16_fields(data, offset, fields, (f"text_{index}",))
        if next_offset == offset:
            return offset
        offset = next_offset
        for size, kind, name in (
            (2, "u16", f"u16_{index - 2}"),
            (1, "u8", f"u8_{index - 1}"),
        ):
            value = read_u(data, offset, size)
            if value is None:
                return offset
            add_field(fields, offset, size, kind, name, value)
            offset += size

    next_offset = decode_string16_fields(data, offset, fields, ("text_4",))
    if next_offset == offset:
        return offset
    offset = next_offset

    next_offset = decode_u8_count_crafting_ui_entries(data, offset, fields, "entry_count", "entry")
    if next_offset == offset:
        return offset
    offset = next_offset
    for index in range(2, 4):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4

    category_count = read_u(data, offset, 1)
    if category_count is None:
        return offset
    add_field(fields, offset, 1, "u8", "category_count", category_count)
    offset += 1
    for category_index in range(category_count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"category[{category_index}].u32_0", value)
        offset += 4

        next_offset = decode_string16_fields(data, offset, fields, (f"category[{category_index}].text",))
        if next_offset == offset:
            return offset
        offset = next_offset

        for size, kind, name in (
            (1, "u8", f"category[{category_index}].u8_0"),
            (4, "u32", f"category[{category_index}].u32_1"),
        ):
            value = read_u(data, offset, size)
            if value is None:
                return offset
            add_field(fields, offset, size, kind, name, value)
            offset += size

        next_offset = decode_u8_count_crafting_ui_entries(
            data,
            offset,
            fields,
            f"category[{category_index}].entry_count",
            f"category[{category_index}].entry",
        )
        if next_offset == offset:
            return offset
        offset = next_offset

        next_offset = decode_u8_count_u32_u8_pairs(
            data,
            offset,
            fields,
            f"category[{category_index}].pair_count",
            f"category[{category_index}].pair",
        )
        if next_offset == offset:
            return offset
        offset = next_offset

    next_offset = decode_string16_fields(data, offset, fields, ("text_5",))
    if next_offset == offset:
        return offset
    offset = next_offset

    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "u8_3", value)
    offset += 1

    next_offset = decode_u8_count_crafting_ui_entries(data, offset, fields, "tail_entry_count", "tail_entry")
    if next_offset == offset:
        return offset
    offset = next_offset
    return decode_u8_count_u32_u8_pairs(data, offset, fields, "tail_pair_count", "tail_pair")


def decode_eqcmd_tail_strings(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = decode_string8_field(data, start, fields, f"{prefix}.tail_text8")
    if offset == start:
        return start
    return decode_string16_fields(data, offset, fields, (f"{prefix}.tail_text16",))


def decode_eqcmd_string8_string16_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string8_field(data, start, fields, f"{prefix}.text8")
    if offset == start:
        return start
    return decode_string16_fields(data, offset, fields, (f"{prefix}.text16",))


def decode_item_detail_variant0_list_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", f"{prefix}.u8_0", value)
    offset = start + 1
    for index in range(2):
        next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_{index}")
        if next_offset == offset:
            return offset
        offset = next_offset
    return decode_string8_field(data, offset, fields, f"{prefix}.text8")


def decode_item_detail_variant0_case(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str, subtype: int
) -> int:
    offset = start
    if subtype == 1:
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.u8_0", value)
        offset += 1
        for index in range(6):
            next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_{index}")
            if next_offset == offset:
                return offset
            offset = next_offset
        for index in range(2):
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"{prefix}.u8_{index + 1}", value)
            offset += 1
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
        return offset + 4

    if subtype == 2:
        for index in range(6):
            next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_{index}")
            if next_offset == offset:
                return offset
            offset = next_offset
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.u8_0", value)
        offset += 1
        next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_6")
        if next_offset == offset:
            return offset
        offset = next_offset
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.u8_1", value)
        offset += 1
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
        return offset + 4

    if subtype in {3, 4}:
        for index in range(2):
            next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_{index}")
            if next_offset == offset:
                return offset
            offset = next_offset
        return offset

    if subtype == 5:
        for index in range(3):
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"{prefix}.u8_{index}", value)
            offset += 1
        offset = decode_string8_field(data, offset, fields, f"{prefix}.text8")
        if offset == start + 3:
            return offset
        packed = read_packed_u16(data, offset)
        if packed is None:
            return offset
        count, consumed = packed
        add_field(fields, offset, consumed, "packed_u16", f"{prefix}.string8_count", count)
        offset += consumed
        for index in range(count):
            next_offset = decode_string8_field(data, offset, fields, f"{prefix}.string8[{index}]")
            if next_offset == offset:
                return offset
            offset = next_offset
        return offset

    if subtype == 6:
        return decode_eqcmd_widget_subrecord(data, offset, fields, prefix, include_optional_sections=True)

    if subtype == 7:
        packed = read_packed_u16(data, offset)
        if packed is None:
            return offset
        count, consumed = packed
        add_field(fields, offset, consumed, "packed_u16", f"{prefix}.string8_count", count)
        offset += consumed
        for index in range(count):
            next_offset = decode_string8_field(data, offset, fields, f"{prefix}.string8[{index}]")
            if next_offset == offset:
                return offset
            offset = next_offset
        return offset

    if subtype == 8:
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.u8_0", value)
        offset += 1
        next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_0")
        if next_offset == offset:
            return offset
        offset = next_offset
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
        return offset + 4

    if subtype == 9:
        for index in range(4):
            next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_{index}")
            if next_offset == offset:
                return offset
            offset = next_offset
        for index in range(2):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
            offset += 4
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.u8_0", value)
        return offset + 1

    if subtype == 10:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
        return offset + 4

    return offset


def decode_item_detail_variant0(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string8_field(data, start, fields, f"{prefix}.text8_0")
    if offset == start:
        return start

    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
    offset += 4
    if value == 0xFFFFFFFF:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_if_minus1", value)
        offset += 4
    else:
        for index in range(2):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"{prefix}.u32_pair_{index}", value)
            offset += 4

    for size, kind, name in (
        (2, "u16", "u16_0"),
        (1, "u8", "u8_0"),
        (2, "u16", "u16_1"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size

    for index in range(2):
        next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_{index}")
        if next_offset == offset:
            return offset
        offset = next_offset

    for size, name in ((5, "bytes_0"), (10, "bytes_1")):
        if offset + size > len(data):
            return offset
        add_field(fields, offset, size, "bytes", f"{prefix}.{name}", data[offset : offset + size].hex())
        offset += size

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.list0_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_item_detail_variant0_list_entry(
            data, offset, fields, f"{prefix}.list0[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.string_pair_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_string8_string16_entry(
            data, offset, fields, f"{prefix}.string_pair[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset

    for size, kind, name in ((1, "u8", "u8_1"), (2, "u16", "u16_2"), (4, "u32", "u32_3"), (4, "u32", "u32_4")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size

    next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_2")
    if next_offset == offset:
        return offset
    offset = next_offset

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.small_record_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_widget_subrecord_small(
            data, offset, fields, f"{prefix}.small_record[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.byte_count", count)
    offset += 1
    if offset + count > len(data):
        return offset
    for index in range(count):
        add_field(fields, offset, 1, "u8", f"{prefix}.byte[{index}]", data[offset])
        offset += 1

    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_5", value)
    offset += 4

    subtype = read_u(data, offset, 1)
    if subtype is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.subtype", subtype)
    offset += 1
    next_offset = decode_item_detail_variant0_case(data, offset, fields, f"{prefix}.case", subtype)
    if next_offset == offset and subtype in set(range(1, 11)):
        return offset
    offset = next_offset

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.string16_entry_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_widget_subrecord_string16_entry(
            data, offset, fields, f"{prefix}.string16_entry[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset

    return decode_eqcmd_tail_strings(data, offset, fields, prefix)


def decode_item_detail_variant1(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    for size, kind, name in (
        (4, "u32", "u32_0"),
        (2, "u16", "u16_0"),
        (2, "u16", "u16_1"),
        (1, "u8", "u8_0"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.string16_entry_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_widget_subrecord_string16_entry(
            data, offset, fields, f"{prefix}.string16_entry[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset
    return decode_eqcmd_tail_strings(data, offset, fields, prefix)


def decode_item_detail_variant2(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
    offset += 4

    for index in range(2):
        next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_{index}")
        if next_offset == offset:
            return offset
        offset = next_offset

    for size, kind, name in (
        (4, "u32", "u32_1"),
        (4, "u32", "u32_2"),
        (1, "u8", "u8_0"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size

    prev_offset = offset
    offset = decode_string8_field(data, offset, fields, f"{prefix}.text8_0")
    if offset == prev_offset:
        return offset

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.small_record_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_widget_subrecord_small(
            data, offset, fields, f"{prefix}.small_record[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset

    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.u8_1", value)
    offset += 1

    for block_index in range(4):
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"{prefix}.block[{block_index}].u16_0", value)
        offset += 2
        next_offset = decode_string8_field(data, offset, fields, f"{prefix}.block[{block_index}].text8_0")
        if next_offset == offset:
            return offset
        offset = next_offset
        for u16_index in range(1, 3):
            value = read_u(data, offset, 2)
            if value is None:
                return offset
            add_field(fields, offset, 2, "u16", f"{prefix}.block[{block_index}].u16_{u16_index}", value)
            offset += 2
        next_offset = decode_string8_field(data, offset, fields, f"{prefix}.block[{block_index}].text8_1")
        if next_offset == offset:
            return offset
        offset = next_offset
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"{prefix}.block[{block_index}].u16_3", value)
        offset += 2

    prev_offset = offset
    offset = decode_string8_field(data, offset, fields, f"{prefix}.text8_1")
    if offset == prev_offset:
        return offset

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.string_flag_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_string8_field(data, offset, fields, f"{prefix}.string_flag[{index}].text8")
        if next_offset == offset:
            return offset
        offset = next_offset
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.string_flag[{index}].flag", value)
        offset += 1

    prev_offset = offset
    offset = decode_string8_field(data, offset, fields, f"{prefix}.text8_2")
    if offset == prev_offset:
        return offset
    next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_2")
    if next_offset == offset:
        return offset
    return decode_eqcmd_tail_strings(data, next_offset, fields, prefix)


def decode_item_detail_helper(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> tuple[int, bool]:
    value = read_u(data, start, 4)
    if value is None:
        return start, True
    add_field(fields, start, 4, "u32", f"{prefix}.detail_type", value)
    offset = start + 4
    if value == 0:
        next_offset = decode_item_detail_variant0(data, offset, fields, f"{prefix}.variant0")
    elif value == 1:
        next_offset = decode_item_detail_variant1(data, offset, fields, f"{prefix}.variant1")
    elif value == 2:
        next_offset = decode_item_detail_variant2(data, offset, fields, f"{prefix}.variant2")
    elif value == 3:
        next_offset = decode_eqcmd_widget_subrecord(
            data, offset, fields, f"{prefix}.variant3", include_optional_sections=True
        )
    else:
        return offset, True

    if next_offset == offset:
        add_field(
            fields,
            offset,
            len(data) - offset,
            "bytes",
            f"{prefix}.detail_payload_raw",
            "unparsed item-detail helper payload",
        )
        return len(data), False
    return next_offset, True


def decode_item_creation_slot_state(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> tuple[int, bool]:
    offset = start
    value = read_u(data, offset, 4)
    if value is None:
        return offset, True
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
    offset += 4

    next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_0",))
    if next_offset == offset:
        return offset, True
    offset = next_offset

    for index in range(2):
        value = read_u(data, offset, 2)
        if value is None:
            return offset, True
        add_field(fields, offset, 2, "u16", f"{prefix}.u16_{index}", value)
        offset += 2

    offset, complete = decode_item_detail_helper(data, offset, fields, f"{prefix}.detail_0")
    if not complete:
        return offset, False

    next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_1",))
    if next_offset == offset:
        return offset, True
    offset = next_offset

    for index in range(2, 4):
        value = read_u(data, offset, 2)
        if value is None:
            return offset, True
        add_field(fields, offset, 2, "u16", f"{prefix}.u16_{index}", value)
        offset += 2

    return decode_item_detail_helper(data, offset, fields, f"{prefix}.detail_1")


def decode_show_item_creation_process(data: bytes, start: int, fields: list[dict[str, Any]]) -> tuple[int, bool]:
    offset = start
    for index in range(4):
        value = read_u(data, offset, 4)
        if value is None:
            return offset, True
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    for index in range(2):
        value = read_u(data, offset, 1)
        if value is None:
            return offset, True
        add_field(fields, offset, 1, "u8", f"u8_{index}", value)
        offset += 1

    for slot_index in range(5):
        offset, complete = decode_item_creation_slot_state(data, offset, fields, f"slot[{slot_index}]")
        if not complete:
            return offset, False
    return offset, True


def decode_update_item_creation_process(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for size, kind, name in (
        (1, "u8", "u8_0"),
        (4, "u32", "u32_0"),
        (4, "u32", "u32_1"),
        (1, "u8", "u8_1"),
        (2, "u16", "u16_0"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", "text", text)
    offset += consumed

    for index in range(2, 5):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return offset


def decode_show_recipe_book(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    if start + 0x29 > len(data):
        return start
    add_field(fields, start, 0x29, "bytes", "recipe_book_header", "0x29 bytes")
    offset = start + 0x29
    bit_count = read_u(data, offset, 4)
    if bit_count is None:
        return offset
    add_field(fields, offset, 4, "u32", "bit_count", bit_count)
    offset += 4
    word_count = (bit_count + 31) // 32
    mask_size = word_count * 4
    if offset + mask_size > len(data):
        return offset
    add_field(fields, offset, mask_size, "bytes", "recipe_mask_words", f"{word_count} u32 words")
    return offset + mask_size


def decode_u32_two_string16(
    data: bytes, start: int, fields: list[dict[str, Any]], names: tuple[str, str]
) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    return decode_string16_fields(data, start + 4, fields, names)


def decode_knowledgebase_response(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    offset += 4

    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "summary_count", count)
    offset += 4
    for index in range(count):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"summary[{index}].title", text)
        offset += consumed

        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"summary[{index}].u32", value)
        offset += 4

        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"summary[{index}].body", text)
        offset += consumed

    decoded = read_len_string(data, offset, 2)
    if decoded is not None:
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", "footer", text)
        offset += consumed
    return offset


def decode_cs_ticket_info(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    offset += 4

    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "ticket_count", count)
    offset += 4
    for index in range(count):
        for scalar_index in range(3):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"ticket[{index}].u32_{scalar_index}", value)
            offset += 4

        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"ticket[{index}].text_0", text)
        offset += consumed

        for scalar_index in range(3, 6):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"ticket[{index}].u32_{scalar_index}", value)
            offset += 4

        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"ticket[{index}].text_1", text)
        offset += consumed
    return offset


def decode_cs_ticket_comment_response(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4

    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "comment_count", count)
    offset += 4
    for index in range(count):
        flag = read_u(data, offset, 1)
        if flag is None:
            return offset
        add_field(fields, offset, 1, "u8", f"comment[{index}].flag", flag)
        offset += 1

        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"comment[{index}].u32", value)
        offset += 4

        for string_index in range(2):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u16", f"comment[{index}].text_{string_index}", text)
            offset += consumed
    return offset


def decode_three_u32_three_string16(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for index in range(3):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return decode_string16_fields(data, offset, fields, ("text_0", "text_1", "text_2"))


def decode_three_u32_string16(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(3):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return decode_string16_fields(data, offset, fields, ("text",))


def decode_two_u32_string16(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return decode_string16_fields(data, offset, fields, ("text",))


def decode_world_data_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("name",))
    if offset == start:
        return offset
    if offset + 8 > len(data):
        return offset
    add_field(fields, offset, 8, "bytes", "raw_u64_or_pair", "8 bytes")
    offset += 8

    blob_size = read_u(data, offset, 2)
    if blob_size is None:
        return offset
    add_field(fields, offset, 2, "u16", "blob_size", blob_size)
    offset += 2
    if offset + blob_size > len(data):
        return offset
    add_field(fields, offset, blob_size, "bytes", "blob", f"{blob_size} bytes")
    offset += blob_size

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", "text", text)
    offset += consumed

    trailing = read_u(data, offset, 1)
    if trailing is not None:
        add_field(fields, offset, 1, "u8", "trailing_flag", trailing)
        offset += 1
    return offset


def decode_known_languages(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 1)
    if count is None:
        return start
    add_field(fields, start, 1, "u8", "language_count", count)
    offset = start + 1
    for index in range(count):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"language[{index}]", value)
        offset += 1
    trailing = read_u(data, offset, 1)
    if trailing is not None:
        add_field(fields, offset, 1, "u8", "trailing_flag", trailing)
        offset += 1
    return offset


def decode_client_teleport_to_location(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = decode_string16_fields(data, start, fields, ("zone", "location", "display_name"))
    if offset == start:
        return offset
    if offset + 0x0C > len(data):
        return offset
    add_field(fields, offset, 0x0C, "bytes", "coords_raw", "0x0c bytes")
    offset += 0x0C
    for index in range(2):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"flag_{index}", value)
        offset += 1
    return offset


def decode_two_string_four_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text_0", "text_1"))
    if offset == start:
        return offset
    for index in range(4):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return offset


def decode_string_three_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text",))
    if offset == start:
        return offset
    for index in range(3):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return offset


def decode_string_u32_u8_u32_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text",))
    if offset == start:
        return offset
    for size, kind, name in (
        (4, "u32", "u32_0"),
        (1, "u8", "flag"),
        (4, "u32", "u32_1"),
        (4, "u32", "u32_2"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_create_boat_transports(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 1)
    if count is None:
        return start
    add_field(fields, start, 1, "u8", "boat_count", count)
    offset = start + 1
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"boat[{index}].u32", value)
        offset += 4
        for name in ("name", "zone", "destination"):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u16", f"boat[{index}].{name}", text)
            offset += consumed
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"boat[{index}].u16", value)
        offset += 2
    return offset


def decode_debug_nll_points(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("zone",))
    if offset == start:
        return offset
    for list_index in range(3):
        count = read_u(data, offset, 2)
        if count is None:
            return offset
        add_field(fields, offset, 2, "u16", f"list[{list_index}].count", count)
        offset += 2
        for entry_index in range(count):
            for scalar_index in range(6):
                value = read_u(data, offset, 4)
                if value is None:
                    return offset
                add_field(
                    fields,
                    offset,
                    4,
                    "u32",
                    f"list[{list_index}].entry[{entry_index}].u32_{scalar_index}",
                    value,
                )
                offset += 4
    return offset


def decode_quickbar_entry(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = start
    for index in range(8):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    for string_index in range(2):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"{prefix}.text_{string_index}", text)
        offset += consumed
    return offset


def decode_quickbar_init(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "entry_count", count)
    offset = start + 4
    for index in range(count):
        next_offset = decode_quickbar_entry(data, offset, fields, f"entry[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_macro_entry(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", f"{prefix}.u8_0", value)
    offset = start + 1

    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", f"{prefix}.name", text)
    offset += consumed

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.command_count", count)
    offset += 1
    for index in range(count):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"{prefix}.command[{index}]", text)
        offset += consumed

    value = read_u(data, offset, 2)
    if value is not None:
        add_field(fields, offset, 2, "u16", f"{prefix}.u16_0", value)
        offset += 2
    return offset


def decode_macro_init(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "entry_count", count)
    offset = start + 4
    for index in range(count):
        next_offset = decode_macro_entry(data, offset, fields, f"macro[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_questionnaire(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    offset += 4

    flag = read_u(data, offset, 1)
    if flag is None:
        return offset
    add_field(fields, offset, 1, "u8", "has_body", flag)
    offset += 1
    if flag == 0:
        return offset

    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_1", value)
    offset += 4

    for index in range(9):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"text_{index}", text)
        offset += consumed

    for index in range(3):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"flag_{index}", value)
        offset += 1
    return offset


def decode_display_warning_msg(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for size, kind, name in (
        (1, "u8", "u8_0"),
        (4, "u32", "u32_0"),
        (4, "u32", "u32_1"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    offset = decode_string16_fields(data, offset, fields, ("text",))
    for size, kind, name in (
        (1, "u8", "u8_1"),
        (1, "u8", "u8_2"),
        (1, "u8", "u8_3"),
        (2, "u16", "u16_0"),
        (2, "u16", "u16_1"),
        (4, "u32", "u32_2"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_onscreen_msg(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", "u8_0", value)
    offset = decode_string16_fields(data, start + 1, fields, ("text_0", "text_1"))
    for size, kind, name in (
        (4, "u32", "u32_0"),
        (1, "u8", "u8_1"),
        (1, "u8", "u8_2"),
        (1, "u8", "u8_3"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_modify_guild(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(3):
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", f"text_{index}", text)
        offset += consumed
    for index in range(3):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"flag_{index}", value)
        offset += 1
    return offset


def decode_guild_event(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(5):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return decode_string16_fields(data, offset, fields, ("text_0", "text_1"))


def decode_u32_u32_string16_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    offset = decode_string16_fields(data, offset, fields, ("text",))
    value = read_u(data, offset, 4)
    if value is not None:
        add_field(fields, offset, 4, "u32", "u32_2", value)
        offset += 4
    return offset


def decode_u32_u64_u32_u32_string16(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for size, kind, name in (
        (4, "u32", "u32_0"),
        (8, "u64", "u64_0"),
        (4, "u32", "u32_1"),
        (4, "u32", "u32_2"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return decode_string16_fields(data, offset, fields, ("text",))


def decode_u32_u16_u64_list(
    data: bytes,
    start: int,
    fields: list[dict[str, Any]],
    prefix: str,
    include_flags: bool = False,
) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    offset = start + 4

    count = read_u(data, offset, 2)
    if count is None:
        return offset
    add_field(fields, offset, 2, "u16", f"{prefix}_count", count)
    offset += 2

    for index in range(count):
        value = read_u(data, offset, 8)
        if value is None:
            return offset
        add_field(fields, offset, 8, "u64", f"{prefix}[{index}].id", value)
        offset += 8

    if include_flags:
        for index in range(count):
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"{prefix}[{index}].flag", value)
            offset += 1
    return offset


def decode_u32_u8_u16_u64_list(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    for size, kind, name in ((4, "u32", "u32_0"), (1, "u8", "u8_0")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    count = read_u(data, offset, 2)
    if count is None:
        return offset
    add_field(fields, offset, 2, "u16", f"{prefix}_count", count)
    offset += 2

    for index in range(count):
        value = read_u(data, offset, 8)
        if value is None:
            return offset
        add_field(fields, offset, 8, "u64", f"{prefix}[{index}].id", value)
        offset += 8
    return offset


def decode_raw12(data: bytes, start: int, fields: list[dict[str, Any]], name: str) -> int:
    if start + 0x0C > len(data):
        return start
    add_field(fields, start, 0x0C, "bytes", name, data[start : start + 0x0C].hex())
    return start + 0x0C


def decode_guild_bank_action(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    layout = (
        (4, "u32", "u32_0"),
        (1, "u8", "u8_0"),
        (1, "u8", "u8_1"),
        (1, "u8", "u8_2"),
        (4, "u32", "u32_1"),
        (4, "u32", "u32_2"),
        (4, "u32", "u32_3"),
    )
    for size, kind, name in layout:
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    next_offset = decode_raw12(data, offset, fields, "raw_0c")
    if next_offset == offset:
        return offset
    offset = next_offset

    for size, kind, name in ((1, "u8", "u8_3"), (4, "u32", "u32_4")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    for name in ("text_0", "text_1"):
        next_offset = decode_string8_field(data, offset, fields, name)
        if next_offset == offset:
            return offset
        offset = next_offset

    offset = decode_u16_sized_raw(data, offset, fields, "blob_size", "guild_bank_blob")
    for size, kind, name in ((8, "u64", "u64_0"), (1, "u8", "u8_4")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_guild_bank_action_response(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    layout = (
        (4, "u32", "u32_0"),
        (1, "u8", "u8_0"),
        (1, "u8", "u8_1"),
        (4, "u32", "u32_1"),
        (4, "u32", "u32_2"),
    )
    for size, kind, name in layout:
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    next_offset = decode_raw12(data, offset, fields, "raw_0c")
    if next_offset == offset:
        return offset
    offset = next_offset

    for size, kind, name in ((4, "u32", "u32_3"), (8, "u64", "u64_0"), (1, "u8", "u8_2")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    return decode_u16_sized_raw(data, offset, fields, "blob_size", "guild_bank_blob")


def decode_guild_bank_item_details_response(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    layout = (
        (4, "u32", "u32_0"),
        (1, "u8", "u8_0"),
        (4, "u32", "u32_1"),
        (4, "u32", "u32_2"),
    )
    for size, kind, name in layout:
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return decode_u16_sized_raw(data, offset, fields, "blob_size", "item_details_blob")


def decode_guild_bank_small_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for size, kind, name in (
        (4, "u32", "u32_0"),
        (1, "u8", "u8_0"),
        (4, "u32", "u32_1"),
        (1, "u8", "u8_1"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    next_offset = decode_raw12(data, offset, fields, "raw_0c")
    if next_offset == offset:
        return offset
    return decode_string8_field(data, next_offset, fields, "text")


def decode_guild_bank_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for size, kind, name in ((4, "u32", "u32_0"), (1, "u8", "u8_0")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return decode_raw_blob_with_size(
        data,
        offset,
        fields,
        count_name=None,
        count_size=0,
        size_name="blob_size",
        blob_name="guild_bank_update_blob",
    )


def decode_reward_pack(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", "u8_0", value)
    offset = decode_string16_fields(data, start + 1, fields, ("text_0",))
    if offset == start + 1:
        return offset

    for size, kind, name in (
        (8, "u64", "u64_0"),
        (8, "u64", "u64_1"),
        (4, "u32", "u32_0"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size

    next_offset = decode_string16_fields(data, offset, fields, ("text_1",))
    if next_offset == offset:
        return offset
    offset = next_offset

    value = read_u(data, offset, 2)
    if value is None:
        return offset
    add_field(fields, offset, 2, "u16", "u16_0", value)
    offset += 2

    for list_index in range(2):
        count = read_u(data, offset, 4)
        if count is None:
            return offset
        add_field(fields, offset, 4, "u32", f"item_list[{list_index}].count", count)
        offset += 4
        for entry_index in range(count):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"item_list[{list_index}].entry[{entry_index}].u32_0", value)
            offset += 4
            value = read_u(data, offset, 2)
            if value is None:
                return offset
            add_field(fields, offset, 2, "u16", f"item_list[{list_index}].entry[{entry_index}].u16_0", value)
            offset += 2
            offset, complete = decode_item_detail_helper(
                data,
                offset,
                fields,
                f"item_list[{list_index}].entry[{entry_index}].detail",
            )
            if not complete:
                return offset

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "string_entry_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_string16_fields(data, offset, fields, (f"string_entry[{index}].text",))
        if next_offset == offset:
            return offset
        offset = next_offset
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"string_entry[{index}].u32", value)
        offset += 4
    return offset


def decode_offer_quest(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_reward_pack(data, start, fields)
    if offset == start or offset >= len(data):
        return offset

    next_offset = decode_string8_field(data, offset, fields, "offer_text_0")
    if next_offset == offset:
        return offset
    offset = next_offset

    for index in range(3):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"offer_flag_{index}", value)
        offset += 1

    for name in ("offer_text_1", "offer_text_2"):
        next_offset = decode_string8_field(data, offset, fields, name)
        if next_offset == offset:
            return offset
        offset = next_offset
    return decode_string16_fields(data, offset, fields, ("offer_text_3",))


def decode_u8_u32_two_string16(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", "u8_0", value)
    offset = start + 1
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    return decode_string16_fields(data, offset + 4, fields, ("text_0", "text_1"))


def decode_u8_u32_string16(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", "u8_0", value)
    offset = start + 1
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    return decode_string16_fields(data, offset + 4, fields, ("text",))


def decode_mail_attachment_payload(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    for name in ("coin_copper", "coin_silver", "coin_gold", "coin_plat"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.{name}", value)
        offset += 4

    value = read_u(data, offset, 2)
    if value is None:
        return offset
    add_field(fields, offset, 2, "u16", f"{prefix}.item_packet_type_or_end_tag", value)
    offset += 2

    next_offset = decode_item_detail_variant0(data, offset, fields, f"{prefix}.item_detail0")
    if next_offset == offset:
        return offset
    offset = next_offset

    blob_size = read_u(data, offset, 4)
    if blob_size is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.blob_size", blob_size)
    offset += 4
    if offset + blob_size > len(data):
        return offset
    if blob_size:
        add_field(fields, offset, blob_size, "bytes", f"{prefix}.blob", f"{blob_size} bytes")
    return offset + blob_size


def decode_mail_message_record(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    for name in ("mail_id", "player_to_id"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.{name}", value)
        offset += 4

    for name in (
        "text_0_player_from_or_subject",
        "text_1_subject_or_player_from",
        "text_2_mail_body_or_empty",
    ):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"{prefix}.{name}", text)
        offset += consumed

    for size, kind, name in (
        (1, "u8", "already_read_or_unknown1"),
        (4, "u32", "mail_deletion_or_unknown2"),
        (1, "u8", "mail_type_or_lock_report_button"),
        (4, "u32", "mail_expire_or_unknown3"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size
    return decode_mail_attachment_payload(data, offset, fields, f"{prefix}.attachment")


def decode_mail_send_message(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("player_to", "subject", "mail_body"))
    if offset == start:
        return offset
    for size, kind, name in (
        (1, "u8", "send_flag"),
        (8, "u64", "mail_fee_or_timestamp"),
        (4, "u32", "unknown_u32_0"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return decode_mail_attachment_payload(data, offset, fields, "attachment")


def decode_mail_get_headers_reply(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "kiosk_or_mailbox_id", value)
    offset = start + 4

    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "message_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_mail_message_record(data, offset, fields, f"message[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset

    for name in ("postage_cost", "attachment_cost", "unknown_tail_u32"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", name, value)
        offset += 4
    return offset


def decode_mail_get_message_reply(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "kiosk_or_mailbox_id", value)
    record_start = start + 4
    offset = decode_mail_message_record(data, record_start, fields, "message")
    if offset == record_start:
        return offset
    for size, kind, name in ((8, "u64", "u64_0"), (1, "u8", "flag")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_mail_send_message_reply(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for size, kind, name in ((8, "u64", "u64_0"), (4, "u32", "u32_0"), (1, "u8", "reply_type")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    offset = decode_string16_fields(data, offset, fields, ("recipient",))
    for size, kind, name in ((4, "u32", "u32_1"), (1, "u8", "flag"), (4, "u32", "u32_2")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_u64_u32_string16_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for size, kind, name in ((8, "u64", "u64_0"), (4, "u32", "u32_0")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    offset = decode_string16_fields(data, offset, fields, ("text",))
    value = read_u(data, offset, 4)
    if value is not None:
        add_field(fields, offset, 4, "u32", "u32_1", value)
        offset += 4
    return offset


def decode_mail_send_system_message(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(
        data, start, fields, ("recipient", "sender", "subject", "body")
    )
    if offset == start:
        return offset
    value = read_u(data, offset, 8)
    if value is None:
        return offset
    add_field(fields, offset, 8, "u64", "u64_0", value)
    return decode_mail_attachment_payload(data, offset + 8, fields, "attachment")


def decode_string_u32_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text",))
    if offset == start:
        return offset
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return offset


def decode_waypoint_entry(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = decode_string16_fields(data, start, fields, (f"{prefix}.name",))
    if offset == start:
        return offset
    for size, kind, name in ((1, "u8", "category"), (4, "u32", "spawn_or_entry_id")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size
    return offset


def decode_waypoint_list(
    data: bytes, start: int, fields: list[dict[str, Any]], include_update_flag: bool
) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "waypoint_count", count)
    offset = start + 4
    for index in range(count):
        next_offset = decode_waypoint_entry(data, offset, fields, f"waypoint[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    if include_update_flag:
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", "update_flag", value)
        offset += 1
    value = read_u(data, offset, 4)
    if value is not None:
        add_field(fields, offset, 4, "u32", "u32_tail", value)
        offset += 4
    return offset


def decode_two_string_three_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text_0", "text_1"))
    if offset == start:
        return offset
    for index in range(3):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return offset


def decode_show_zone_teleporter_destinations(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "teleporter_id", value)
    offset = start + 4
    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "destination_count", count)
    offset += 4
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"destination[{index}].id", value)
        offset += 4
        for name in ("name", "zone"):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u16", f"destination[{index}].{name}", text)
            offset += consumed
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"destination[{index}].u32", value)
        offset += 4
    return offset


def decode_guild_member_record(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", f"{prefix}.character_id", value)
    offset = decode_string16_fields(data, start + 4, fields, (f"{prefix}.name",))
    if offset == start + 4:
        return offset
    for index in range(8):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    for index in range(2):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.flag_{index}", value)
        offset += 1
    zone_start = offset
    offset = decode_string16_fields(data, offset, fields, (f"{prefix}.zone",))
    if offset == zone_start:
        return offset
    for index in range(8, 10):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    value = read_u(data, offset, 2)
    if value is None:
        return offset
    add_field(fields, offset, 2, "u16", f"{prefix}.u16_0", value)
    offset += 2
    note_start = offset
    offset = decode_string16_fields(data, offset, fields, (f"{prefix}.note", f"{prefix}.officer_note"))
    if offset == note_start:
        return offset
    value = read_u(data, offset, 4)
    if value is not None:
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_tail", value)
        offset += 4
    return offset


def decode_guild_membership_response(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for name in ("guild_id", "target_character_id"):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", name, value)
        offset += 4
    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "member_count", count)
    offset += 4
    for index in range(count):
        next_offset = decode_guild_member_record(data, offset, fields, f"member[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    value = read_u(data, offset, 2)
    if value is not None:
        add_field(fields, offset, 2, "u16", "u16_tail", value)
        offset += 2
    return offset


def decode_join_guild_notify(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "guild_id", value)
    record_start = start + 4
    offset = decode_guild_member_record(data, record_start, fields, "member")
    if offset == record_start:
        return offset
    value = read_u(data, offset, 2)
    if value is not None:
        add_field(fields, offset, 2, "u16", "u16_tail", value)
        offset += 2
    return offset


def decode_cs_category_response(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    offset = start + 4
    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "category_count", count)
    offset += 4
    for category_index in range(count):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"category[{category_index}].name", text)
        offset += consumed
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"category[{category_index}].u32", value)
        offset += 4
        nested_count = read_u(data, offset, 4)
        if nested_count is None:
            return offset
        add_field(fields, offset, 4, "u32", f"category[{category_index}].entry_count", nested_count)
        offset += 4
        for entry_index in range(nested_count):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(
                fields,
                offset,
                consumed,
                "string_u16",
                f"category[{category_index}].entry[{entry_index}].name",
                text,
            )
            offset += consumed
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(
                fields,
                offset,
                4,
                "u32",
                f"category[{category_index}].entry[{entry_index}].u32",
                value,
            )
            offset += 4
    return offset


def decode_knowledge_window_slot_mapping(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    count = read_u(data, start, 2)
    if count is None:
        return start
    add_field(fields, start, 2, "u16", "spell_count", count)
    offset = start + 2
    for index in range(count):
        spell_id = read_u(data, offset, 4)
        slot_id = read_u(data, offset + 4, 2)
        if spell_id is None or slot_id is None:
            return offset
        add_field(fields, offset, 4, "u32", f"spell[{index}].spell_id", spell_id)
        add_field(fields, offset + 4, 2, "u16", f"spell[{index}].slot_id", slot_id)
        offset += 6
    return offset


def decode_u32_u8_string16(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for size, kind, name in ((4, "u32", "u32_0"), (1, "u8", "flag")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return decode_string16_fields(data, offset, fields, ("text",))


def decode_update_active_public_zones(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "zone_count", count)
    offset = start + 4
    for zone_index in range(count):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"zone[{zone_index}].name", text)
        offset += consumed

        group_count = read_u(data, offset, 4)
        if group_count is None:
            return offset
        add_field(fields, offset, 4, "u32", f"zone[{zone_index}].group_count", group_count)
        offset += 4
        for group_index in range(group_count):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(
                fields,
                offset,
                consumed,
                "string_u16",
                f"zone[{zone_index}].group[{group_index}].name",
                text,
            )
            offset += consumed
            for flag_index in range(2):
                value = read_u(data, offset, 1)
                if value is None:
                    return offset
                add_field(
                    fields,
                    offset,
                    1,
                    "u8",
                    f"zone[{zone_index}].group[{group_index}].flag_{flag_index}",
                    value,
                )
                offset += 1
                if flag_index == 1 and value != 0:
                    member_count = read_u(data, offset, 4)
                    if member_count is None:
                        return offset
                    add_field(
                        fields,
                        offset,
                        4,
                        "u32",
                        f"zone[{zone_index}].group[{group_index}].member_count",
                        member_count,
                    )
                    offset += 4
                    for member_index in range(member_count):
                        member_id = read_u(data, offset, 4)
                        if member_id is None:
                            return offset
                        add_field(
                            fields,
                            offset,
                            4,
                            "u32",
                            f"zone[{zone_index}].group[{group_index}].member[{member_index}]",
                            member_id,
                        )
                        offset += 4
    return offset


def decode_promo_flags_details(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "item_count", count)
    offset = start + 4
    for item_index in range(count):
        for size, kind, name in (
            (4, "u32", "id"),
            (1, "u8", "not_yet_claimed"),
            (4, "u32", "num_remaining"),
            (1, "u8", "one_per_character"),
        ):
            value = read_u(data, offset, size)
            if value is None:
                return offset
            add_field(fields, offset, size, kind, f"item[{item_index}].{name}", value)
            offset += size
        for name in ("item_name", "text"):
            decoded = read_len_string(data, offset, 2)
            if decoded is None:
                return offset
            text, _text_size, consumed = decoded
            add_field(fields, offset, consumed, "string_u16", f"item[{item_index}].{name}", text)
            offset += consumed
        sub_count = read_u(data, offset, 1)
        if sub_count is None:
            return offset
        add_field(fields, offset, 1, "u8", f"item[{item_index}].subentry_count", sub_count)
        offset += 1
        for sub_index in range(sub_count):
            value = read_u(data, offset, 4)
            flag = read_u(data, offset + 4, 1)
            if value is None or flag is None:
                return offset
            add_field(fields, offset, 4, "u32", f"item[{item_index}].subentry[{sub_index}].u32", value)
            add_field(fields, offset + 4, 1, "u8", f"item[{item_index}].subentry[{sub_index}].flag", flag)
            offset += 5

    detail_count = read_u(data, offset, 2)
    if detail_count is not None:
        add_field(fields, offset, 2, "u16", "item_detail_count", detail_count)
        offset += 2
        for detail_index in range(detail_count):
            next_offset = decode_item_detail_variant0(data, offset, fields, f"item_detail[{detail_index}]")
            if next_offset == offset:
                return offset
            offset = next_offset
    return offset


def decode_consign_view_create_header(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string16_fields(data, start, fields, (f"{prefix}.text_0",))
    if offset == start:
        return offset
    for index in range(3):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_1", f"{prefix}.text_2"))
    for index in range(3, 5):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    for index in range(2):
        value = read_u(data, offset, 8)
        if value is None:
            return offset
        add_field(fields, offset, 8, "u64", f"{prefix}.u64_{index}", value)
        offset += 8
    for index in range(5, 10):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    return decode_string16_fields(data, offset, fields, (f"{prefix}.text_3",))


def decode_consign_view_create(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    value = read_u(data, offset, 8)
    if value is None:
        return offset
    add_field(fields, offset, 8, "u64", "u64_0", value)
    offset += 8
    for index in range(3):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    header_start = offset
    offset = decode_consign_view_create_header(data, offset, fields, "header")
    if offset == header_start:
        return offset
    count = read_u(data, offset, 4)
    if count is None:
        return offset
    add_field(fields, offset, 4, "u32", "skill_count", count)
    offset += 4
    for index in range(count):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"skill[{index}].name", text)
        offset += consumed
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"skill[{index}].u32", value)
        offset += 4
    value = read_u(data, offset, 4)
    if value is not None:
        add_field(fields, offset, 4, "u32", "u32_tail", value)
        offset += 4
    return offset


def decode_six_u32_raw12_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(6):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    if offset + 0x0C > len(data):
        return offset
    add_field(fields, offset, 0x0C, "bytes", "raw_0x0c", data[offset : offset + 0x0C].hex())
    offset += 0x0C
    value = read_u(data, offset, 4)
    if value is not None:
        add_field(fields, offset, 4, "u32", "u32_6", value)
        offset += 4
    return offset


def decode_u32_u32_u8_string16_u8(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for size, kind, name in ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (1, "u8", "flag_0")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    offset = decode_string16_fields(data, offset, fields, ("text",))
    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", "flag_1", value)
        offset += 1
    return offset


def decode_u64_u32_u32_u64_u32_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    layout = (
        (8, "u64", "u64_0"),
        (4, "u32", "u32_0"),
        (4, "u32", "u32_1"),
        (8, "u64", "u64_1"),
        (4, "u32", "u32_2"),
        (4, "u32", "u32_3"),
    )
    for size, kind, name in layout:
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_scalar_layout(
    data: bytes, start: int, fields: list[dict[str, Any]], layout: tuple[tuple[int, str, str], ...]
) -> int:
    offset = start
    for size, kind, name in layout:
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return offset


def decode_client_fell(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    height = read_float(data, start)
    raw_height = read_u(data, start, 4)
    spawn_id = read_u(data, start + 4, 4)
    if height is None or raw_height is None or spawn_id is None:
        return start
    add_field(fields, start, 4, "float/u32", "height", {"float": height, "u32": raw_height})
    add_field(fields, start + 4, 4, "u32", "spawn_id", spawn_id)
    return start + 8


def decode_title_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 2)
    if count is None:
        return start
    add_field(fields, start, 2, "u16", "title_count", count)
    offset = start + 2
    for index in range(count):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"title[{index}].text", text)
        offset += consumed
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"title[{index}].prefix", value)
        offset += 1
    for index in range(2):
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"u16_tail_{index}", value)
        offset += 2
    return offset


def decode_tracking_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    mode = read_u(data, start, 1)
    if mode is None:
        return start
    add_field(fields, start, 1, "u8", "mode", mode)
    offset = start + 1

    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    spawn_count, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "spawn_count", spawn_count)
    offset += consumed
    for index in range(spawn_count):
        spawn_id = read_u(data, offset, 4)
        if spawn_id is None:
            return offset
        add_field(fields, offset, 4, "u32", f"spawn[{index}].id", spawn_id)
        offset += 4
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", f"spawn[{index}].name", text)
        offset += consumed
        for flag_index in range(2):
            value = read_u(data, offset, 1)
            if value is None:
                return offset
            add_field(fields, offset, 1, "u8", f"spawn[{index}].flag_{flag_index}", value)
            offset += 1

    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    count, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "u32_list_count", count)
    offset += consumed
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_list[{index}]", value)
        offset += 4

    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    count, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "order_count", count)
    offset += consumed
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"order[{index}].id", value)
        offset += 4
        packed = read_packed_u16(data, offset)
        if packed is None:
            return offset
        packed_value, consumed = packed
        add_field(fields, offset, consumed, "packed_u16", f"order[{index}].value", packed_value)
        offset += consumed
    return offset


def decode_map_fog_location_record(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", f"{prefix}.u32_0", value)
    offset = decode_string16_fields(data, start + 4, fields, (f"{prefix}.text",))
    if offset == start + 4:
        return offset
    for index in range(1, 5):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    for index in range(2):
        packed = read_packed_u16(data, offset)
        if packed is None:
            return offset
        value, consumed = packed
        add_field(fields, offset, consumed, "packed_u16", f"{prefix}.packed_{index}", value)
        offset += consumed
    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    blob_size, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", f"{prefix}.blob_size", blob_size)
    offset += consumed
    if offset + blob_size > len(data):
        return offset
    if blob_size:
        add_field(fields, offset, blob_size, "bytes", f"{prefix}.blob", f"{blob_size} bytes")
    return offset + blob_size


def decode_map_fog_map_subentry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", f"{prefix}.flag", value)
    offset = decode_string16_fields(data, start + 1, fields, (f"{prefix}.text",))
    if offset == start + 1:
        return offset
    for index in range(8):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    return offset


def decode_map_fog_map_record(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string16_fields(data, start, fields, (f"{prefix}.text_0", f"{prefix}.text_1"))
    if offset == start:
        return offset
    for index in range(8):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    value, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", f"{prefix}.packed_0", value)
    offset += consumed
    value = read_u(data, offset, 8)
    if value is None:
        return offset
    add_field(fields, offset, 8, "u64", f"{prefix}.u64_0", value)
    offset += 8
    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    count, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", f"{prefix}.subentry_count", count)
    offset += consumed
    for index in range(count):
        next_offset = decode_map_fog_map_subentry(data, offset, fields, f"{prefix}.subentry[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_map_fog_data_init(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    location_count = read_u(data, offset, 1)
    if location_count is None:
        return offset
    add_field(fields, offset, 1, "u8", "location_count", location_count)
    offset += 1
    for index in range(location_count):
        next_offset = decode_map_fog_location_record(data, offset, fields, f"location[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    map_count = read_u(data, offset, 1)
    if map_count is None:
        return offset
    add_field(fields, offset, 1, "u8", "map_count", map_count)
    offset += 1
    for index in range(map_count):
        next_offset = decode_map_fog_map_record(data, offset, fields, f"map[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_map_fog_data_update(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 1)
    if count is None:
        return start
    add_field(fields, start, 1, "u8", "location_count", count)
    offset = start + 1
    for index in range(count):
        next_offset = decode_map_fog_location_record(data, offset, fields, f"location[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_char_transfer_base(data: bytes, start: int, fields: list[dict[str, Any]], prefix: str) -> int:
    offset = start
    layout = (
        (8, "u64", "u64_0"),
        (4, "u32", "u32_0"),
        (4, "u32", "u32_1"),
        (4, "u32", "u32_2"),
        (4, "u32", "u32_3"),
        (8, "u64", "u64_1"),
        (1, "u8", "flag"),
        (8, "u64", "u64_2"),
    )
    for size, kind, name in layout:
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size
    return offset


def decode_char_transfer_common(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.text_0", text)
    offset += consumed
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_2", value)
    offset += 4

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.text_1", text)
    offset += consumed
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag_0", value)
    offset += 1

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.text_2", text)
    offset += consumed
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_3", value)
    offset += 4

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.text_3", text)
    offset += consumed
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag_1", value)
    offset += 1

    for index in range(4, 6):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4

    decoded = read_len_string(data, offset, 2)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u16", f"{prefix}.text_4", text)
    offset += consumed

    for index in range(6, 8):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index}", value)
        offset += 4
    return offset


def decode_u8_three_string16(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", "flag", value)
    return decode_string16_fields(data, start + 1, fields, ("text_0", "text_1", "text_2"))


def decode_three_string16_u64(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text_0", "text_1", "text_2"))
    if offset == start:
        return offset
    value = read_u(data, offset, 8)
    if value is not None:
        add_field(fields, offset, 8, "u64", "u64_0", value)
        offset += 8
    return offset


def decode_nested_raw0c_array(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    outer_count = read_u(data, start, 2)
    if outer_count is None:
        return start
    add_field(fields, start, 2, "u16", "outer_count", outer_count)
    offset = start + 2
    inner_counts: list[int] = []
    for index in range(outer_count):
        count = read_u(data, offset, 2)
        if count is None:
            return offset
        add_field(fields, offset, 2, "u16", f"outer[{index}].inner_count", count)
        inner_counts.append(count)
        offset += 2
    for outer_index, count in enumerate(inner_counts):
        size = count * 0x0C
        if offset + size > len(data):
            return offset
        if size:
            add_field(fields, offset, size, "bytes", f"outer[{outer_index}].entries_raw", f"{count} x 0x0c")
        offset += size
    return offset


def decode_char_transfer_blobs(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str, blob_count: int
) -> int:
    offset = start
    for index in range(blob_count):
        size = read_u(data, offset, 4)
        if size is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.blob[{index}].size", size)
        offset += 4
        if offset + size > len(data):
            return offset
        if size:
            add_field(fields, offset, size, "bytes", f"{prefix}.blob[{index}].data", f"{size} bytes")
        offset += size
    return offset


def decode_char_transfer_common_and_blobs(
    data: bytes,
    start: int,
    fields: list[dict[str, Any]],
    leading_flag: bool = False,
    trailing_flag: bool = False,
    blob_count: int = 0,
) -> int:
    offset = start
    if leading_flag:
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", "leading_flag", value)
        offset += 1
    common_start = offset
    offset = decode_char_transfer_common(data, offset, fields, "transfer")
    if offset == common_start:
        return offset
    if trailing_flag:
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", "trailing_flag", value)
        offset += 1
    if blob_count:
        offset = decode_char_transfer_blobs(data, offset, fields, "transfer", blob_count)
    return offset


def decode_char_transfer_envelope(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag", value)
    offset += 1
    for index in range(4):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    offset = decode_string16_fields(data, offset, fields, ("text",))
    return decode_char_transfer_common(data, offset, fields, "transfer")


def decode_get_character_serialized_reply(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"flag_{index}", value)
        offset += 1
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    offset = decode_char_transfer_common(data, offset + 4, fields, "transfer")
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_tail_{index}", value)
        offset += 4
    offset = decode_string16_fields(data, offset, fields, ("text_tail",))
    return decode_char_transfer_blobs(data, offset, fields, "serialized", 4)


def decode_create_char_from_cbb_request(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag", value)
    offset = decode_string16_fields(data, offset + 1, fields, ("text",))
    value = read_u(data, offset, 8)
    if value is None:
        return offset
    add_field(fields, offset, 8, "u64", "u64_0", value)
    offset += 8
    for index in range(3):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    offset = decode_char_transfer_common(data, offset, fields, "transfer")
    return decode_char_transfer_blobs(data, offset, fields, "cbb", 4)


def decode_char_transfer_validation(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"flag_{index}", value)
        offset += 1
    for size, kind, name in ((4, "u32", "u32_0"), (8, "u64", "u64_0"), (4, "u32", "u32_1"), (4, "u32", "u32_2")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return decode_char_transfer_common(data, offset, fields, "transfer")


def decode_string16_list_u32_count(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "string_count", count)
    offset = start + 4
    for index in range(count):
        decoded = read_len_string(data, offset, 2)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u16", f"string[{index}]", text)
        offset += consumed
    return offset


def decode_housing_restore(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_list_u32_count(data, start, fields)
    if offset == start:
        return offset
    offset = decode_string16_fields(data, offset, fields, ("house_name",))
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    size = read_u(data, offset, 4)
    if size is None:
        return offset
    add_field(fields, offset, 4, "u32", "blob_size", size)
    offset += 4
    if offset + size > len(data):
        return offset
    if size:
        add_field(fields, offset, size, "bytes", "blob", f"{size} bytes")
    offset += size
    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", "trailing_flag", value)
        offset += 1
    return offset


def decode_u32_u32_u64_u32_string16(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for size, kind, name in (
        (4, "u32", "u32_0"),
        (4, "u32", "u32_1"),
        (8, "u64", "u64_0"),
        (4, "u32", "u32_2"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return decode_string16_fields(data, offset, fields, ("text",))


def decode_u32_u32_u64_string16(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for size, kind, name in ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (8, "u64", "u64_0")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return decode_string16_fields(data, offset, fields, ("text",))


def decode_u32_u64_u32_u32_string3(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for size, kind, name in (
        (4, "u32", "u32_0"),
        (8, "u64", "u64_0"),
        (4, "u32", "u32_1"),
        (4, "u32", "u32_2"),
    ):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    decoded = read_len_string(data, offset, 3)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u24", "text", text)
    return offset + consumed


def decode_u32_u64_string16(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for size, kind, name in ((4, "u32", "u32_0"), (8, "u64", "u64_0")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    return decode_string16_fields(data, offset, fields, ("text",))


def decode_u32_string8(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    offset = start + 4
    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", "text", text)
    return offset + consumed


def decode_string8_field(
    data: bytes, start: int, fields: list[dict[str, Any]], name: str
) -> int:
    decoded = read_len_string(data, start, 1)
    if decoded is None:
        return start
    text, _text_size, consumed = decoded
    add_field(fields, start, consumed, "string_u8", name, text)
    return start + consumed


def decode_packed_u16_field(
    data: bytes, start: int, fields: list[dict[str, Any]], name: str
) -> int:
    packed = read_packed_u16(data, start)
    if packed is None:
        return start
    value, consumed = packed
    add_field(fields, start, consumed, "packed_u16", name, value)
    return start + consumed


def decode_eqcmd_packed_string8_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string8_field(data, start, fields, f"{prefix}.text")
    if offset == start:
        return start
    for index in range(4):
        next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_{index}")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_eqcmd_string16_string8_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string16_fields(data, start, fields, (f"{prefix}.text16",))
    if offset == start:
        return start
    next_offset = decode_string8_field(data, offset, fields, f"{prefix}.text8")
    return next_offset


def decode_eqcmd_string8_string16_flag_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string8_field(data, start, fields, f"{prefix}.text8")
    if offset == start:
        return start
    next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text16",))
    if next_offset == offset:
        return offset
    offset = next_offset
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag", value)
    return offset + 1


def decode_eqcmd_nested_string_list(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    for index in range(3):
        next_offset = decode_string8_field(data, offset, fields, f"{prefix}.text8_{index}")
        if next_offset == offset:
            return offset
        offset = next_offset
    next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text16",))
    if next_offset == offset:
        return offset
    offset = next_offset
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.packed_entry_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_packed_string8_entry(
            data, offset, fields, f"{prefix}.packed_entry[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_eqcmd_grouped_list_header(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", f"{prefix}.flag_0", value)
    offset = start + 1
    for group_index in range(3):
        count = read_u(data, offset, 1)
        if count is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.group[{group_index}].count", count)
        offset += 1
        for entry_index in range(count):
            next_offset = decode_eqcmd_string16_string8_entry(
                data, offset, fields, f"{prefix}.group[{group_index}].entry[{entry_index}]"
            )
            if next_offset == offset:
                return offset
            offset = next_offset
    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", f"{prefix}.flag_1", value)
        offset += 1
    return offset


def decode_eqcmd_grouped_complex_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.flag_{index}", value)
        offset += 1
    next_offset = decode_string8_field(data, offset, fields, f"{prefix}.text8")
    if next_offset == offset:
        return offset
    offset = next_offset
    next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text16",))
    if next_offset == offset:
        return offset
    offset = next_offset
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.string_entry_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_string16_string8_entry(
            data, offset, fields, f"{prefix}.string_entry[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag_2", value)
    offset += 1
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.nested_list_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_nested_string_list(
            data, offset, fields, f"{prefix}.nested_list[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_eqcmd_grouped_lists(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_eqcmd_grouped_list_header(data, start, fields, "header")
    if offset == start:
        return start
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "grouped_entry_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_grouped_complex_entry(data, offset, fields, f"entry[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_eqcmd_short_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", f"{prefix}.u32_0", value)
    offset = start + 4
    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", f"{prefix}.text", text)
    offset += consumed
    for index in range(4):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.flag_{index}", value)
        offset += 1
    return offset


def decode_eqcmd_visual_state(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    for index in range(2):
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", f"{prefix}.text_{index}", text)
        offset += consumed
    for index in range(8):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.flag_{index}", value)
        offset += 1
    return offset


def decode_eqcmd_short_entry_list(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    offset = start + 4
    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    count, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "entry_count", count)
    offset += consumed
    for index in range(count):
        next_offset = decode_eqcmd_short_entry(data, offset, fields, f"entry[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_eqcmd_action_union(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    action = read_u(data, start, 1)
    if action is None:
        return start
    add_field(fields, start, 1, "u8", "action", action)
    offset = start + 1
    if action == 0:
        return decode_eqcmd_short_entry(data, offset, fields, "payload")
    if action == 1:
        value = read_u(data, offset, 4)
        if value is not None:
            add_field(fields, offset, 4, "u32", "payload_u32", value)
            offset += 4
        return offset
    if action == 2:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "payload_u32", value)
        offset += 4
        value = read_u(data, offset, 1)
        if value is not None:
            add_field(fields, offset, 1, "u8", "payload_flag", value)
            offset += 1
    return offset


def decode_eqcmd_two_u32_visual_state(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return decode_eqcmd_visual_state(data, offset, fields, "visual")


def decode_u32_string8_list(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    offset = start + 4
    count = read_u(data, offset, 2)
    if count is None:
        return offset
    add_field(fields, offset, 2, "u16", "string_count", count)
    offset += 2
    for index in range(count):
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", f"string[{index}]", text)
        offset += consumed
    return offset


def decode_string16_u32_visual_state(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = decode_string16_fields(data, start, fields, ("text",))
    if offset == start:
        return offset
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    return decode_eqcmd_visual_state(data, offset + 4, fields, "visual")


def decode_u32_visual_state(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    return decode_eqcmd_visual_state(data, start + 4, fields, "visual")


def decode_eqcmd_action_switch(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    action = read_u(data, offset, 1)
    if action is None:
        return offset
    add_field(fields, offset, 1, "u8", "action", action)
    offset += 1
    if action in {1, 2, 6, 8, 9}:
        value = read_u(data, offset, 4)
        if value is not None:
            add_field(fields, offset, 4, "u32", "case_u32", value)
            offset += 4
    elif action in {0, 4}:
        offset, _complete = decode_eqcmd_widget_record(data, offset, fields, "case_widget")
    elif action in {3, 5}:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "case_u32", value)
        offset += 4
        value = read_u(data, offset, 1)
        if value is not None:
            add_field(fields, offset, 1, "u8", "case_flag", value)
            offset += 1
    elif action == 7:
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", "case_flag", value)
        offset += 1
        value = read_u(data, offset, 2)
        if value is not None:
            add_field(fields, offset, 2, "u16", "case_u16", value)
            offset += 2
    return offset


def decode_eqcmd_widget_payload(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> tuple[int, bool]:
    value = read_u(data, start, 4)
    if value is None:
        return start, False
    add_field(fields, start, 4, "u32", f"{prefix}.u32_0", value)
    offset = start + 4
    next_offset = decode_string8_field(data, offset, fields, f"{prefix}.text8")
    if next_offset == offset:
        return offset, False
    offset = next_offset
    next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text16",))
    if next_offset == offset:
        return offset, False
    offset = next_offset
    next_offset = decode_eqcmd_packed_string8_entry(data, offset, fields, f"{prefix}.packed_entry")
    if next_offset == offset:
        return offset, False
    offset = next_offset
    count = read_u(data, offset, 1)
    if count is None:
        return offset, False
    add_field(fields, offset, 1, "u8", f"{prefix}.subrecord_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_widget_subrecord(data, offset, fields, f"{prefix}.subrecord[{index}]")
        if next_offset == offset:
            return offset, False
        offset = next_offset
    count = read_u(data, offset, 1)
    if count is None:
        return offset, False
    add_field(fields, offset, 1, "u8", f"{prefix}.string_flag_entry_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_string8_string16_flag_entry(
            data, offset, fields, f"{prefix}.string_flag_entry[{index}]"
        )
        if next_offset == offset:
            return offset, False
        offset = next_offset
    for index in range(3):
        if offset + 3 > len(data):
            return offset, False
        add_field(
            fields,
            offset,
            3,
            "bytes",
            f"{prefix}.color_block_{index}",
            data[offset : offset + 3].hex(),
        )
        offset += 3
    return offset, True


def decode_eqcmd_widget_record(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> tuple[int, bool]:
    value = read_u(data, start, 4)
    if value is None:
        return start, False
    add_field(fields, start, 4, "u32", f"{prefix}.u32_0", value)
    offset = start + 4
    next_offset = decode_string8_field(data, offset, fields, f"{prefix}.text8")
    if next_offset == offset:
        return offset, False
    offset = next_offset
    offset, complete = decode_eqcmd_widget_payload(data, offset, fields, f"{prefix}.payload")
    if not complete:
        return offset, False
    for index in range(3):
        value = read_u(data, offset, 1)
        if value is None:
            return offset, False
        add_field(fields, offset, 1, "u8", f"{prefix}.flag_{index}", value)
        offset += 1
    return offset, True


def decode_eqcmd_widget_subrecord_small(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = start
    for size, kind, name in ((1, "u8", "u8_0"), (1, "u8", "u8_1"), (2, "u16", "u16_0")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
        offset += size
    return offset


def decode_eqcmd_widget_subrecord_string8_flag(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string8_field(data, start, fields, f"{prefix}.text8")
    if offset == start:
        return start
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag", value)
    return offset + 1


def decode_eqcmd_widget_subrecord_string16_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    value = read_u(data, start, 1)
    if value is None:
        return start
    add_field(fields, start, 1, "u8", f"{prefix}.flag_0", value)
    offset = decode_string16_fields(data, start + 1, fields, (f"{prefix}.text16",))
    if offset == start + 1:
        return start + 1
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag_1", value)
    return offset + 1


def decode_eqcmd_widget_subrecord(
    data: bytes,
    start: int,
    fields: list[dict[str, Any]],
    prefix: str,
    *,
    include_optional_sections: bool = False,
) -> int:
    offset = start
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
    offset += 4
    for index in range(5):
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"{prefix}.u16_{index}", value)
        offset += 2
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag_0", value)
    offset += 1
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index + 1}", value)
        offset += 4
    next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_0")
    if next_offset == offset:
        return offset
    offset = next_offset
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_3", value)
    offset += 4
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.small_record_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_widget_subrecord_small(
            data, offset, fields, f"{prefix}.small_record[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag_1", value)
    offset += 1
    for index in range(4):
        next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_{index + 1}")
        if next_offset == offset:
            return offset
        offset = next_offset
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag_2", value)
    offset += 1
    if include_optional_sections:
        for index in range(2):
            next_offset = decode_packed_u16_field(
                data, offset, fields, f"{prefix}.optional_packed_{index}"
            )
            if next_offset == offset:
                return offset
            offset = next_offset
        for index in range(2):
            value = read_u(data, offset, 4)
            if value is None:
                return offset
            add_field(fields, offset, 4, "u32", f"{prefix}.optional_u32_{index}", value)
            offset += 4
        value = read_u(data, offset, 2)
        if value is None:
            return offset
        add_field(fields, offset, 2, "u16", f"{prefix}.optional_u16", value)
        offset += 2
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag_3", value)
    offset += 1
    if include_optional_sections:
        count = read_u(data, offset, 1)
        if count is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.string8_flag_count", count)
        offset += 1
        for index in range(count):
            next_offset = decode_eqcmd_widget_subrecord_string8_flag(
                data, offset, fields, f"{prefix}.string8_flag[{index}]"
            )
            if next_offset == offset:
                return offset
            offset = next_offset
        count = read_u(data, offset, 1)
        if count is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.string16_entry_count", count)
        offset += 1
        for index in range(count):
            next_offset = decode_eqcmd_widget_subrecord_string16_entry(
                data, offset, fields, f"{prefix}.string16_entry[{index}]"
            )
            if next_offset == offset:
                return offset
            offset = next_offset
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag_4", value)
    offset += 1
    for index in range(4):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_{index + 4}", value)
        offset += 4
    for index in range(6):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.flag_{index + 5}", value)
        offset += 1
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_8", value)
    offset += 4
    next_offset = decode_string8_field(data, offset, fields, f"{prefix}.tail_text8")
    if next_offset == offset:
        return offset
    offset = next_offset
    next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.tail_text16",))
    return next_offset


def decode_eqcmd_complex_visual_map_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    offset = decode_string8_field(data, start, fields, f"{prefix}.text8")
    if offset == start:
        return start
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
    offset += 4
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.flag", value)
    offset += 1
    next_offset = decode_packed_u16_field(data, offset, fields, f"{prefix}.packed_0")
    if next_offset == offset:
        return offset
    offset = next_offset
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", f"{prefix}.id_count", count)
    offset += 1
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.id[{index}]", value)
        offset += 4
    return offset


def decode_eqcmd_complex_visual_list(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    offset = decode_eqcmd_visual_state(data, start + 4, fields, "visual")
    for size, kind, name in ((4, "u32", "u32_1"), (1, "u8", "flag_0"), (4, "u32", "u32_2")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "map_entry_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_eqcmd_complex_visual_map_entry(
            data, offset, fields, f"map_entry[{index}]"
        )
        if next_offset == offset:
            return offset
        offset = next_offset
    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    count, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "id_count", count)
    offset += consumed
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"id[{index}]", value)
        offset += 4
    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    count, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "widget_record_count", count)
    offset += consumed
    for index in range(count):
        offset, complete = decode_eqcmd_widget_record(data, offset, fields, f"widget_record[{index}]")
        if not complete:
            return offset
    return offset


def decode_eqcmd_widget_envelope(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(3):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    next_offset = decode_string8_field(data, offset, fields, "text8")
    if next_offset == offset:
        return offset
    offset = next_offset
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag", value)
    offset += 1
    offset, complete = decode_eqcmd_widget_payload(data, offset, fields, "widget")
    if not complete:
        return offset
    return decode_packed_u16_field(data, offset, fields, "tail_packed")


def decode_eqcmd_widget_envelope2(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    for index in range(2):
        next_offset = decode_string8_field(data, offset, fields, f"text8_{index}")
        if next_offset == offset:
            return offset
        offset = next_offset
    offset, complete = decode_eqcmd_widget_payload(data, offset, fields, "widget")
    if not complete:
        return offset
    return decode_packed_u16_field(data, offset, fields, "tail_packed")


def decode_eqcmd_u32_string8_entry(
    data: bytes, start: int, fields: list[dict[str, Any]], prefix: str
) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", f"{prefix}.u32_0", value)
    offset = start + 4
    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", f"{prefix}.text", text)
    return offset + consumed


def decode_eqcmd_u32_string8_list(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 1)
    if count is None:
        return start
    add_field(fields, start, 1, "u8", "entry_count", count)
    offset = start + 1
    for index in range(count):
        next_offset = decode_eqcmd_u32_string8_entry(data, offset, fields, f"entry[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_three_u32(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    for index in range(3):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    return offset


def decode_u32_two_string16(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    value = read_u(data, start, 4)
    if value is None:
        return start
    add_field(fields, start, 4, "u32", "u32_0", value)
    return decode_string16_fields(data, start + 4, fields, ("text_0", "text_1"))


def decode_type401_mixed_string_record(
    data: bytes, start: int, fields: list[dict[str, Any]], *, include_tail: bool = False
) -> int:
    offset = start
    for size, kind, name in ((1, "u8", "flag_0"), (4, "u32", "u32_0")):
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    offset = decode_string16_fields(data, offset, fields, ("text_0", "text_1"))
    for size, kind, name in (
        (4, "u32", "u32_1"),
        (4, "u32", "u32_2"),
        (4, "u32", "u32_3"),
        (1, "u8", "flag_1"),
    ):
        if name == "u32_2":
            offset = decode_string16_fields(data, offset, fields, ("text_2",))
        value = read_u(data, offset, size)
        if value is None:
            return offset
        add_field(fields, offset, size, kind, name, value)
        offset += size
    if include_tail:
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", "tail_flag", value)
        offset = decode_string16_fields(data, offset + 1, fields, ("tail_text",))
    return offset


def decode_type403_mixed_scalar_string_record(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = start
    for index in range(2):
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"flag_{index}", value)
        offset += 1
    for index in range(2):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    offset = decode_string16_fields(data, offset, fields, ("text_0",))
    for index in range(2, 7):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"u32_{index}", value)
        offset += 4
    value = read_u(data, offset, 1)
    if value is None:
        return offset
    add_field(fields, offset, 1, "u8", "flag_2", value)
    return decode_string16_fields(data, offset + 1, fields, ("text_1",))


def decode_string8_u32_u8(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    offset = start
    decoded = read_len_string(data, offset, 1)
    if decoded is None:
        return offset
    text, _text_size, consumed = decoded
    add_field(fields, offset, consumed, "string_u8", "text", text)
    offset += consumed
    value = read_u(data, offset, 4)
    if value is None:
        return offset
    add_field(fields, offset, 4, "u32", "u32_0", value)
    offset += 4
    value = read_u(data, offset, 1)
    if value is not None:
        add_field(fields, offset, 1, "u8", "flag", value)
        offset += 1
    return offset


def decode_dispatch_msg(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    method = read_u(data, start, 1)
    if method is None:
        return start
    add_field(fields, start, 1, "u8", "method", method)
    offset = start + 1
    if method in {1, 3, 5, 8, 10}:
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", "method_text", text)
        offset += consumed
    elif method in {2, 6, 7, 9}:
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "method_u32", value)
        offset += 4
    for index in range(2):
        decoded = read_len_string(data, offset, 1)
        if decoded is None:
            return offset
        text, _text_size, consumed = decoded
        add_field(fields, offset, consumed, "string_u8", f"text_{index}", text)
        offset += consumed
    packed = read_packed_u16(data, offset)
    if packed is None:
        return offset
    size, consumed = packed
    add_field(fields, offset, consumed, "packed_u16", "raw_size", size)
    offset += consumed
    if offset + size <= len(data):
        add_field(fields, offset, size, "bytes", "raw_payload", data[offset : offset + size].hex())
        offset += size
    return offset


def decode_conditional_string8_list_prefix(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    return decode_string16_fields(data, start, fields, ("text_0",))


def decode_conditional_string8_list_default(
    data: bytes, start: int, fields: list[dict[str, Any]]
) -> int:
    offset = decode_string16_fields(data, start, fields, ("text_0",))
    if offset == start:
        return start
    mode = read_u(data, offset, 1)
    if mode is None:
        return offset
    add_field(fields, offset, 1, "u8", "mode", mode)
    offset += 1
    if mode != 0xFF:
        for index in range(2):
            next_offset = decode_string8_field(data, offset, fields, f"mode_text_{index}")
            if next_offset == offset:
                return offset
            offset = next_offset
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", "mode_u32", value)
        offset += 4
    count = read_u(data, offset, 1)
    if count is None:
        return offset
    add_field(fields, offset, 1, "u8", "string_count", count)
    offset += 1
    for index in range(count):
        next_offset = decode_string8_field(data, offset, fields, f"string[{index}]")
        if next_offset == offset:
            return offset
        offset = next_offset
    return offset


def decode_house_items_details(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 4)
    if count is None:
        return start
    add_field(fields, start, 4, "u32", "item_count", count)
    offset = start + 4
    for index in range(count):
        prefix = f"item[{index}]"
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_0", value)
        offset += 4
        next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_0",))
        if next_offset == offset:
            return offset
        offset = next_offset
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"{prefix}.u32_1", value)
        offset += 4
        value = read_u(data, offset, 1)
        if value is None:
            return offset
        add_field(fields, offset, 1, "u8", f"{prefix}.flag_0", value)
        offset += 1
        next_offset = decode_string16_fields(data, offset, fields, (f"{prefix}.text_1",))
        if next_offset == offset:
            return offset
        offset = next_offset
        for size, kind, name in ((2, "u16", "u16_0"), (1, "u8", "flag_1")):
            value = read_u(data, offset, size)
            if value is None:
                return offset
            add_field(fields, offset, size, kind, f"{prefix}.{name}", value)
            offset += size
    return offset


def decode_u8_count_u32_id_list(data: bytes, start: int, fields: list[dict[str, Any]]) -> int:
    count = read_u(data, start, 1)
    if count is None:
        return start
    add_field(fields, start, 1, "u8", "id_count", count)
    offset = start + 1
    for index in range(count):
        value = read_u(data, offset, 4)
        if value is None:
            return offset
        add_field(fields, offset, 4, "u32", f"id[{index}]", value)
        offset += 4
    return offset


def decode_compressed_blob(
    packet: Packet, data: bytes, start: int, fields: list[dict[str, Any]], payload_dir: Path
) -> tuple[int, dict[str, Any] | None]:
    compressed_size = read_u(data, start, 4)
    decompressed_size = read_u(data, start + 4, 4)
    if compressed_size is None or decompressed_size is None:
        return start, None

    blob_offset = start + 8
    blob_end = blob_offset + compressed_size
    if blob_end > len(data):
        add_field(fields, start, 4, "u32", "compressed_size_claimed", compressed_size)
        add_field(fields, start + 4, 4, "u32", "decompressed_size_claimed", decompressed_size)
        return blob_offset, None

    blob = data[blob_offset:blob_end]
    add_field(fields, start, 4, "u32", "compressed_size", compressed_size)
    add_field(fields, start + 4, 4, "u32", "decompressed_size", decompressed_size)
    add_field(fields, blob_offset, compressed_size, "zlib", "compressed_payload", f"{compressed_size} bytes")

    decoded: dict[str, Any] = {
        "compressed_size": compressed_size,
        "decompressed_size_claimed": decompressed_size,
        "zlib_ok": False,
    }
    try:
        inflated = zlib.decompress(blob)
    except zlib.error as exc:
        decoded["zlib_error"] = str(exc)
        return blob_end, decoded

    decoded["zlib_ok"] = True
    decoded["decompressed_size_actual"] = len(inflated)
    decoded["size_matches"] = len(inflated) == decompressed_size
    decoded["printable_ratio"] = round(printable_ratio(inflated), 4)
    decoded["preview"] = text_preview(inflated)

    payload_dir.mkdir(parents=True, exist_ok=True)
    stem = f"session{packet.session:02d}_seq{packet.seq:04d}_{packet.direction}_type{packet.type_id}"
    bin_path = payload_dir / f"{stem}.bin"
    bin_path.write_bytes(inflated)
    decoded["bin_path"] = str(bin_path)
    if printable_ratio(inflated) > 0.85:
        txt_path = payload_dir / f"{stem}.txt"
        txt_path.write_text(inflated.decode("utf-8", errors="replace"), encoding="utf-8")
        decoded["txt_path"] = str(txt_path)

    return blob_end, decoded


def text_preview(data: bytes, max_lines: int = 12) -> list[str]:
    text = data.decode("utf-8", errors="replace")
    lines = text.splitlines()
    if not lines:
        return [text[:240]]
    return lines[:max_lines]


def trace_fields(packet: Packet, data: bytes) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    seen: set[tuple[str, int, int]] = set()
    for record in packet.field_events:
        offset = record.get("offset")
        size = record.get("size")
        event = record.get("event")
        if not isinstance(offset, int) or not isinstance(size, int):
            continue
        if offset < 0 or size < 1 or offset + size > len(data):
            continue
        key = (str(event), offset, size)
        if key in seen:
            continue
        seen.add(key)
        raw = data[offset : offset + size]
        row: dict[str, Any] = {
            "event": event,
            "offset": offset,
            "size": size,
            "hex": raw.hex(),
        }
        if size > 64:
            row["summary"] = f"{size} bytes"
        elif size in {1, 2, 4, 8}:
            row["u"] = int.from_bytes(raw, "little", signed=False)
            row["i"] = int.from_bytes(raw, "little", signed=True)
        if size <= 64 and printable_ratio(raw) > 0.85 and any(32 <= byte < 127 for byte in raw):
            row["ascii"] = raw.decode("utf-8", errors="replace")
        rows.append(row)
    return sorted(rows, key=lambda row: (row["offset"], row["size"], row["event"]))


def decode_packet(
    packet: Packet,
    payload_dir: Path,
    opcode_names: dict[int, str],
    structs_by_opcode: dict[int, list[dict[str, Any]]],
    structs_by_name: dict[str, dict[str, Any]],
) -> dict[str, Any] | None:
    if packet.body_hex is None:
        return None
    data = bytes.fromhex(packet.body_hex)
    type_info = read_packed_u16(data, 0)
    fields: list[dict[str, Any]] = []
    notes: list[str] = []
    compressed: dict[str, Any] | None = None

    if type_info is None:
        return None

    type_id, type_size = type_info
    add_field(fields, 0, type_size, "packed_u16", "type_id", type_id)
    offset = type_size

    if packet.type_id is None:
        packet.type_id = type_id
    opcode_name = CLIENT_DERIVED_OPCODE_NAMES.get(type_id, opcode_names.get(type_id))

    struct_meta: dict[str, Any] | None = None

    if type_id == 0:
        offset = decode_type_0(data, offset, fields)
        notes.append("PacketParser opcode name: OP_LoginRequestMsg")
    elif type_id == 1:
        offset = decode_u32_u32_u16_raw20(data, offset, fields)
        notes.append("client-derived LoginByNumRequest registry layout; differs from older PacketParser struct")
    elif type_id == 2:
        offset = decode_type_2(data, offset, fields)
        notes.append("client-derived WSLoginRequest layout")
    elif type_id == 3:
        offset = decode_type_3(data, offset, fields)
        notes.append("client-derived ESLoginRequest layout")
    elif type_id == 4:
        offset = decode_type_4(data, offset, fields)
        notes.append("handler-backed LoginReply layout; PacketParser tail bytes are ignored by this client")
        reply_code = next(
            (field.get("value") for field in fields if field.get("name") == "reply_code"),
            None,
        )
        if isinstance(reply_code, int):
            notes.append(
                f"login reply code {reply_code}: "
                f"{LOGIN_REPLY_CODE_MEANINGS.get(reply_code, 'unknown login rejection/response')}"
            )
    elif type_id == 5:
        offset = decode_type_5(data, offset, fields)
        notes.append("client-derived WSStatusReply fixed-field layout")
    elif type_id == 6:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (4, "u32", "u32_0"),
                (1, "u8", "flag_0"),
                (1, "u8", "flag_1"),
                (1, "u8", "flag_2"),
                (1, "u8", "flag_3"),
            ),
        )
        notes.append("handler-backed WorldStatusChange u32/four-u8 layout; GameScene no-ops but character-select updates cached world status")
    elif type_id == 7:
        notes.append("client-derived AllWSDescRequest empty payload")
    elif type_id == 11:
        offset = decode_create_character_request(data, offset, fields)
        notes.append("client-derived CreateCharacterRequest layout with DoF v4 customization profile")
    elif type_id == 12:
        offset = decode_u32_u8_string16(data, offset, fields)
        notes.append("handler-backed CreateCharacterReply shared u32/u8/string16 layout")
        response_code = next((field.get("value") for field in fields if field.get("name") == "flag"), None)
        if isinstance(response_code, int):
            notes.append(
                f"create-character reply code {response_code}: "
                f"{CREATE_CHARACTER_REPLY_CODE_MEANINGS.get(response_code, 'unknown create-character response')}"
            )
    elif type_id == 13:
        offset = decode_ws_create_character_request(data, offset, fields)
        notes.append("client-derived WSCreateCharacterRequest wrapper layout")
    elif type_id == 14:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (1, "u8", "response")))
        offset = decode_string16_fields(data, offset, fields, ("character_name",))
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_1"), (1, "u8", "trailing_flag")))
        notes.append("client-derived WSCreateCharacterReply u32/u8/string16/u32/u8 layout")
    elif type_id == 15:
        offset = decode_reskin_character_request(data, offset, fields)
        notes.append("client-derived ReskinCharacterRequest layout with DoF v4 customization profile")
    elif type_id == 16:
        offset = decode_three_u32_string16(data, offset, fields)
        notes.append("client-derived DeleteCharacterRequest three-u32/string16 layout")
    elif type_id == 17:
        offset = decode_scalar_layout(data, offset, fields, ((1, "u8", "response"), (4, "u32", "server_id"), (4, "u32", "character_id")))
        offset = decode_string16_fields(data, offset, fields, ("character_name",))
        notes.append("handler-backed DeleteCharacterReply u8/u32/u32/string16 layout")
        response_code = next((field.get("value") for field in fields if field.get("name") == "response"), None)
        if isinstance(response_code, int):
            notes.append(
                f"delete-character reply code {response_code}: "
                f"{DELETE_CHARACTER_REPLY_CODE_MEANINGS.get(response_code, 'generic delete failure')}"
            )
    elif type_id == 18:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "character_id"), (4, "u32", "server_id"), (1, "u8", "flag")))
        offset = decode_string16_fields(data, offset, fields, ("character_name",))
        notes.append("client-derived PlayCharacterRequest u32/u32/u8/string16 layout")
    elif type_id == 19:
        offset = decode_play_character_reply(data, offset, fields, include_u32_before_success=False)
        notes.append("handler-backed PlayCharacterReply conditional success layout")
        response_code = next((field.get("value") for field in fields if field.get("name") == "response"), None)
        if isinstance(response_code, int):
            notes.append(
                f"play-character reply code {response_code}: "
                f"{PLAY_CHARACTER_REPLY_CODE_MEANINGS.get(response_code, 'unknown play-character response')}"
            )
    elif type_id == 20:
        offset = decode_server_play_character_request(data, offset, fields)
        notes.append("client-derived ServerPlayCharacterRequest layout; counted u32 list")
    elif type_id == 21:
        offset = decode_play_character_reply(data, offset, fields, include_u32_before_success=True)
        notes.append("client-derived ServerPlayCharacterReply conditional success layout")
    elif type_id == 22:
        offset = decode_es_init(data, offset, fields)
        notes.append("client-derived VeESInit layout; nonzero guild payloads remain raw until that nested type is mapped")
    elif type_id == 23:
        notes.append("client-derived ESReadyForClients empty payload")
    elif type_id == 24:
        offset = decode_five_string16_four_u32(data, offset, fields)
        notes.append("client-derived CreateZoneInstance five-string16/four-u32 layout")
    elif type_id == 25:
        offset = decode_string16_u8(data, offset, fields)
        notes.append("client-derived ZoneInstanceCreateReply string16/u8 layout")
    elif type_id == 26:
        offset = decode_string16_fields(data, offset, fields, ("text",))
        notes.append("client-derived ZoneInstanceDestroyed single-string16 layout")
    elif type_id == 27:
        offset = decode_expect_client_as_character_request(data, offset, fields)
        notes.append("client-derived ExpectClientAsCharacterRequest layout; includes optional four-u32 block")
    elif type_id == 28:
        offset = decode_type_28(data, offset, fields)
        notes.append("client-derived ExpectClientAsCharacterReply candidate; PacketParser v546 names drift in this range")
    elif type_id == 29:
        offset = decode_zone_info(data, offset, fields)
        notes.append("handler-backed ZoneInfo layout with slideshow block and counted string16/u16 list")
    elif type_id == 30:
        offset = decode_type_30(data, offset, fields)
        notes.append("client-derived counted u16 list layout; PacketParser v546 names drift in this range")
    elif type_id in {31, 32, 33}:
        if type_id == 31:
            notes.append("handler-backed DoneSendingInitialEntities empty payload; GameScene logs the initial-entities completion")
        elif type_id == 33:
            notes.append("handler-backed DoneLoadingUIResources empty payload; GameScene inline case sets the UI/resource-loaded ack flag")
        else:
            notes.append("client-to-server loading-state empty payload; client sends it after logging done loading entity resources")
    elif type_id == 34:
        offset = decode_prediction_buffer(data, offset, fields)
        notes.append("client-derived VePredictionUpdate nested blob envelope; blob size is capped below 0x401")
    elif type_id == 35:
        offset = decode_set_remote_cmds(data, offset, fields)
        notes.append("handler-backed SetRemoteCmds layout; two u16-counted string8-style command lists")
    elif type_id == 8:
        offset = decode_type_8(data, offset, fields)
        notes.append("handler-backed WorldList layout; login autologin and character-select list paths consume 0x44-byte records")
    elif type_id == 9:
        notes.append("client-derived AllCharactersDescRequest empty payload")
    elif type_id == 10:
        offset = decode_type_10(data, offset, fields)
        notes.append("handler-backed AllCharactersDescReply layout; login autologin and character-select UI paths consume 0x1cc-byte records")
    elif type_id == 36:
        offset = decode_remote_cmd(data, offset, fields)
        notes.append("client-derived RemoteCmd layout; PacketParser v546 names drift in this range")
    elif type_id == 37:
        offset = decode_game_world_time(data, offset, fields)
        notes.append("handler-backed GameWorldTime layout; PacketParser v546 names drift in this range")
    elif type_id == 38:
        offset = decode_string16_fields(data, offset, fields, ("text",))
        notes.append("client-derived single-string16 layout; exact MOTD-style handler not yet confirmed")
    elif type_id == 39:
        offset = decode_string16_fields(data, offset, fields, ("text_0", "text_1"))
        notes.append("client-derived two-string16 layout; exact MOTD-style handler not yet confirmed")
    elif type_id == 40:
        offset = decode_type_40(data, offset, fields)
        notes.append("client-derived fixed-field layout; exact opcode name still unresolved")
    elif type_id == 41:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (1, "u8", "flag")))
        notes.append("client-derived MOTD/avatar-adjacent u32/u8 layout; not a ZoneMOTD string payload")
    elif type_id == 42:
        offset = decode_request_camp(data, offset, fields)
        notes.append("outbound RequestCamp layout; client builder parses the optional desktop argument")
    elif type_id == 43:
        offset = decode_request_camp(data, offset, fields, "seconds_or_state")
        notes.append("handler-backed CampStarted layout; GameScene treats incoming row as camp-start state")
    elif type_id == 44:
        notes.append("handler-backed CampAborted empty payload; GameScene handler prints the camp-preparation abort text")
    elif type_id == 45:
        offset = decode_state_blob(data, offset, fields)
        notes.append(
            "client-derived environment-map state layout; gated string and optional u32 are flag-controlled; "
            "not the legacy PacketParser OP_MapRequest opcode 999"
        )
    elif type_id == 46:
        offset = decode_who_query_reply(data, offset, fields)
        notes.append("handler-backed WhoQueryReply layout; PacketParser v546 names drift in this range")
    elif type_id == 47:
        offset = decode_fixed_u32_and_raw(
            data, offset, fields, ("monitor_blocks_raw", 0x18, "two 0x0c-byte monitor blocks")
        )
        notes.append("client-derived MonitorReply candidate; PacketParser v546 names drift in this range")
    elif type_id == 48:
        offset = decode_fixed4c_array(data, offset, fields)
        notes.append("client-derived MonitorCharacterList candidate; PacketParser v546 names drift in this range")
    elif type_id == 49:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived MonitorCharacterListRequest-adjacent single-u32 layout")
    elif type_id == 50:
        offset = decode_client_cmd_blob(data, offset, fields)
        notes.append(
            "handler-backed ClientCmd blob containing nested VeType client commands; "
            "decoder previews the first nested VeType tag and decodes known compact nested bodies"
        )
    elif type_id == 51:
        offset = decode_dispatch_client_cmd(data, offset, fields)
        notes.append("client-derived DispatchClientCmd envelope; PacketParser v546 names drift in this range")
    elif type_id == 52:
        offset = decode_dispatch_es(data, offset, fields)
        notes.append("client-derived DispatchES envelope; PacketParser v546 names drift in this range")
    elif type_id == 53:
        value = read_u(data, offset, 2)
        if value is not None:
            add_field(fields, offset, 2, "u16", "spawn_index", value)
            offset += 2
        notes.append("client-derived small UpdateTarget candidate; PacketParser v546 names drift in this range")
    elif type_id == 54:
        offset = decode_three_floats(data, offset, fields, ("x", "y", "z"))
        notes.append("client-derived UpdateTargetLoc candidate; PacketParser v546 names drift in this range")
    elif type_id == 55:
        offset = decode_character_sheet_update(data, offset, fields)
        notes.append(
            "handler-backed CharacterSheet packed full sheet or delta; v546 unpacked size is 4897 bytes"
        )
    elif type_id == 56:
        offset = decode_spell_book_update(data, offset, fields)
        notes.append(
            "handler-backed SpellBook packed delta; v546 entries are 27-byte SubStruct_UpdateSpellBook records"
        )
    elif type_id == 58:
        offset = decode_inventory_update(data, offset, fields)
        notes.append(
            "handler-backed Inventory packed delta; v546 entries are 108-byte Substruct_Item records"
        )
    elif type_id == 59:
        offset = decode_raw_blob_with_size(
            data,
            offset,
            fields,
            count_name="entry_count",
            count_size=2,
            size_name="packed_size",
            blob_name="raw_update_blob",
        )
        notes.append(
            "handler-backed AfterInvSpellUpdate-style packed delta; GameScene decodes 27-byte records into the secondary spellbook-adjacent state"
        )
    elif type_id == 60:
        offset = decode_recipe_book_update(data, offset, fields)
        notes.append(
            "handler-backed RecipeBook packed full array; v546 entries are 12-byte recipe_id/crc/unknown records"
        )
    elif type_id == 61:
        offset = decode_recipe_id_list(data, offset, fields)
        notes.append("client-derived RequestRecipeDetails layout; count is capped below 0x10001")
    elif type_id == 62:
        offset = decode_recipe_details(data, offset, fields)
        notes.append("handler-backed RecipeDetails v546 fixed-entry layout; each entry is 0x2d6 bytes")
    elif type_id in {63, 64}:
        offset = decode_skill_update(data, offset, fields)
        skill_opcode = "UpdateSkillBook" if type_id == 63 else "UpdateSkills"
        evidence = "handler-backed" if type_id == 63 else "client-derived"
        notes.append(
            f"{evidence} {skill_opcode} packed delta; v546 skill entries are 21-byte records"
        )
    elif type_id == 65:
        offset = decode_update_opportunity(data, offset, fields)
        notes.append("handler-backed UpdateOpportunity/HeroicOpportunity layout")
    elif type_id == 67:
        offset = decode_change_zone(data, offset, fields)
        notes.append("handler-backed ChangeZone layout; PacketParser v546 names drift in this range")
    elif type_id in {68, 69}:
        offset = decode_three_floats(data, offset, fields, ("x", "y", "z"))
        evidence = "handler-backed" if type_id == 69 else "client-derived"
        notes.append(f"{evidence} 3-float teleport/update layout; PacketParser v546 names drift in this range")
    elif type_id == 70:
        offset = decode_three_floats(data, offset, fields, ("x", "y", "z"))
        heading = read_float(data, offset)
        if heading is not None:
            add_field(fields, offset, 4, "float", "heading", heading)
            offset += 4
        notes.append("handler-backed TeleportWithinZoneNoReload layout; PacketParser v546 names drift in this range")
    elif type_id == 71:
        offset = decode_migrate_client_to_zone_request(data, offset, fields)
        notes.append("client-derived MigrateClientToZoneRequest layout; optional block has no explicit wire flag")
    elif type_id == 72:
        offset = decode_migrate_client_to_zone_reply(data, offset, fields)
        notes.append("client-derived MigrateClientToZoneReply layout")
    elif type_id == 73:
        notes.append("client-derived ReadyToZone empty payload")
    elif type_id == 74:
        offset = decode_ten_u32_six_u8(data, offset, fields)
        notes.append("client-derived RemoveClientFromGroup candidate ten-u32/six-u8 layout; no v546 XML struct")
    elif type_id in {75, 77, 82, 83, 84}:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (4, "u32", "u32_1")))
        group_name = {
            75: "RemoveGroupFromGroup",
            77: "GroupCreated",
            82: "GroupLeaderChanged",
            83: "GroupResendOOZData",
            84: "GroupSettingsChanged",
        }[type_id]
        notes.append(f"client-derived {group_name} candidate two-u32 layout; no v546 XML struct")
    elif type_id in {76, 78, 85}:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        group_name = {
            76: "MakeGroupLeader",
            78: "GroupDestroyed",
            85: "OutOfZoneMemberData",
        }[type_id]
        notes.append(f"client-derived {group_name} candidate single-u32 layout; no v546 XML struct")
    elif type_id == 79:
        offset = decode_group_member_added(data, offset, fields)
        notes.append("client-derived GroupMemberAdded tree layout; PacketParser v546 names drift in this range")
    elif type_id == 81:
        offset = decode_group_removed(data, offset, fields)
        notes.append("client-derived GroupRemovedFromGroup candidate; PacketParser v546 names drift in this range")
    elif type_id == 80:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1")),
        )
        notes.append("client-derived GroupMemberRemoved candidate two-u32 layout")
    elif type_id == 86:
        offset = decode_u32_six_u8(data, offset, fields)
        notes.append("client-derived SendLatestRequest candidate u32/six-u8 layout; no v546 XML struct")
    elif type_id == 87:
        offset = decode_clear_data(data, offset, fields)
        notes.append("client-derived ClearData layout; PacketParser v546 names drift in this range")
    elif type_id == 88:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((1, "u8", "flag"), (2, "u16", "u16_0")),
        )
        notes.append("client-derived SetSocial candidate u8/u16 layout")
    elif type_id == 89:
        value = read_u(data, offset, 1)
        if value is not None:
            add_field(fields, offset, 1, "u8", "status", value)
            offset += 1
        notes.append("handler-backed ClearData layout; handler asserts status/data type byte must be 1")
    elif type_id == 90:
        offset = decode_es_zone_instance_status(data, offset, fields)
        notes.append("client-derived ESZoneInstanceStatus layout; PacketParser v546 names drift in this range")
    elif type_id == 91:
        offset = decode_large_zone_status_table(data, offset, fields)
        notes.append("client-derived ZonesStatusRequest candidate fixed 0x19b-pair table; no v546 XML struct")
    elif type_id == 92:
        offset = decode_zones_status(data, offset, fields)
        notes.append("client-derived ZonesStatus layout; PacketParser v546 names drift in this range")
    elif type_id == 93:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived weather-adjacent single-u32 layout")
    elif type_id == 94:
        offset = decode_zone_servers_instances_status(data, offset, fields)
        notes.append(
            "handler-backed ZonesStatus nested server/instance layout; no v546 XML struct"
        )
    elif type_id in {95, 96}:
        offset = decode_string16_fields(data, offset, fields, ("text_0", "text_1"))
        notes.append("client-derived two-string16 dialog layout; PacketParser v546 names drift in this range")
    elif type_id == 97:
        offset = decode_u32_string16_list(data, offset, fields, "effect")
        notes.append("client-derived RemoveSpellEffect string-list layout; PacketParser v546 names drift in this range")
    elif type_id == 98:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1")),
        )
        notes.append("client-derived RemoveConcentration candidate two-u32 layout")
    elif type_id == 99:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "quest_id"),))
        notes.append("likely QuestJournalOpen by source handler/order; client layout is single quest_id u32")
    elif type_id == 100:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "quest_id"), (4, "u32", "unknown_u32_0")),
        )
        notes.append("likely QuestJournalInspect by source handler/order; client layout is quest_id plus an extra u32 not used by the legacy handler")
    elif type_id == 101:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (4, "u32", "u32_2")),
        )
        notes.append("client-derived quest-journal gap three-u32 layout; not the active spell-slot mapping packet")
    elif type_id == 102:
        notes.append("client-derived quest-journal gap empty payload; exact opcode name unresolved")
    elif type_id == 103:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (4, "u32", "quest_or_entry_id"),
                (4, "u32", "journal_context_id"),
                (1, "u8", "select_or_refresh_flag"),
                (1, "u8", "source_flag"),
                (4, "u32", "reserved_u32"),
            ),
        )
        notes.append("Quest Journal UI sends this row when selecting or refreshing quest focus; trailing u32 semantics remain unresolved")
    elif type_id == 104:
        offset = decode_counted_u32_list_with_u8(data, offset, fields, "selection_count")
        notes.append("client-derived QuestJournalSetVisible layout; count is capped below 0x401")
    elif type_id == 105:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "quest_id"),))
        notes.append("likely QuestJournalWaypoint by source handler/order before CreateGuildRequest; client layout is single quest_id u32")
    elif type_id == 106:
        offset = decode_create_guild_request(data, offset, fields)
        notes.append("client-derived CreateGuildRequest candidate; PacketParser v546 names drift in this range")
    elif type_id == 107:
        offset = decode_create_guild_reply(data, offset, fields)
        notes.append("client-derived CreateGuildReply layout with shared GuildUpdatePayload")
    elif type_id == 108:
        offset = decode_guildsay(data, offset, fields)
        notes.append("client-derived Guildsay candidate; PacketParser v546 names drift in this range")
    elif type_id == 109:
        offset = decode_guild_or_fellowship_status(data, offset, fields)
        notes.append("likely GuildKick by commented source/oplist gap between Guildsay and GuildUpdate; field semantics still need handler evidence")
    elif type_id == 110:
        offset = decode_guild_update_payload(data, offset, fields, "guild")
        notes.append("handler-backed GuildUpdate shared payload layout")
    elif type_id == 111:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived guild-adjacent single-u32 layout; do not confuse with actual DeleteGuild row at client type 322")
    elif type_id == 112:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (4, "u32", "u32_2")),
        )
        notes.append("client-derived FellowshipExp candidate three-u32 layout")
    elif type_id == 113:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (1, "u8", "flag")),
        )
        notes.append("client-derived unnamed extra row between FellowshipExp and ConsignmentCloseStore")
    elif type_id == 114:
        offset = decode_consignment_close_store(data, offset, fields)
        notes.append("client-derived ConsignmentCloseStore layout with shared store payload")
    elif type_id == 115:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived likely ConsignItemRequest single-u32 layout from PacketParser/source order")
    elif type_id == 116:
        offset = decode_char_transfer_base(data, offset, fields, "base")
        notes.append("client-derived likely ConsignItemResponse base-field layout; serializer is reused by char-transfer rows")
    elif type_id == 117:
        offset = decode_purchase_consignment_response(data, offset, fields)
        notes.append("client-derived PurchaseConsignmentResponse layout; blob size is capped below 0x10001")
    elif type_id == 118:
        offset = decode_string16_fields(data, offset, fields, ("house_name",))
        notes.append("client-derived HouseDeletedRemotely string layout")
    elif type_id == 119:
        offset = decode_string16_fields(data, offset, fields, ("text_0", "text_1"))
        notes.append("client-derived UpdateHouseData two-string layout")
    elif type_id == 120:
        offset = decode_string16_u32(data, offset, fields, "house_name")
        notes.append("client-derived UpdateHouseAccessData layout")
    elif type_id == 121:
        offset = decode_house_base_screen(data, offset, fields)
        notes.append("client-derived PlayerHouseBaseScreen envelope")
    elif type_id == 122:
        offset = decode_house_purchase_screen(data, offset, fields)
        notes.append("client-derived PlayerHousePurchaseScreen envelope")
    elif type_id == 123:
        offset = decode_player_house_access_update(data, offset, fields)
        notes.append("handler-backed PlayerHouseAccessUpdate layout routed to the PlayerHouse UI")
    elif type_id == 124:
        offset = decode_house_display_status(data, offset, fields)
        notes.append("handler-backed PlayerHouseDisplayStatus layout routed to the PurchaseHouse UI")
    elif type_id == 125:
        offset = decode_house_close_ui(data, offset, fields)
        notes.append("handler-backed PlayerHouseCloseUI layout routed to the PlayerHouse UI")
    elif type_id == 126:
        offset = decode_scalar_layout(data, offset, fields, ((1, "u8", "flag"),))
        notes.append("handler-backed BuyPlayerHouse response/status byte")
    elif type_id == 127:
        notes.append("handler-backed empty PlayerHouse UI lookup row; source-order BuyPlayerHouseTint name remains provisional")
    elif type_id == 128:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "house_id"),))
        notes.append("send-site-backed BuyPlayerHouse request from PurchaseHouse BuyButton")
    elif type_id in {130, 131}:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived house-adjacent single-u32 layout")
    elif type_id == 132:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "room_id"),))
        notes.append("likely EnterHouse row; PlayerHouse visit UI sends the ROOMID when access/upkeep checks allow direct entry")
    elif type_id == 129:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (1, "u8", "flag")))
        notes.append("client-derived house-adjacent u32/u8 layout")
    elif type_id == 133:
        notes.append("likely ExitHouse empty row; PlayerHouse button handler sends this from the in-house branch")
    elif type_id == 134:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "house_id"), (1, "u8", "access_level")))
        notes.append("likely HouseDefaultAccessSet row; PlayerHouse UI sends AccessEnumValue with the house id")
    elif type_id == 135:
        value = read_u(data, offset, 4)
        if value is not None:
            add_field(fields, offset, 4, "u32", "house_id", value)
            offset += 4
        offset = decode_string16_fields(data, offset, fields, ("player_name",))
        flag = read_u(data, offset, 1)
        if flag is not None:
            add_field(fields, offset, 1, "u8", "access_level", flag)
            offset += 1
        notes.append("PlayerHouse access UI send site uses AccessEnumValue and logs Sending access set message to server")
    elif type_id == 136:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "house_id"), (4, "u32", "player_dbid")),
        )
        notes.append("PlayerHouse access UI send site uses PlayerDBID and logs Sending remove message to server")
    elif type_id == 137:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "house_id"), (1, "u8", "payment_flag")),
        )
        notes.append("likely PayHouseUpkeep row; PlayerHouse UI derives the flag from cached coin/status comparisons")
    elif type_id == 138:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "unknown_0"), (4, "u32", "unknown_1"), (4, "u32", "unknown_2")),
        )
        notes.append("handler-backed MoveableObjectPlacementCriteria layout copied into GameScene placement state")
    elif type_id == 139:
        offset = decode_enter_move_object_mode(data, offset, fields)
        notes.append("handler-backed EnterMoveObjectMode layout; starts with WS_MoveObjectMode v1 fields plus counted id list")
    elif type_id == 140:
        offset = decode_position_moveable_object(data, offset, fields)
        notes.append("send-site-backed PositionMoveableObject layout from the place_item path")
    elif type_id == 141:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "spawn_id"),))
        notes.append("send-site-backed CancelMoveObjectMode row; restores cached moved-object state and sends the spawn id")
    elif type_id == 142:
        offset = decode_counted_u32_string16_entries(
            data, offset, fields, count_size=1, count_kind="u8", prefix="entry"
        )
        notes.append("handler-backed HouseCustomizationScreen layout; PacketParser names drift in this range")
    elif type_id == 143:
        offset = decode_customization_purchase_request(data, offset, fields)
        notes.append("handler-backed CustomizationPurchaseRequest layout; packed count plus trailing flag")
    elif type_id == 144:
        offset = decode_customization_set_request(data, offset, fields)
        notes.append("handler-backed CustomizationSetRequest layout routed to InteriorCustomization UI")
    elif type_id in {145, 146}:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "customization_id"), (4, "u32", "shader_id")))
        notes.append("shader/tint selection send site reads the ShaderID UI property; exact opcode name remains unresolved")
    elif type_id == 147:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (1, "u8", "flag")))
        notes.append("handler-backed interior customization reply layout; GameScene routes this row to Eq2GuiInteriorCustomizationWindow::update")
    elif type_id == 148:
        offset = decode_tint_widgets(data, offset, fields)
        notes.append("likely TintWidgets layout: counted object_id plus RGB entries; consignment response is client type 150")
    elif type_id == 149:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (4, "u32", "consignment_context_id"),
                (8, "u64", "broker_item_id"),
                (4, "u32", "item_id_or_quantity"),
                (1, "u8", "flag_0"),
                (1, "u8", "flag_1"),
            ),
        )
        notes.append("likely ExamineConsignmentRequest layout immediately before the assertion-backed response at type 150; field meanings remain provisional")
    elif type_id == 150:
        offset = decode_examine_consignment_response(data, offset, fields)
        notes.append("client-derived ExamineConsignmentResponse layout; blob size is capped below 0x10001")
    elif type_id == 151:
        offset = decode_u16_sized_raw_u16(data, offset, fields, "ui_blob")
        notes.append("handler-backed UISettingsResponse-style raw blob plus trailing u16")
    elif type_id == 152:
        notes.append("source-order UIReset empty payload; GameScene routes this type to the unhandled-message branch")
    elif type_id == 153:
        notes.append("capture-backed KeymapLoad empty payload; EQ2Emu login server handles this row as OP_KeymapLoadMsg")
    elif type_id == 154:
        notes.append("handler-backed KeymapNone empty payload; generic keymap handler resets/defaults keymap state")
    elif type_id in {155, 156}:
        offset = decode_raw_blob_with_size(
            data,
            offset,
            fields,
            count_name=None,
            count_size=0,
            size_name="packed_size",
            blob_name="keymap_blob",
        )
        notes.append("client-derived Keymap raw blob; deserializer caps packed_size at 0x8000")
    elif type_id == 157:
        offset = decode_dispatch_spell_cmd(data, offset, fields)
        notes.append("client-derived DispatchSpellCmd envelope; method 0 payload is capped at 0x8000")
    elif type_id == 159:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "spawn_id"),))
        notes.append("client-derived EntityVerbsRequest layout matching WS_EntityVerbsRequest")
    elif type_id == 160:
        offset = decode_entity_verbs_reply(data, offset, fields)
        notes.append("handler-backed EntityVerbsReply layout routed to the RadialMenu UI")
    elif type_id == 161:
        offset = decode_u32_string16(data, offset, fields, "command")
        notes.append("client-derived EntityVerbsVerb candidate; u32 spawn/id plus command string")
    elif type_id == 162:
        offset = decode_chat_relationship_update(data, offset, fields)
        notes.append("handler-backed ChatRelationshipUpdate layout; client omits the PacketParser trailing int16 per entry")
    elif type_id == 163:
        offset = decode_loot_items_request(data, offset, fields)
        notes.append("client-derived LootItemsRequest layout matching WS_LootItem v546")
    elif type_id == 164:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "spawn_id"),))
        notes.append("handler-backed StoppedLooting layout matching WS_StoppedLooting")
    elif type_id == 165:
        notes.append("handler-backed Sit empty payload; toggles local player seated/action state")
    elif type_id == 166:
        notes.append("handler-backed Stand empty payload; toggles local player seated/action state")
    elif type_id in {167, 168, 171, 172}:
        notes.append(
            "likely empty sit/stand/takeoff/illusion toggle row by PacketParser v546 order after StoppedLooting"
        )
    elif type_id == 169:
        notes.append("handler-backed ClearForTakeOff empty payload; resets the takeoff/flight state timer")
    elif type_id == 170:
        notes.append("handler-backed ReadyForTakeOff empty payload; arms the takeoff/flight state timer")
    elif type_id == 173:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (4, "u32", "u32_2")),
        )
        notes.append("client-derived ExamineItemRequest candidate; row is shifted +1 from PacketParser v546")
    elif type_id == 174:
        offset = decode_scalar_layout(data, offset, fields, ((1, "u8", "flag_0"), (1, "u8", "flag_1")))
        notes.append("client-derived ReadBookPage candidate; row is shifted +1 from PacketParser v546")
    elif type_id == 175:
        notes.append("client-derived DefaultGroupOptionsRequest layout; empty payload matching WS_DefaultGroupOptionsRequestMsg")
    elif type_id == 176:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (1, "u8", "loot_method"),
                (1, "u8", "loot_items_rarity"),
                (1, "u8", "auto_split_coin"),
                (1, "u8", "default_yell_method"),
                (1, "u8", "default_group_lock_method"),
                (1, "u8", "group_autolock"),
            ),
        )
        notes.append("handler-backed DefaultGroupOptions layout matching WS_DefaultGroupOptions v546")
    elif type_id == 177:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (1, "u8", "loot_method"),
                (1, "u8", "loot_items_rarity"),
                (1, "u8", "auto_split_coin"),
                (1, "u8", "default_yell_method"),
                (1, "u8", "default_group_lock_method"),
                (1, "u8", "group_autolock"),
            ),
        )
        notes.append("client-derived GroupOptions candidate; six-u8 layout mirrors DefaultGroupOptions")
    elif type_id == 178:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            tuple((1, "u8", f"u8_{index}") for index in range(7)),
        )
        notes.append("handler-backed DisplayGroupOptionsScreen layout; row is shifted +1 from PacketParser v546")
    elif type_id == 179:
        offset = decode_display_inn_visit_screen(data, offset, fields)
        notes.append("handler-backed DisplayInnVisitScreen layout; count is capped below 0x10001")
    elif type_id == 180:
        offset = decode_dump_scheduler(data, offset, fields)
        notes.append("client-derived DumpScheduler layout; count is capped below 0x10001")
    elif type_id == 181:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (4, "u32", "u32_1")))
        notes.append("client-derived LSRequestPlayerDesc-adjacent two-u32 layout")
    elif type_id == 182:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived LSCheckAcctLock-adjacent single-u32 layout")
    elif type_id == 183:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (1, "u8", "flag")))
        notes.append("client-derived WSAcctLockStatus-adjacent u32/u8 layout")
    elif type_id == 184:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived RequestHelpRepath-adjacent single-u32 layout")
    elif type_id == 185:
        notes.append("client-derived RequestTargetLoc-adjacent empty payload")
    elif type_id in {186, 191}:
        offset = decode_string16_fields(data, offset, fields, ("text",))
        notes.append("client-derived single-string16 layout in the 181-210 drift range")
    elif type_id == 187:
        offset = decode_perform_player_knockback(data, offset, fields)
        notes.append("handler-backed PerformPlayerKnockback layout; DoF client body is five floats and omits the old XML trailing byte fields")
    elif type_id == 188:
        intensity = read_float(data, offset)
        raw_intensity = read_u(data, offset, 4)
        if intensity is not None and raw_intensity is not None:
            add_field(fields, offset, 4, "float/u32", "intensity", {"float": intensity, "u32": raw_intensity})
            offset += 4
        notes.append("handler-backed PerformCameraShake layout matching WS_PerformCameraShakeMsg v546")
    elif type_id == 189:
        offset = decode_populate_skill_maps(data, offset, fields)
        notes.append("handler-backed PopulateSkillMaps layout; count is capped below 0x10001")
    elif type_id == 190:
        notes.append("client-derived CancelledFeign empty payload")
    elif type_id == 192:
        offset = decode_show_create_from_recipe_ui(data, offset, fields)
        notes.append("handler-backed ShowCreateFromRecipeUI complex crafting list layout")
    elif type_id == 193:
        notes.append("handler-backed CancelCreateFromRecipe empty payload")
    elif type_id == 194:
        offset = decode_begin_item_creation(data, offset, fields)
        notes.append("client-derived BeginItemCreation nested u32/u8 pair-list layout")
    elif type_id == 195:
        notes.append("handler-backed StopItemCreation empty payload")
    elif type_id == 196:
        offset, complete = decode_show_item_creation_process(data, offset, fields)
        if not complete:
            notes.append("item-detail helper variant payload could not be fully parsed")
        notes.append("handler-backed ShowItemCreationProcessUI five-slot-state layout")
    elif type_id == 197:
        offset = decode_update_item_creation_process(data, offset, fields)
        notes.append("handler-backed UpdateItemCreationProcessUI layout")
    elif type_id == 198:
        offset = decode_scalar_layout(data, offset, fields, ((1, "u8", "flag"),))
        notes.append("handler-backed DisplayTSEventReaction single-u8 layout")
    elif type_id == 199:
        offset = decode_show_recipe_book(data, offset, fields)
        notes.append("handler-backed ShowRecipeBook bit-mask layout; bit_count is capped below 0x10001")
    elif type_id == 200:
        offset = decode_u32_two_string16(data, offset, fields, ("query", "context"))
        notes.append("client-derived KnowledgebaseRequest candidate")
    elif type_id == 201:
        offset = decode_knowledgebase_response(data, offset, fields)
        notes.append("handler-backed KnowledgebaseResponse layout routed to the Help UI; summary count is capped below 0x10001")
    elif type_id == 202:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived CSTicketHeaderRequest candidate single-u32 layout")
    elif type_id == 203:
        offset = decode_cs_ticket_info(data, offset, fields)
        notes.append("handler-backed CSTicketInfo layout routed to the Help UI; ticket count is capped below 0x401")
    elif type_id == 204:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1")),
        )
        notes.append("client-derived CSTicketCommentRequest candidate two-u32 layout")
    elif type_id == 205:
        offset = decode_cs_ticket_comment_response(data, offset, fields)
        notes.append("handler-backed CSTicketCommentResponse layout routed to the Help UI; comment count is capped below 0x81")
    elif type_id == 206:
        offset = decode_three_u32_three_string16(data, offset, fields)
        notes.append("client-derived CSTicketCreate candidate")
    elif type_id == 207:
        offset = decode_two_u32_string16(data, offset, fields)
        notes.append("client-derived CSTicketAddComment candidate")
    elif type_id == 208:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (4, "u32", "u32_1")))
        notes.append("client-derived CSTicketDelete two-u32 layout")
    elif type_id == 209:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (1, "u8", "flag"), (4, "u32", "u32_1")))
        notes.append("handler-backed CSTicketChangeNotification u32/u8/u32 layout routed to the Help UI")
    elif type_id == 210:
        offset = decode_world_data_update(data, offset, fields)
        notes.append("client-derived WorldDataUpdate envelope")
    elif type_id == 211:
        offset = decode_known_languages(data, offset, fields)
        notes.append("handler-backed KnownLanguages byte-list layout")
    elif type_id == 218:
        offset = decode_client_teleport_to_location(data, offset, fields)
        notes.append("client-derived ClientTeleportToLocation candidate after registry gap")
    elif type_id == 219:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (1, "u8", "flag")))
        notes.append("handler-backed UpdateClientPredFlags u32/u8 layout")
    elif type_id == 220:
        offset = decode_scalar_layout(data, offset, fields, ((1, "u8", "toggle"),))
        notes.append("handler-backed ChangeServerControlFlag single-u8 layout; not the CSToolsRequest row")
    elif type_id == 221:
        offset = decode_two_string_four_u32(data, offset, fields)
        notes.append("client-derived CSToolsResponse candidate")
    elif type_id == 222:
        offset = decode_string_three_u32(data, offset, fields)
        notes.append("client-derived AddSocialStructureStanding candidate")
    elif type_id == 223:
        offset = decode_string_u32_u8_u32_u32(data, offset, fields)
        notes.append("client-derived boat/social-standing adjacent row; exact opcode name still provisional")
    elif type_id == 224:
        offset = decode_create_boat_transports(data, offset, fields)
        notes.append("client-derived CreateBoatTransports candidate")
    elif type_id == 225:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (2, "u16", "u16_0")))
        notes.append("client-derived PositionBoatTransport u32/u16 layout")
    elif type_id == 226:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived MigrateBoatTransport single-u32 layout")
    elif type_id == 227:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (1, "u8", "flag")))
        notes.append("client-derived MigrateBoatTransportReply u32/u8 layout")
    elif type_id == 228:
        offset = decode_debug_nll_points(data, offset, fields)
        notes.append("client-derived DisplayDebugNLLPoints candidate")
    elif type_id == 229:
        offset = decode_examine_info_request(data, offset, fields)
        notes.append("handler-backed ExamineInfoRequest conditional layout")
    elif type_id == 230:
        offset = decode_quickbar_init(data, offset, fields)
        notes.append("handler-backed QuickbarInit layout; count is capped below 0x401")
    elif type_id == 231:
        offset = decode_quickbar_entry(data, offset, fields, "entry")
        notes.append("client-derived QuickbarUpdate layout")
    elif type_id == 232:
        offset = decode_macro_init(data, offset, fields)
        notes.append("handler-backed MacroInit layout; count is capped below 0x401")
    elif type_id == 233:
        offset = decode_macro_entry(data, offset, fields, "macro")
        notes.append("client-derived MacroUpdate layout")
    elif type_id == 234:
        offset = decode_questionnaire(data, offset, fields)
        notes.append("client-derived Questionnaire conditional string layout")
    elif type_id == 235:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((2, "u16", "old_level"), (2, "u16", "new_level"), (1, "u8", "type")),
        )
        notes.append("handler-backed LevelChanged layout matching WS_LevelChanged")
    elif type_id == 236:
        offset = decode_display_warning_msg(data, offset, fields)
        notes.append("handler-backed DisplayWarning layout")
    elif type_id == 237:
        offset = decode_string16_fields(data, offset, fields, ("message",))
        notes.append("handler-backed EncounterBroken compact string16 layout")
    elif type_id == 238:
        offset = decode_onscreen_msg(data, offset, fields)
        notes.append("handler-backed OnscreenMsg popup layout matching WS_OnScreenMsg")
    elif type_id == 239:
        offset = decode_modify_guild(data, offset, fields)
        notes.append(
            "handler-backed ModifyGuild candidate; trailing flags are present only on one serializer path"
        )
    elif type_id == 240:
        offset = decode_guild_event(data, offset, fields)
        notes.append("client-derived GuildEvent candidate")
    elif type_id == 241:
        offset = decode_u32_u32_string16_u32(data, offset, fields)
        notes.append("client-derived guild-event adjacent u32/u32/string16/u32 layout; exact opcode label unresolved")
    elif type_id == 242:
        offset = decode_u32_u64_u32_u32_string16(data, offset, fields)
        notes.append("handler-backed GuildEventAdd layout matching WS_GuildEventAdd")
    elif type_id == 243:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "account_id"), (8, "u64", "event_id"), (4, "u32", "action_unknown_packed")),
        )
        notes.append("handler-backed GuildEventAction layout matching WS_GuildEventAction packed as u32/u64/u32")
    elif type_id == 244:
        offset = decode_u32_u16_u64_list(data, offset, fields, "event", include_flags=True)
        notes.append("handler-backed GuildEventList u64-id list plus u8 flags; deserializer rejects counts >= 0x1f5")
    elif type_id == 245:
        offset = decode_u32_u16_u64_list(data, offset, fields, "event")
        notes.append("client-derived GuildEventDetails u64-id list; deserializer rejects counts >= 0x1f5")
    elif type_id == 246:
        offset = decode_u32_u64_u32_u32_string16(data, offset, fields)
        notes.append("handler-backed RequestGuildInfo layout matching WS_RequestGuildInfo")
    elif type_id == 247:
        offset = decode_three_u32_string16(data, offset, fields)
        notes.append("client-derived guild-info adjacent u32/u32/u32/string16 layout; exact opcode label unresolved")
    elif type_id == 248:
        offset = decode_guild_bank_action(data, offset, fields)
        notes.append("handler-backed GuildBankAction fixed fields, string8 fields, and u16-sized blob layout")
    elif type_id == 249:
        offset = decode_guild_bank_action_response(data, offset, fields)
        notes.append("client-derived GuildBankActionResponse fixed fields and u16-sized blob layout")
    elif type_id == 250:
        offset = decode_guild_bank_item_details_response(data, offset, fields)
        notes.append("client-derived GuildBankItemDetailsResponse u16-sized blob layout")
    elif type_id == 251:
        offset = decode_guild_bank_small_update(data, offset, fields)
        notes.append("client-derived GuildBankSmallUpdate fixed/raw/string8 layout")
    elif type_id == 252:
        offset = decode_guild_bank_update(data, offset, fields)
        notes.append(
            "handler-backed GuildBankUpdate u32-sized blob layout; deserializer rejects sizes >= 0x1001"
        )
    elif type_id in {253, 254}:
        offset = decode_u32_u8_u16_u64_list(data, offset, fields, "event")
        evidence = "handler-backed" if type_id == 253 else "client-derived"
        notes.append(f"{evidence} guild-bank event u64-id list layout")
    elif type_id == 255:
        offset = decode_reward_pack(data, offset, fields)
        notes.append("handler-backed RewardPack header/list layout with item-detail helper variants")
    elif type_id == 256:
        offset = decode_u32_string16(data, offset, fields, "guild_name")
        notes.append("client-derived RenameGuild candidate")
    elif type_id in {257, 258}:
        offset = decode_string16_u32(data, offset, fields, "text")
        notes.append("client-derived ZoneToFriend string16/u32 layout")
    elif type_id in {261, 262}:
        offset = decode_u32_string16(data, offset, fields, "text")
        notes.append("client-derived u32/string16 chat-channel layout")
    elif type_id == 259:
        offset = decode_u8_u32_two_string16(data, offset, fields)
        notes.append("client-derived ChatCreateChannel layout")
    elif type_id in {260, 263, 264}:
        offset = decode_u32_two_string16(data, offset, fields, ("text_0", "text_1"))
        notes.append("client-derived u32/two-string16 chat-channel layout")
    elif type_id in {265, 266}:
        offset = decode_u8_u32_string16(data, offset, fields)
        notes.append("client-derived chat friend/ignore toggle layout")
    elif type_id in {267, 268}:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived chat friend/ignore send-list single-u32 layout")
    elif type_id == 269:
        offset = decode_u16_sized_raw(data, offset, fields, "byte_count", "raw_bytes")
        notes.append("likely ChatFilters by source-order slot after ChatSendIgnores; client serializes a u16-sized raw field capped at 0x32")
    elif type_id == 270:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived MailGetHeaders candidate single-u32 layout")
    elif type_id == 271:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((8, "u64", "u64_0"), (4, "u32", "u32_0"), (4, "u32", "u32_1"), (1, "u8", "flag")),
        )
        notes.append("client-derived MailGetMessage candidate u64/u32/u32/u8 layout")
    elif type_id == 272:
        offset = decode_mail_send_message(data, offset, fields)
        notes.append("client-derived MailSendMessage layout; shared attachment blob is capped at 0x1000")
    elif type_id == 273:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1")),
        )
        notes.append("client-derived MailDeleteMessage candidate two-u32 layout")
    elif type_id == 274:
        offset = decode_mail_get_headers_reply(data, offset, fields)
        notes.append("handler-backed MailGetHeadersReply layout; message count is a u8 in this client")
    elif type_id == 275:
        offset = decode_mail_get_message_reply(data, offset, fields)
        notes.append("handler-backed MailGetMessageReply layout")
    elif type_id == 276:
        offset = decode_mail_send_message_reply(data, offset, fields)
        notes.append("handler-backed MailSendMessageReply layout")
    elif type_id == 277:
        offset = decode_u64_u32_string16_u32(data, offset, fields)
        notes.append("client-derived MailCommitSendMessage candidate")
    elif type_id == 278:
        offset = decode_mail_send_system_message(data, offset, fields)
        notes.append("client-derived MailSendSystemMessage candidate; shared attachment blob is capped at 0x1000")
    elif type_id == 279:
        offset = decode_string_u32_u32(data, offset, fields)
        notes.append("client-derived MailRemoveAttachFromMail candidate")
    elif type_id == 280:
        notes.append("client-derived WaypointRequest empty payload")
    elif type_id == 281:
        offset = decode_waypoint_list(data, offset, fields, include_update_flag=False)
        notes.append("handler-backed WaypointReply layout; count is capped below 0x10001")
    elif type_id == 282:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("handler-backed WaypointSelect single-u32 layout")
    elif type_id == 283:
        offset = decode_waypoint_list(data, offset, fields, include_update_flag=True)
        notes.append("handler-backed WaypointUpdate layout; count is capped below 0x10001")
    elif type_id == 284:
        offset = decode_two_string_three_u32(data, offset, fields)
        notes.append("client-derived CharNameChanged candidate")
    elif type_id == 285:
        offset = decode_show_zone_teleporter_destinations(data, offset, fields)
        notes.append("handler-backed ShowZoneTeleporterDestinations layout; count is capped below 0x41")
    elif type_id == 286:
        offset = decode_u32_u32_string16_u32(data, offset, fields)
        notes.append("client-derived SelectZoneTeleporterDestination candidate")
    elif type_id == 287:
        notes.append("client-derived ReloadLocalizedTxt empty payload")
    elif type_id == 288:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"), (4, "u32", "u32_1")))
        notes.append("client-derived RequestGuildMembership two-u32 layout")
    elif type_id == 289:
        offset = decode_guild_membership_response(data, offset, fields)
        notes.append("handler-backed GuildMembershipResponse layout; member count is capped below 0x10001")
    elif type_id == 290:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (2, "u16", "u16_0")),
        )
        notes.append("handler-backed LeaveGuildNotify u32/u32/u16 layout")
    elif type_id == 291:
        offset = decode_join_guild_notify(data, offset, fields)
        notes.append("handler-backed JoinGuildNotify layout")
    elif type_id == 292:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            tuple((4, "u32", f"u32_{index}") for index in range(7)),
        )
        notes.append("client-derived AvatarUpdate candidate seven-u32 layout")
    elif type_id == 293:
        offset = decode_string16_fields(data, offset, fields, ("biography",))
        notes.append("handler-backed BioUpdate single-string16 layout routed to InspectPlayer UI")
    elif type_id == 294:
        value = read_u(data, offset, 1)
        if value is not None:
            add_field(fields, offset, 1, "u8", "quest_reward_flag", value)
            offset += 1
        pp_offset, struct_meta = decode_packetparser_struct(
            data, offset, type_id, fields, structs_by_opcode, structs_by_name
        )
        if pp_offset != offset:
            offset = pp_offset
        notes.append("handler-backed QuestReward wrapper; payload follows PacketParser WS_QuestComplete")
    elif type_id in {295, 296, 297}:
        offset = decode_scalar_layout(data, offset, fields, ((1, "u8", "flag"),))
        lock_name = {
            295: "WSServerLock",
            296: "LSServerLock",
            297: "WSServerHide",
        }[type_id]
        notes.append(f"client-derived {lock_name} candidate single-u8 layout; source/PacketParser order differs for 296/297")
    elif type_id == 298:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "category_or_request_id"),))
        notes.append("client-derived CsCategoryRequest candidate single-u32 layout before CsCategoryResponse")
    elif type_id == 299:
        offset = decode_cs_category_response(data, offset, fields)
        notes.append("handler-backed CsCategoryResponse layout routed to the Help UI; category count is capped below 0x1001")
    elif type_id == 300:
        offset = decode_knowledge_window_slot_mapping(data, offset, fields)
        notes.append("handler-backed KnowledgeWindowSlotMapping layout matching WS_SpellSlotMapping")
    elif type_id == 301:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((1, "u8", "flag_0"), (1, "u8", "flag_1")),
        )
        notes.append("handler-backed LFGUpdate candidate two-u8 layout routed to the GroupMembers UI")
    elif type_id == 302:
        offset = decode_u32_u8_string16(data, offset, fields)
        notes.append("client-derived AFKUpdate candidate")
    elif type_id == 303:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (1, "u8", "flag_0"), (1, "u8", "flag_1")),
        )
        notes.append("client-derived AnonUpdate candidate u32/u8/u8 layout")
    elif type_id == 304:
        offset = decode_update_active_public_zones(data, offset, fields)
        notes.append("client-derived UpdateActivePublicZones layout; zone_count is capped below 0x10001")
    elif type_id == 305:
        offset = decode_five_u32(data, offset, fields)
        notes.append("client-derived five-u32 UnknownNpc-shaped layout")
    elif type_id == 306:
        offset = decode_promo_flags_details(data, offset, fields)
        notes.append("handler-backed PromoFlagsDetails layout routed to the Claim UI; item_count is capped below 0x401")
    elif type_id == 307:
        offset = decode_consign_view_create(data, offset, fields)
        notes.append("client-derived ConsignViewCreate layout; skill_count is capped below 0x10001")
    elif type_id == 308:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((8, "u64", "u64_0"), (4, "u32", "u32_0"), (4, "u32", "u32_1"), (4, "u32", "u32_2"), (4, "u32", "u32_3")),
        )
        notes.append("client-derived ConsignViewGetPage u64/four-u32 layout")
    elif type_id == 309:
        offset = decode_scalar_layout(data, offset, fields, ((8, "u64", "u64_0"), (4, "u32", "u32_0")))
        notes.append("client-derived ConsignViewRelease u64/u32 layout")
    elif type_id == 310:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((8, "u64", "u64_0"), (4, "u32", "u32_0"), (4, "u32", "u32_1")),
        )
        notes.append("client-derived ConsignRemoveItems u64/two-u32 layout")
    elif type_id == 311:
        offset = decode_six_u32_raw12_u32(data, offset, fields)
        notes.append("client-derived UpdateDebugRadii layout")
    elif type_id == 312:
        offset = decode_u32_u32_u8_string16_u8(data, offset, fields)
        notes.append("client-derived Snoop layout; emu_oplist.h comments this opcode row out")
    elif type_id == 313:
        offset = decode_string16_fields(data, offset, fields, ("text_0", "text_1"))
        notes.append("client-derived Report two-string16 layout")
    elif type_id == 314:
        offset = decode_raid_update(data, offset, fields)
        notes.append(
            "handler-backed UpdateRaid packed full sheet or delta; v546 body is 24 raid-member slots"
        )
    elif type_id == 315:
        offset = decode_raw_blob_with_size(
            data,
            offset,
            fields,
            count_name=None,
            count_size=0,
            size_name="blob_size",
            blob_name="arena_update_blob",
        )
        notes.append("handler-backed UpdateArena opaque blob layout; client rejects blob_size > 0x1000")
    elif type_id == 316:
        offset = decode_u64_u32_u32_u64_u32_u32(data, offset, fields)
        notes.append("client-derived ConsignViewSort layout")
    elif type_id == 317:
        offset = decode_title_update(data, offset, fields)
        notes.append("handler-backed TitleUpdate layout routed to InspectPlayer UI")
    elif type_id == 318:
        offset = decode_client_fell(data, offset, fields)
        notes.append("client-derived ClientFell layout")
    elif type_id == 319:
        notes.append("client-derived ClientInDeathRegion empty row")
    elif type_id == 320:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "camp_value"),))
        notes.append("client-derived CampClient single-u32 layout")
    elif type_id == 321:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "value_0"), (1, "u8", "response")),
        )
        notes.append("client-derived CSToolAccessResponse u32/u8 layout")
    elif type_id == 322:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "guild_id"),))
        notes.append("client-derived DeleteGuild single-u32 layout; source oplist keeps this row before TrackingUpdate")
    elif type_id == 323:
        offset = decode_tracking_update(data, offset, fields)
        notes.append("handler-backed TrackingUpdate layout")
    elif type_id == 324:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "spawn_id"),))
        notes.append("client-derived BeginTracking layout")
    elif type_id == 325:
        notes.append("client-derived StopTracking empty row")
    elif type_id == 326:
        notes.append("client-derived GetAvatarAccessRequestForCSTools empty row")
    elif type_id == 327:
        offset = decode_scalar_layout(data, offset, fields, ((8, "u64", "advancement_payload"),))
        notes.append("handler-backed AdvancementRequest 8-byte layout; GameScene inline case caches the two u32 halves")
    elif type_id == 328:
        offset = decode_map_fog_data_init(data, offset, fields)
        notes.append("handler-backed MapFogDataInit layout; GameScene logs m_processMapFogDataInitMsg for this type")
    elif type_id == 329:
        offset = decode_map_fog_data_update(data, offset, fields)
        notes.append("source-adjacent MapFogDataUpdate layout using the same compact fog-location record helper as type 328")
    elif type_id == 330:
        notes.append("handler-backed CloseGroupInviteWindow empty payload")
    elif type_id == 331:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (4, "u32", "u32_0"),
                (4, "u32", "u32_1"),
                (4, "u32", "u32_2"),
                (4, "u32", "u32_3"),
                (4, "u32", "u32_4"),
                (4, "u32", "u32_5"),
            ),
        )
        notes.append("client-derived CorruptedClient candidate six-u32 layout")
    elif type_id == 332:
        offset = decode_scalar_layout(data, offset, fields, ((1, "u8", "value"),))
        notes.append("client-derived WorldDataChange candidate one-byte layout")
    elif type_id == 333:
        offset = decode_u8_three_string16(data, offset, fields)
        notes.append("client-derived MailEventNotification layout")
    elif type_id == 334:
        offset = decode_offer_quest(data, offset, fields)
        notes.append("handler-backed OfferQuest layout")
    elif type_id == 335:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((1, "u8", "flag"), (4, "u32", "value")),
        )
        notes.append("client-derived RestartZone candidate u8/u32 layout")
    elif type_id == 336:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "value"),))
        notes.append("handler-backed DisplayMailScreen single-u32 layout; the handler logs the display-mail-screen message")
    elif type_id in {337, 338}:
        notes.append("client-derived char-transfer/linkdead adjacent empty payload")
    elif type_id == 339:
        start_offset = offset
        offset = decode_char_transfer_base(data, offset, fields, "base")
        if offset != start_offset:
            offset = decode_string16_fields(data, offset, fields, ("text",))
        notes.append("client-derived char-transfer base plus string16 layout")
    elif type_id in {340, 341}:
        offset = decode_two_u32_string16(data, offset, fields)
        notes.append("client-derived two-u32/string16 transfer layout")
    elif type_id == 342:
        offset = decode_three_string16_u64(data, offset, fields)
        notes.append("client-derived three-string16/u64 transfer layout")
    elif type_id == 343:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (1, "u8", "flag_0"),
                (4, "u32", "u32_0"),
                (4, "u32", "u32_1"),
                (1, "u8", "flag_1"),
                (4, "u32", "u32_2"),
            ),
        )
        notes.append("client-derived u8/u32/u32/u8/u32 transfer-reply layout")
    elif type_id in {344, 346}:
        offset = decode_u32_string16(data, offset, fields, "text")
        notes.append("client-derived u32/string16 transfer-adjacent layout")
    elif type_id == 345:
        offset = decode_nested_raw0c_array(data, offset, fields)
        notes.append("handler-backed FlightPaths layout; nested 0x0c entries are route coordinate triplets")
    elif type_id == 347:
        offset = decode_char_transfer_common(data, offset, fields, "transfer")
        notes.append("client-derived char-transfer common layout")
    elif type_id == 348:
        offset = decode_char_transfer_common_and_blobs(
            data, offset, fields, leading_flag=True, blob_count=4
        )
        notes.append("client-derived CharTransferStartReply layout; blob sizes are capped below 0x100001")
    elif type_id == 349:
        offset = decode_char_transfer_common_and_blobs(data, offset, fields, blob_count=4)
        notes.append("client-derived CharTransferRequest layout; blob sizes are capped below 0x100001")
    elif type_id == 350:
        offset = decode_char_transfer_common_and_blobs(data, offset, fields, leading_flag=True)
        notes.append("client-derived char-transfer common plus leading flag layout")
    elif type_id == 351:
        offset = decode_char_transfer_common_and_blobs(data, offset, fields, trailing_flag=True)
        notes.append("client-derived char-transfer common plus trailing flag layout")
    elif type_id == 352:
        offset = decode_char_transfer_common(data, offset, fields, "transfer")
        notes.append("client-derived char-transfer common-only layout")
    elif type_id == 353:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (1, "u8", "flag")),
        )
        notes.append("client-derived u32/u8 transfer-adjacent layout")
    elif type_id == 354:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("client-derived single-u32 transfer-adjacent layout")
    elif type_id == 355:
        offset = decode_char_transfer_envelope(data, offset, fields)
        notes.append("client-derived char-transfer envelope layout")
    elif type_id == 356:
        offset = decode_get_character_serialized_reply(data, offset, fields)
        notes.append("client-derived GetCharacterSerializedReply layout; blob sizes are capped below 0x100001")
    elif type_id == 357:
        offset = decode_create_char_from_cbb_request(data, offset, fields)
        notes.append("client-derived CreateCharFromCBBRequest layout; blob sizes are capped below 0x100001")
    elif type_id == 358:
        offset = decode_char_transfer_validation(data, offset, fields)
        notes.append("client-derived char-transfer validation-shaped layout")
    elif type_id == 359:
        offset = decode_string16_list_u32_count(data, offset, fields)
        notes.append("client-derived HousingDataChanged string-list candidate")
    elif type_id == 360:
        offset = decode_housing_restore(data, offset, fields)
        notes.append("client-derived HousingRestore layout; blob size is capped below 0x100001")
    elif type_id == 361:
        offset = decode_u32_u32_u64_u32_string16(data, offset, fields)
        notes.append("likely AuctionItem by source/PacketParser block order; client layout is u32/u32/u64/u32/string16")
    elif type_id == 362:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (1, "u8", "flag"),
                (4, "u32", "u32_0"),
                (4, "u32", "u32_1"),
                (8, "u64", "u64_0"),
                (4, "u32", "u32_2"),
                (8, "u64", "u64_1"),
            ),
        )
        notes.append("likely AuctionItemReply by source/PacketParser block order; client layout is u8/u32/u32/u64/u32/u64")
    elif type_id == 363:
        offset = decode_u32_u32_u64_string16(data, offset, fields)
        notes.append("likely AuctionCoin by source/PacketParser block order; client layout is u32/u32/u64/string16")
    elif type_id == 364:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (1, "u8", "flag"),
                (4, "u32", "u32_0"),
                (4, "u32", "u32_1"),
                (8, "u64", "u64_0"),
                (8, "u64", "u64_1"),
            ),
        )
        notes.append("likely AuctionCoinReply by source/PacketParser block order; client layout is u8/u32/u32/u64/u64")
    elif type_id == 365:
        offset = decode_u32_u64_u32_u32_string3(data, offset, fields)
        notes.append("likely AuctionCharacter by source/PacketParser block order; client layout is u32/u64/u32/u32/string-u24")
    elif type_id == 366:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (1, "u8", "flag"),
                (4, "u32", "u32_0"),
                (4, "u32", "u32_1"),
                (8, "u64", "u64_0"),
            ),
        )
        notes.append("handler-backed AuctionCharacterReply layout; status byte drives Station Exchange error UI")
    elif type_id == 367:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((8, "u64", "u64_0"), (4, "u32", "u32_0")),
        )
        notes.append("likely AuctionCommitMsg by source/PacketParser block order; client layout is u64/u32")
    elif type_id == 368:
        offset = decode_scalar_layout(data, offset, fields, ((8, "u64", "u64_0"),))
        notes.append("likely AuctionAbortMsg by source/PacketParser block order; client layout is single u64")
    elif type_id == 369:
        offset = decode_char_transfer_common_and_blobs(data, offset, fields, leading_flag=True)
        notes.append("likely CharTransferValidateRequest by source/PacketParser block order after auction; client layout is leading u8 plus char-transfer common")
    elif type_id == 370:
        for index in range(2):
            value = read_u(data, offset, 1)
            if value is None:
                break
            add_field(fields, offset, 1, "u8", f"leading_flag_{index}", value)
            offset += 1
        offset = decode_char_transfer_common(data, offset, fields, "transfer")
        notes.append("likely CharTransferValidateReply by source/PacketParser block order after auction; client layout is two leading u8 values plus char-transfer common")
    elif type_id == 371:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((1, "u8", "flag_0"), (1, "u8", "flag_1")),
        )
        notes.append("likely RaceRestriction by source/PacketParser block order after auction; client layout is two u8 values")
    elif type_id == 372:
        offset = decode_u32_string8(data, offset, fields)
        notes.append("likely SetInstanceDisplayName by source/PacketParser block order after auction; client layout is u32/string8")
    elif type_id == 373:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1")),
        )
        notes.append("likely GetAuctionAssetID by source/PacketParser block order after auction; client layout is two u32 values")
    elif type_id == 374:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (1, "u8", "flag"),
                (8, "u64", "u64_0"),
                (4, "u32", "u32_0"),
                (4, "u32", "u32_1"),
            ),
        )
        notes.append("handler-backed GetAuctionAssetIDReply layout; status byte drives Station Exchange error/success paths")
    elif type_id == 375:
        offset = decode_u32_string16(data, offset, fields, "text")
        notes.append("likely ResendWorldChannels by source/PacketParser block order after auction; client layout is u32/string16")
    elif type_id == 376:
        notes.append("handler-backed DisplayExchangeScreen empty payload routed to the Exchange UI")
    elif type_id == 377:
        offset = decode_u32_u64_string16(data, offset, fields)
        notes.append("likely ArenaGameTypes by source/PacketParser block order after auction; client layout is u32/u64/string16")
    elif type_id == 378:
        offset = decode_eqcmd_grouped_lists(data, offset, fields)
        notes.append("handler-backed EqHearChatCmd grouped-list layout routed through ArenaMain/Eq command helpers")
    elif type_id == 379:
        offset = decode_eqcmd_short_entry_list(data, offset, fields)
        notes.append("handler-backed EqDisplayTextCmd short-entry list routed through ArenaMain")
    elif type_id == 380:
        offset = decode_eqcmd_action_union(data, offset, fields)
        notes.append("handler-backed EqCreateGhostCmd action union routed through ArenaMain; action >2 asserts in client")
    elif type_id == 381:
        offset = decode_eqcmd_two_u32_visual_state(data, offset, fields)
        notes.append("handler-backed EqCreateWidgetCmd two-u32 visual-state layout routed through ArenaMain")
    elif type_id == 382:
        offset = decode_u32_string8_list(data, offset, fields)
        notes.append("handler-backed arena zone/status list layout; source-order EqCreateSignWidget label is provisional")
    elif type_id == 383:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1"), (4, "u32", "u32_2")),
        )
        notes.append("handler-backed EqDestroyGhostCmd three-u32 layout routed through ArenaMain/widget helpers")
    elif type_id == 384:
        offset = decode_eqcmd_widget_envelope(data, offset, fields)
        notes.append("source-order Eq command candidate; client-derived widget envelope")
    elif type_id == 385:
        offset = decode_string16_u32_visual_state(data, offset, fields)
        notes.append("source-order Eq command candidate; client-derived string16/u32 visual-state layout")
    elif type_id == 386:
        offset = decode_eqcmd_complex_visual_list(data, offset, fields)
        notes.append("source-order Eq command candidate; client-derived complex visual-list")
    elif type_id == 387:
        offset = decode_u32_visual_state(data, offset, fields)
        notes.append("source-order Eq command candidate; client-derived u32 visual-state layout")
    elif type_id == 388:
        offset = decode_eqcmd_widget_envelope2(data, offset, fields)
        notes.append("source-order Eq command candidate; client-derived widget envelope variant")
    elif type_id == 389:
        value = read_u(data, offset, 4)
        if value is not None:
            add_field(fields, offset, 4, "u32", "u32_0", value)
            offset = decode_eqcmd_complex_visual_list(data, offset + 4, fields)
        notes.append("handler-backed EqHearSpellInterruptCmd u32 plus complex visual-list layout")
    elif type_id == 390:
        offset = decode_eqcmd_action_switch(data, offset, fields)
        notes.append("handler-backed EqHearSpellFizzleCmd action switch layout")
    elif type_id == 391:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            (
                (4, "u32", "u32_0"),
                (4, "u32", "u32_1"),
                (4, "u32", "u32_2"),
                (1, "u8", "flag"),
                (2, "u16", "u16_0"),
            ),
        )
        notes.append("source-order Eq command candidate; client-derived three-u32/u8/u16 layout")
    elif type_id == 392:
        offset, complete = decode_eqcmd_widget_payload(data, offset, fields, "widget")
        if not complete:
            notes.append("widget payload ended before the fixed subrecord/helper tail completed")
        notes.append("source-order Eq command candidate; client-derived widget-payload-only wrapper")
    elif type_id == 393:
        offset = decode_eqcmd_u32_string8_list(data, offset, fields)
        notes.append("handler-backed EqCreateListBoxCmd u8-count u32/string8 list")
    elif type_id == 394:
        offset = decode_three_u32(data, offset, fields)
        offset, complete = decode_eqcmd_widget_payload(data, offset, fields, "widget")
        if not complete:
            notes.append("widget payload ended before the fixed subrecord/helper tail completed")
        notes.append("source-order Eq command candidate; client-derived three-u32 prefix followed by widget payload")
    elif type_id in {395, 396}:
        offset = decode_scalar_layout(
            data,
            offset,
            fields,
            ((4, "u32", "u32_0"), (4, "u32", "u32_1")),
        )
        notes.append("source-order Eq command candidate; client-derived two-u32 layout")
    elif type_id == 397:
        offset = decode_u32_two_string16(data, offset, fields)
        notes.append("source-order Eq command candidate; client-derived u32 plus two string16 layout")
    elif type_id in {398, 399}:
        notes.append("handler-backed empty Arena UI command row")
    elif type_id == 400:
        offset = decode_scalar_layout(data, offset, fields, ((4, "u32", "u32_0"),))
        notes.append("handler-backed EqHearDrowningCmd single-u32 layout routed to ArenaRevive/KilledByText")
    elif type_id == 401:
        offset = decode_type401_mixed_string_record(data, offset, fields)
        notes.append("likely InviteRequest by source/PacketParser order immediately before Dispatch/DisplayEvent; client layout is mixed string/scalar record")
    elif type_id == 402:
        offset = decode_type401_mixed_string_record(data, offset, fields, include_tail=True)
        notes.append("likely InviteResponse by source/PacketParser order immediately before Dispatch/DisplayEvent; client layout is mixed string/scalar record plus trailing u8/string16")
    elif type_id == 403:
        offset = decode_type403_mixed_scalar_string_record(data, offset, fields)
        notes.append("likely InviteTargetResponse by source/PacketParser order immediately before Dispatch/DisplayEvent; client layout is mixed scalar/string record")
    elif type_id == 404:
        offset = decode_string8_u32_u8(data, offset, fields)
        notes.append("likely InspectPlayerRequest by source/PacketParser order immediately before Dispatch/DisplayEvent; client layout is string8/u32/u8")
    elif type_id == 405:
        offset = decode_dispatch_msg(data, offset, fields)
        notes.append("client-derived VeDispatchMsg layout; raw buffer size is capped below 0x8001")
    elif type_id == 406:
        offset = decode_conditional_string8_list_default(data, offset, fields)
        notes.append(
            "handler-backed DisplayEvent layout; default deserialize object omits the optional pre-mode branch"
        )
    elif type_id == 407:
        notes.append("handler-backed PrePossession empty payload; snapshots possession UI/control state")
    elif type_id == 408:
        notes.append("handler-backed PostPossession empty payload; restores possession UI/control state")
    elif type_id == 409:
        offset = decode_house_items_details(data, offset, fields)
        notes.append("handler-backed HouseItemsDetails layout; item count is capped below 0x4001")
    elif type_id == 410:
        offset = decode_u8_count_u32_id_list(data, offset, fields)
        notes.append("handler-backed compact OP_HouseItemsList id list copied into GameScene house-item state")
    elif type_id in CLIENT_COMPRESSED_LOG_BLOB_TYPES:
        offset, compressed = decode_compressed_blob(packet, data, offset, fields, payload_dir)
        notes.append("client-derived shared compressed log blob layout for types 213-217")
        if opcode_names.get(type_id) and opcode_names[type_id] != opcode_name:
            notes.append(
                f"PacketParser opcode name differs for this client: {opcode_names[type_id]}"
            )
    elif type_id in CLIENT_DERIVED_PACKETPARSER_DRIFT_IDS:
        notes.append("client-derived opcode name/layout; PacketParser v546 names drift in this range")
    elif type_id in CLIENT_DERIVED_OPCODE_NAMES:
        notes.append("client-derived opcode name; no client-specific decoder yet")
    else:
        offset, struct_meta = decode_packetparser_struct(
            data, offset, type_id, fields, structs_by_opcode, structs_by_name
        )

    if struct_meta:
        struct_label = str(struct_meta.get("name") or "unknown")
        opcode_label = str(struct_meta.get("opcode_name") or opcode_name or "")
        note = f"PacketParser struct: {struct_label}"
        if opcode_label:
            note += f" / {opcode_label}"
        if struct_meta.get("candidate_count", 0) > 1:
            note += f" ({struct_meta['candidate_count']} candidate structs; selected first non-subopcode struct)"
        notes.append(note)

    if offset < len(data):
        add_field(fields, offset, len(data) - offset, "bytes", "remaining_unparsed", data[offset:].hex())

    return {
        "session": packet.session,
        "seq": packet.seq,
        "direction": packet.direction,
        "type_id": packet.type_id,
        "type_name": packet.type_name,
        "opcode_name": opcode_name,
        "length": len(data),
        "body_hex": packet.body_hex,
        "decoded_fields": fields,
        "packetparser_struct": struct_meta,
        "compressed": compressed,
        "trace_fields": trace_fields(packet, data),
        "string_events": packet.string_events,
        "notes": notes,
    }


def field_value_text(value: Any) -> str:
    if isinstance(value, str):
        if len(value) > 160:
            return f"`{value[:160]}...`"
        return f"`{value}`"
    if isinstance(value, (dict, list)):
        text = json.dumps(value, ensure_ascii=True)
        if len(text) > 160:
            text = f"{text[:160]}..."
        return f"`{text}`"
    return f"`{value}`"


def write_markdown(path: Path, decoded: list[dict[str, Any]]) -> None:
    lines = ["# EQ2 Decoded Packet Log", ""]
    lines.append("This report decodes packet bodies from `packet_clear.body_hex` records.")
    lines.append("")

    for item in decoded:
        display_name = item.get("opcode_name") or item.get("type_name") or ""
        title_name = f" {display_name}" if display_name else ""
        lines.append(
            f"## Session {item['session']} Seq {item['seq']} {item['direction']} "
            f"Type {item['type_id']}{title_name}"
        )
        lines.append("")
        lines.append(f"- length: `{item['length']}`")
        if item["notes"]:
            for note in item["notes"]:
                lines.append(f"- note: {note}")
        lines.append("")
        lines.append("| Offset | Size | Kind | Name | Value |")
        lines.append("| ---: | ---: | --- | --- | --- |")
        for field in item["decoded_fields"]:
            lines.append(
                f"| `0x{field['offset']:04x}` | `{field['size']}` | `{field['kind']}` | "
                f"`{field['name']}` | {field_value_text(field['value'])} |"
            )
        lines.append("")

        compressed = item.get("compressed")
        if compressed:
            lines.append("### Decompressed Payload")
            lines.append("")
            lines.append(f"- zlib ok: `{compressed.get('zlib_ok')}`")
            lines.append(f"- actual size: `{compressed.get('decompressed_size_actual')}`")
            lines.append(f"- size matches header: `{compressed.get('size_matches')}`")
            if compressed.get("txt_path"):
                lines.append(f"- text: `{compressed['txt_path']}`")
            if compressed.get("bin_path"):
                lines.append(f"- binary: `{compressed['bin_path']}`")
            preview = compressed.get("preview") or []
            if preview:
                lines.append("")
                lines.append("```text")
                lines.extend(str(line)[:240] for line in preview)
                lines.append("```")
            lines.append("")

        if item["trace_fields"]:
            lines.append("### Trace Fields")
            lines.append("")
            lines.append("| Event | Offset | Size | Hex | Value |")
            lines.append("| --- | ---: | ---: | --- | --- |")
            for row in item["trace_fields"][:80]:
                value = row.get("ascii")
                if value is None:
                    value = row.get("summary", row.get("u", ""))
                lines.append(
                    f"| `{row['event']}` | `0x{row['offset']:04x}` | `{row['size']}` | "
                    f"`{row['hex'][:64]}` | `{value}` |"
                )
            lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path, help="Frida JSONL packet logs")
    parser.add_argument(
        "--all-sessions",
        action="store_true",
        help="Decode every appended Frida session instead of only the latest session",
    )
    parser.add_argument(
        "--out-md",
        type=Path,
        default=Path("artifacts/eq2_packet_decoded.md"),
        help="Markdown output path",
    )
    parser.add_argument(
        "--out-json",
        type=Path,
        default=Path("artifacts/eq2_packet_decoded.json"),
        help="JSON output path",
    )
    parser.add_argument(
        "--payload-dir",
        type=Path,
        default=Path("artifacts/eq2_packet_decoded_payloads"),
        help="Directory for decompressed payload files",
    )
    parser.add_argument(
        "--opcodes",
        type=Path,
        default=Path("artifacts/packetparser_metadata/opcodes_v546.csv"),
        help="Optional PacketParser opcode CSV from extract_packetparser_metadata.py",
    )
    parser.add_argument(
        "--structs",
        type=Path,
        default=Path("artifacts/packetparser_metadata/structs_by_opcode_v546.json"),
        help="Optional PacketParser struct JSON from extract_packetparser_metadata.py",
    )
    args = parser.parse_args()

    packets = build_packets(iter_records(args.logs))
    if not packets:
        print("No packet_clear records found.")
        return 1

    latest_session = max(session for session, _ in packets)
    selected = [
        packet
        for (session, _), packet in sorted(packets.items())
        if args.all_sessions or session == latest_session
    ]

    opcode_names = load_opcode_names(args.opcodes)
    structs_by_opcode, structs_by_name = load_packetparser_structs(args.structs)
    decoded = [
        item
        for item in (
            decode_packet(
                packet,
                args.payload_dir,
                opcode_names,
                structs_by_opcode,
                structs_by_name,
            )
            for packet in selected
        )
        if item is not None
    ]

    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(decoded, indent=2), encoding="utf-8")
    write_markdown(args.out_md, decoded)

    print(f"Decoded {len(decoded)} packet bodies from session {latest_session}.")
    print(f"Wrote Markdown: {args.out_md}")
    print(f"Wrote JSON: {args.out_json}")
    print(f"Wrote payloads: {args.payload_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
