/*
 * EQ2 2006 client packet logger prototype.
 *
 * Usage, from an elevated shell if the client requires it:
 *   frida -n EverQuest2.exe -l scripts/frida_eq2_2006_packet_logger.js
 *
 * This logs JSON lines to the Frida console and, when possible, also to
 * LOG_PATH in the target process working directory.
 */

"use strict";

const MODULE_NAME = "EverQuest2.exe";
const GHIDRA_IMAGE_BASE = 0x00400000;

const LOG_TO_FILE = true;
const LOG_PATH = "eq2_packet_log.jsonl";
const MAX_PACKET_BYTES = 16 * 1024;
const MAX_FIELD_BYTES = 256;
const MAX_STRING_CHARS = 4096;

/*
 * Stability switches:
 *
 * Interior hooks attach to instructions inside larger functions. They are very
 * useful, but are also the first thing to disable when a fragile old client
 * terminates under instrumentation. Start with the safe defaults below, then
 * turn these on one at a time.
 */
const ENABLE_STREAM_PACKET_BODY_LOG = true;
const ENABLE_DANGEROUS_INTERIOR_PACKET_BODY_HOOKS = false;
const ENABLE_DANGEROUS_INTERIOR_MESSAGE_CALL_HOOKS = false;
const ENABLE_FIELD_TRACE = true;
const ENABLE_STRING_TRACE = true;

const ADDR = {
  incomingClearBody: 0x0043bac9,
  outgoingClearBody: 0x0043ad9f,
  deserializeRoot: 0x008fc5c8,
  serializeRoot: 0x008fc581,
  beforeDeserializeCall: 0x008fc60f,
  beforeSerializeCall: 0x008fc5ac,
  readRaw: 0x0075ab32,
  writeRaw: 0x0075ad7e,
  readPackedType: 0x0075e670,
  writePackedType: 0x0075e625,
  readStringLenN: 0x00472240,
  writeStringLenN: 0x00472380,
};

const KNOWN_TYPE_NAMES = {
  4: "OP_LoginReplyMsg",
  6: "OP_WorldStatusChangeMsg",
  8: "OP_WorldListMsg",
  10: "OP_AllCharactersDescReplyMsg",
  12: "OP_CreateCharacterReplyMsg",
  17: "OP_DeleteCharacterReplyMsg",
  19: "OP_PlayCharacterReplyMsg",
  22: "VeESInitMsg",
  29: "OP_ZoneInfoMsg",
  31: "OP_DoneSendingInitialEntitiesMsg",
  32: "OP_DoneLoadingZoneResourcesMsg",
  33: "OP_DoneLoadingUIResourcesMsg",
  34: "VePredictionUpdateMsg",
  35: "OP_SetRemoteCmdsMsg",
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
  50: "OP_ClientCmdMsg",
  51: "VeDispatchClientCmdMsg",
  52: "VeDispatchESMsg",
  55: "VeUpdateCharacterSheetMsg",
  56: "VeUpdateSpellBookMsg",
  58: "OP_UpdateInventoryMsg",
  59: "OP_AfterInvSpellUpdate",
  60: "VeUpdateRecipeBookMsg",
  61: "VeRequestRecipeDetailsMsg",
  62: "VeRecipeDetailsMsg",
  63: "VeUpdateSkillsMsg",
  64: "VeUpdateSkillsMsg",
  65: "VeUpdateOpportunityMsg",
  67: "OP_ChangeZoneMsg",
  69: "OP_TeleportWithinZoneMsg",
  70: "OP_TeleportWithinZoneNoReloadMsg",
  89: "OP_ClearDataMsg",
  90: "OP_ESZoneInstanceStatusMsg",
  94: "OP_ZonesStatusMsg",
  103: "OP_UnresolvedQuestJournalSelectionType103Msg",
  104: "VeQuestJournalSetVisibleMsg",
  110: "OP_GuildUpdateMsg",
  117: "VePurchaseConsignmentResponseMsg",
  123: "OP_PlayerHouseAccessUpdateMsg",
  124: "OP_PlayerHouseDisplayStatusMsg",
  125: "OP_PlayerHouseCloseUIMsg",
  126: "OP_BuyPlayerHouseStatusMsg",
  127: "OP_BuyPlayerHouseTintMsg",
  128: "OP_BuyPlayerHouseMsg",
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
  150: "VeExamineConsignmentResponseMsg",
  151: "OP_UISettingsResponseMsg",
  153: "OP_KeymapLoadMsg",
  154: "OP_KeymapNoneMsg",
  155: "VeKeymapDataMsg",
  156: "VeKeymapSaveMsg",
  157: "VeDispatchSpellCmdMsg",
  160: "OP_EntityVerbsReplyMsg",
  162: "VeChatRelationshipUpdateMsg",
  164: "OP_StoppedLootingMsg",
  165: "OP_SitMsg",
  166: "OP_StandMsg",
  169: "OP_ClearForTakeOffMsg",
  170: "OP_ReadyForTakeOffMsg",
  176: "OP_DefaultGroupOptionsMsg",
  178: "OP_DisplayGroupOptionsScreenMsg",
  179: "VeDisplayInnVisitScreenMsg",
  180: "VeDumpSchedulerMsg",
  187: "OP_PerformPlayerKnockbackMsg",
  188: "OP_PerformCameraShakeMsg",
  189: "VePopulateSkillMapsMsg",
  192: "OP_ShowCreateFromRecipeUIMsg",
  193: "OP_CancelCreateFromRecipeMsg",
  195: "OP_StopItemCreationMsg",
  196: "OP_ShowItemCreationProcessUIMsg",
  197: "OP_UpdateItemCreationProcessUIMsg",
  198: "OP_DisplayTSEventReactionMsg",
  199: "VeShowRecipeBookMsg",
  201: "VeKnowledgebaseResponseMsg",
  203: "VeCSTicketInfoMsg",
  205: "VeCSTicketCommentResponseMsg",
  209: "OP_CSTicketChangeNotificationMsg",
  211: "OP_KnownLanguagesMsg",
  213: "VeLsClientBaselogReplyMsg",
  214: "OP_LsClientCrashlogReplyMsg",
  215: "OP_LsClientEq2CrashLogReplyMsg",
  216: "OP_LsClientAlertlogReplyMsg",
  217: "OP_LsClientVerifylogReplyMsg",
  219: "OP_UpdateClientPredFlagsMsg",
  220: "OP_ChangeServerControlFlagMsg",
  229: "VeExamineInfoRequestMsg",
  230: "VeQuickbarInitMsg",
  232: "VeMacroInitMsg",
  235: "OP_LevelChangedMsg",
  236: "OP_DisplayWarningMsg",
  237: "OP_EncounterBrokenMsg",
  238: "OP_OnscreenMsgMsg",
  239: "OP_ModifyGuildMsg",
  242: "OP_GuildEventAddMsg",
  243: "OP_GuildEventActionMsg",
  244: "OP_GuildEventListMsg",
  246: "OP_RequestGuildInfoMsg",
  248: "OP_GuildBankActionMsg",
  252: "VeGuildBankUpdateMsg",
  253: "OP_GuildBankEventListMsg",
  255: "OP_RewardPackMsg",
  272: "OP_MailSendMessageMsg",
  274: "OP_MailGetHeadersReplyMsg",
  275: "OP_MailGetMessageReplyMsg",
  276: "OP_MailSendMessageReplyMsg",
  277: "OP_MailCommitSendMessageMsg",
  278: "OP_MailSendSystemMessageMsg",
  279: "OP_MailRemoveAttachFromMailMsg",
  281: "VeWaypointReplyMsg",
  282: "OP_WaypointSelectMsg",
  283: "VeWaypointUpdateMsg",
  285: "VeShowZoneTeleporterDestinationsMsg",
  289: "VeGuildMembershipResponseMsg",
  290: "OP_LeaveGuildNotifyMsg",
  291: "OP_JoinGuildNotifyMsg",
  293: "OP_BioUpdateMsg",
  294: "OP_QuestReward",
  299: "VeCsCategoryResponseMsg",
  300: "OP_KnowledgeWindowSlotMappingMsg",
  301: "OP_LFGUpdateMsg",
  304: "VeUpdateActivePublicZonesMsg",
  306: "VePromoFlagsDetailsMsg",
  307: "VeConsignViewCreateMsg",
  314: "VeUpdateRaidMsg",
  315: "VeUpdateArenaMsg",
  317: "OP_TitleUpdateMsg",
  323: "OP_TrackingUpdateMsg",
  327: "OP_AdvancementRequestMsg",
  328: "OP_MapFogDataInitMsg",
  329: "OP_MapFogDataUpdateMsg",
  330: "OP_CloseGroupInviteWindowMsg",
  333: "OP_MailEventNotificationMsg",
  334: "OP_OfferQuestMsg",
  336: "OP_DisplayMailScreenMsg",
  345: "OP_FlightPathsMsg",
  348: "VeCharTransferStartReplyMsg",
  349: "VeCharTransferRequestMsg",
  356: "VeGetCharacterSerializedReplyMsg",
  357: "VeCreateCharFromCBBRequestMsg",
  360: "VeHousingRestoreMsg",
  366: "OP_AuctionCharacterReply",
  374: "OP_GetAuctionAssetIDReplyMsg",
  376: "OP_DisplayExchangeScreenMsg",
  378: "OP_EqHearChatCmd",
  379: "OP_EqDisplayTextCmd",
  380: "OP_EqCreateGhostCmd",
  381: "OP_EqCreateWidgetCmd",
  382: "OP_EqCreateSignWidgetCmd",
  383: "OP_EqDestroyGhostCmd",
  389: "OP_EqHearSpellInterruptCmd",
  390: "OP_EqHearSpellFizzleCmd",
  393: "OP_EqCreateListBoxCmd",
  398: "OP_EqPlaySound3DCmd",
  399: "OP_EqPlayVoiceCmd",
  400: "OP_EqHearDrowningCmd",
  405: "VeDispatchMsg",
  406: "OP_DisplayEventMsg",
  407: "OP_PrePossessionMsg",
  408: "OP_PostPossessionMsg",
  409: "VeHouseItemsDetailsMsg",
  410: "OP_HouseItemsList",
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
};

if (Process.arch !== "ia32") {
  throw new Error(`Expected 32-bit x86 process, got ${Process.arch}`);
}

const mainModule = Process.getModuleByName(MODULE_NAME);
let logFile = null;
let nextPacketSeq = 1;
const contextByThread = {};
const pendingIncomingClearByThread = {};
const lastSerializedByThread = {};

if (LOG_TO_FILE) {
  try {
    logFile = new File(LOG_PATH, "a");
  } catch (e) {
    console.error(`[eq2-packet-log] file logging disabled: ${e}`);
  }
}

function va(ghidraVa) {
  return mainModule.base.add(ghidraVa - GHIDRA_IMAGE_BASE);
}

function threadId() {
  return Process.getCurrentThreadId().toString();
}

function nowIso() {
  return new Date().toISOString();
}

function emit(record) {
  record.time = nowIso();
  const line = JSON.stringify(record);
  console.log(line);
  if (logFile !== null) {
    try {
      logFile.write(line + "\n");
      logFile.flush();
    } catch (e) {
      console.error(`[eq2-packet-log] file write failed: ${e}`);
      logFile = null;
    }
  }
}

function stackPtr(ctx, index) {
  return ptr(ctx.esp)
    .add(4 + index * 4)
    .readPointer();
}

function stackU32(ctx, index) {
  return ptr(ctx.esp)
    .add(4 + index * 4)
    .readU32();
}

function safeHex(address, length, maxLength) {
  const size = Math.max(0, Math.min(length >>> 0, maxLength >>> 0));
  if (size === 0) {
    return "";
  }
  try {
    const p = ptr(address);
    let raw = null;

    if (typeof p.readByteArray === "function") {
      raw = p.readByteArray(size);
    } else if (typeof Memory.readByteArray === "function") {
      raw = Memory.readByteArray(p, size);
    } else {
      throw new TypeError("readByteArray is unavailable in this Frida runtime");
    }

    if (raw === null) {
      return "<unreadable:null>";
    }

    const bytes = new Uint8Array(raw);
    let out = "";
    for (let i = 0; i < bytes.length; i++) {
      out += bytes[i].toString(16).padStart(2, "0");
    }
    return out;
  } catch (e) {
    return `<unreadable:${e}>`;
  }
}

function safeCString(address) {
  try {
    if (ptr(address).isNull()) {
      return null;
    }
    return ptr(address).readCString();
  } catch (_) {
    return null;
  }
}

function safeUtf8(address, maxChars) {
  try {
    if (ptr(address).isNull()) {
      return null;
    }
    return ptr(address).readUtf8String(maxChars);
  } catch (_) {
    return null;
  }
}

function streamState(stream) {
  try {
    const streamPtr = ptr(stream);
    const buffer = streamPtr.add(4).readPointer();
    return {
      stream: streamPtr.toString(),
      buffer: buffer.toString(),
      data: buffer.add(4).readPointer(),
      size: buffer.add(8).readU32(),
      capacity: buffer.add(12).readU32(),
      position: buffer.add(16).readU32(),
    };
  } catch (_) {
    return null;
  }
}

function isLikelyStream(stream) {
  const state = streamState(stream);
  return (
    state !== null && !state.data.isNull() && state.capacity < 1024 * 1024 * 32
  );
}

function pickStream(ctx, stackIndex) {
  const ecx = ptr(ctx.ecx);
  if (!ecx.isNull() && isLikelyStream(ecx)) {
    return ecx;
  }

  try {
    const candidate = stackPtr(ctx, stackIndex);
    if (!candidate.isNull() && isLikelyStream(candidate)) {
      return candidate;
    }
  } catch (_) {}

  return ecx;
}

function decodePackedTypeAt(address, length) {
  try {
    if (length < 1) {
      return null;
    }
    const first = ptr(address).readU8();
    if (first !== 0xff) {
      return { type_id: first, type_id_size: 1 };
    }
    if (length < 3) {
      return null;
    }
    return { type_id: ptr(address).add(1).readU16(), type_id_size: 3 };
  } catch (_) {
    return null;
  }
}

function hasDecodedType(decoded) {
  return decoded.type_id !== undefined && decoded.type_id !== null;
}

function packetLengthFromStream(state, lengthHint) {
  if (lengthHint !== undefined && lengthHint !== null && lengthHint >= 0) {
    return Math.min(lengthHint >>> 0, state.size >>> 0);
  }
  return state.size >>> 0;
}

function emitPacketBodyFromStream(ctx, stream, lengthHint) {
  if (!ENABLE_STREAM_PACKET_BODY_LOG) {
    return;
  }

  const state = streamState(stream);
  if (state === null || state.data.isNull()) {
    emit({
      event: "packet_clear_unavailable",
      seq: ctx.seq,
      direction: ctx.direction,
      reason: "missing_stream_buffer",
      stream: ptr(stream).toString(),
    });
    return;
  }

  const length = packetLengthFromStream(state, lengthHint);
  const decoded = decodePackedTypeAt(state.data, length) || {};
  const typeId = hasDecodedType(decoded) ? decoded.type_id : ctx.type_id;
  const hasType = typeId !== undefined && typeId !== null;
  if (hasType) {
    setContextType(ctx, typeId);
  }

  emit({
    event: "packet_clear",
    seq: ctx.seq,
    direction: ctx.direction,
    source: "stream",
    stream: ptr(stream).toString(),
    body: state.data.toString(),
    length,
    stream_size: state.size,
    stream_position: state.position,
    type_id: hasType ? typeId : null,
    type_name: hasType ? KNOWN_TYPE_NAMES[typeId] || null : null,
    type_id_size: decoded.type_id_size || null,
    body_hex: safeHex(state.data, length, MAX_PACKET_BYTES),
    truncated: length > MAX_PACKET_BYTES,
  });
}

function pointerDelta(address, base) {
  return ptr(address).toUInt32() - ptr(base).toUInt32();
}

function valueSummary(address, size) {
  const p = ptr(address);
  try {
    if (size === 1) {
      return { u8: p.readU8() };
    }
    if (size === 2) {
      return { u16: p.readU16() };
    }
    if (size === 4) {
      return { u32: p.readU32(), i32: p.readS32(), f32: p.readFloat() };
    }
    if (size === 8) {
      return { u64_hex: safeHex(p, 8, 8), f64: p.readDouble() };
    }
  } catch (_) {}
  return { bytes_hex: safeHex(p, size, MAX_FIELD_BYTES) };
}

function currentContext() {
  const stack = contextByThread[threadId()];
  if (stack === undefined || stack.length === 0) {
    return null;
  }
  return stack[stack.length - 1];
}

function pushContext(ctx) {
  const tid = threadId();
  if (contextByThread[tid] === undefined) {
    contextByThread[tid] = [];
  }
  contextByThread[tid].push(ctx);
}

function popContext(expected) {
  const tid = threadId();
  const stack = contextByThread[tid];
  if (stack === undefined || stack.length === 0) {
    return;
  }
  const popped = stack.pop();
  if (expected !== undefined && popped.seq !== expected.seq) {
    emit({
      event: "context_stack_mismatch",
      expected_seq: expected.seq,
      popped_seq: popped.seq,
    });
  }
}

function setContextType(ctx, typeId) {
  if (ctx === null || typeId === null || typeId === undefined) {
    return;
  }
  if (ctx.type_id === null) {
    ctx.type_id = typeId >>> 0;
    ctx.type_name = KNOWN_TYPE_NAMES[ctx.type_id] || null;
  }
}

function objectOffset(ctx, address) {
  if (ctx === null || ctx.object === null) {
    return null;
  }
  const delta = pointerDelta(address, ctx.object);
  if (delta >= 0 && delta < 0x10000) {
    return delta;
  }
  return null;
}

function attach(label, ghidraVa, callbacks) {
  const target = va(ghidraVa);
  const wrapped = {};

  if (callbacks.onEnter !== undefined) {
    wrapped.onEnter = function wrappedOnEnter(args) {
      try {
        return callbacks.onEnter.call(this, args);
      } catch (e) {
        emit({
          event: "hook_error",
          hook: label,
          phase: "onEnter",
          error: String(e),
          thread_id: threadId(),
        });
      }
    };
  }

  if (callbacks.onLeave !== undefined) {
    wrapped.onLeave = function wrappedOnLeave(retval) {
      try {
        return callbacks.onLeave.call(this, retval);
      } catch (e) {
        emit({
          event: "hook_error",
          hook: label,
          phase: "onLeave",
          error: String(e),
          thread_id: threadId(),
        });
      }
    };
  }

  Interceptor.attach(target, wrapped);
  console.error(
    `[eq2-packet-log] hooked ${label} at ${target} (Ghidra ${ptr(ghidraVa)})`,
  );
}

function attachIf(enabled, label, ghidraVa, callbacks) {
  if (!enabled) {
    console.error(
      `[eq2-packet-log] skipped ${label} at Ghidra ${ptr(ghidraVa)}`,
    );
    return;
  }

  attach(label, ghidraVa, callbacks);
}

attachIf(
  ENABLE_DANGEROUS_INTERIOR_PACKET_BODY_HOOKS,
  "incoming clear body",
  ADDR.incomingClearBody,
  {
    onEnter() {
      const body = ptr(this.context.esi);
      const length = ptr(this.context.edi).toUInt32();
      const decoded = decodePackedTypeAt(body, length) || {};
      const seq = nextPacketSeq++;

      pendingIncomingClearByThread[threadId()] = {
        seq,
        length,
        type_id: hasDecodedType(decoded) ? decoded.type_id : null,
      };

      emit({
        event: "packet_clear",
        seq,
        direction: "incoming",
        body: body.toString(),
        length,
        type_id: hasDecodedType(decoded) ? decoded.type_id : null,
        type_name: hasDecodedType(decoded)
          ? KNOWN_TYPE_NAMES[decoded.type_id] || null
          : null,
        type_id_size: decoded.type_id_size || null,
        body_hex: safeHex(body, length, MAX_PACKET_BYTES),
        truncated: length > MAX_PACKET_BYTES,
      });
    },
  },
);

attachIf(
  ENABLE_DANGEROUS_INTERIOR_PACKET_BODY_HOOKS,
  "outgoing clear body",
  ADDR.outgoingClearBody,
  {
    onEnter() {
      const body = ptr(this.context.eax);
      const length = ptr(this.context.edi).toUInt32();
      const decoded = decodePackedTypeAt(body, length) || {};
      const previous = lastSerializedByThread[threadId()] || {};
      const seq = previous.seq || nextPacketSeq++;
      const typeId =
        decoded.type_id !== undefined ? decoded.type_id : previous.type_id;
      const hasType = typeId !== undefined && typeId !== null;

      emit({
        event: "packet_clear",
        seq,
        direction: "outgoing",
        body: body.toString(),
        length,
        type_id: hasType ? typeId : null,
        type_name: hasType ? KNOWN_TYPE_NAMES[typeId] || null : null,
        type_id_size: decoded.type_id_size || null,
        client_net: ptr(this.context.esi).toString(),
        body_hex: safeHex(body, length, MAX_PACKET_BYTES),
        truncated: length > MAX_PACKET_BYTES,
      });
    },
  },
);

attach("deserialize root", ADDR.deserializeRoot, {
  onEnter() {
    const pending = pendingIncomingClearByThread[threadId()] || null;
    const stream = pickStream(this.context, 0);
    const ctx = {
      seq: pending !== null ? pending.seq : nextPacketSeq++,
      direction: "incoming",
      stream: stream.toString(),
      object: null,
      metadata: null,
      metadata_name: null,
      type_id: pending !== null ? pending.type_id : null,
      type_name:
        pending !== null && pending.type_id !== null
          ? KNOWN_TYPE_NAMES[pending.type_id] || null
          : null,
      root: "VeType_DeserializeObjectFromStream_Maybe",
      field_count: 0,
    };
    delete pendingIncomingClearByThread[threadId()];
    pushContext(ctx);
    emit({
      event: "packet_context_begin",
      seq: ctx.seq,
      direction: ctx.direction,
      stream: ctx.stream,
    });
    emitPacketBodyFromStream(ctx, stream, null);
    this.ctx = ctx;
  },
  onLeave(retval) {
    const ctx = this.ctx;
    emit({
      event: "packet_context_end",
      seq: ctx.seq,
      direction: ctx.direction,
      type_id: ctx.type_id,
      type_name: ctx.type_name,
      metadata_name: ctx.metadata_name,
      object: ctx.object,
      return_value: ptr(retval).toString(),
      field_count: ctx.field_count,
    });
    popContext(ctx);
  },
});

attach("serialize root", ADDR.serializeRoot, {
  onEnter() {
    const object = stackPtr(this.context, 0);
    const stream = stackPtr(this.context, 1);
    const ctx = {
      seq: nextPacketSeq++,
      direction: "outgoing",
      stream: stream.toString(),
      object: object.toString(),
      metadata: null,
      metadata_name: null,
      type_id: null,
      type_name: null,
      root: "VeType_SerializeObjectToStream_Maybe",
      field_count: 0,
    };
    pushContext(ctx);
    emit({
      event: "packet_context_begin",
      seq: ctx.seq,
      direction: ctx.direction,
      stream: ctx.stream,
      object: ctx.object,
    });
    this.ctx = ctx;
  },
  onLeave(retval) {
    const ctx = this.ctx;
    const state = streamState(ctx.stream);
    const length = state !== null ? state.position : ptr(retval).toInt32();

    lastSerializedByThread[threadId()] = {
      seq: ctx.seq,
      type_id: ctx.type_id,
      type_name: ctx.type_name,
    };
    emitPacketBodyFromStream(ctx, ctx.stream, length);
    emit({
      event: "packet_context_end",
      seq: ctx.seq,
      direction: ctx.direction,
      type_id: ctx.type_id,
      type_name: ctx.type_name,
      metadata_name: ctx.metadata_name,
      object: ctx.object,
      bytes_written: ptr(retval).toInt32(),
      field_count: ctx.field_count,
    });
    popContext(ctx);
  },
});

attachIf(
  ENABLE_DANGEROUS_INTERIOR_MESSAGE_CALL_HOOKS,
  "before concrete deserialize call",
  ADDR.beforeDeserializeCall,
  {
    onEnter() {
      const ctx = currentContext();
      if (ctx === null) {
        return;
      }

      const typeId = ptr(this.context.ebp).sub(4).readU32() & 0xffff;
      const meta = ptr(this.context.esi);
      const object = ptr(this.context.edi);
      let metadataName = null;

      try {
        metadataName = safeCString(meta.add(0x0c).readPointer());
      } catch (_) {}

      setContextType(ctx, typeId);
      ctx.object = object.toString();
      ctx.metadata = meta.toString();
      ctx.metadata_name = metadataName;

      emit({
        event: "message_deserialize_call",
        seq: ctx.seq,
        type_id: ctx.type_id,
        type_name: ctx.type_name,
        metadata_name: ctx.metadata_name,
        object: ctx.object,
        metadata: ctx.metadata,
      });
    },
  },
);

attachIf(
  ENABLE_DANGEROUS_INTERIOR_MESSAGE_CALL_HOOKS,
  "before concrete serialize call",
  ADDR.beforeSerializeCall,
  {
    onEnter() {
      const ctx = currentContext();
      if (ctx === null) {
        return;
      }

      ctx.object = ptr(this.context.edi).toString();
      ctx.stream = ptr(this.context.esi).toString();

      emit({
        event: "message_serialize_call",
        seq: ctx.seq,
        type_id: ctx.type_id,
        type_name: ctx.type_name,
        object: ctx.object,
        stream: ctx.stream,
      });
    },
  },
);

attach("read packed type", ADDR.readPackedType, {
  onEnter() {
    this.ctx = currentContext();
    this.stream = ptr(this.context.ecx);
    this.out = stackPtr(this.context, 0);
  },
  onLeave(retval) {
    if (this.ctx === null || ptr(retval).toInt32() < 0) {
      return;
    }
    try {
      const typeId = this.out.readU16();
      const firstTypeForContext = this.ctx.type_id === null;
      setContextType(this.ctx, typeId);
      emit({
        event: "packed_type_read",
        seq: this.ctx.seq,
        direction: this.ctx.direction,
        stream: this.stream.toString(),
        type_id: typeId,
        type_name: KNOWN_TYPE_NAMES[typeId] || null,
        context_type_set: firstTypeForContext,
      });
    } catch (_) {}
  },
});

attach("write packed type", ADDR.writePackedType, {
  onEnter() {
    const ctx = currentContext();
    if (ctx === null) {
      return;
    }
    const typeId = stackU32(this.context, 0) & 0xffff;
    const firstTypeForContext = ctx.type_id === null;
    setContextType(ctx, typeId);
    emit({
      event: "packed_type_write",
      seq: ctx.seq,
      direction: ctx.direction,
      stream: ptr(this.context.ecx).toString(),
      type_id: typeId,
      type_name: KNOWN_TYPE_NAMES[typeId] || null,
      context_type_set: firstTypeForContext,
    });
  },
});

attachIf(ENABLE_FIELD_TRACE, "raw field read", ADDR.readRaw, {
  onEnter() {
    const ctx = currentContext();
    if (ctx === null) {
      this.skip = true;
      return;
    }

    this.skip = false;
    this.ctx = ctx;
    this.stream = ptr(this.context.ecx);
    this.dst = stackPtr(this.context, 0);
    this.size = stackU32(this.context, 1);
    this.strict = stackU32(this.context, 2) & 0xff;
    const state = streamState(this.stream);
    this.offset = state !== null ? state.position : null;
  },
  onLeave(retval) {
    if (this.skip) {
      return;
    }

    const got = ptr(retval).toInt32();
    const size = this.size >>> 0;
    this.ctx.field_count++;
    emit({
      event: "field_read",
      seq: this.ctx.seq,
      direction: this.ctx.direction,
      type_id: this.ctx.type_id,
      type_name: this.ctx.type_name,
      metadata_name: this.ctx.metadata_name,
      stream: this.stream.toString(),
      offset: this.offset,
      size,
      returned: got,
      strict: this.strict,
      dst: this.dst.toString(),
      object_offset: objectOffset(this.ctx, this.dst),
      value: got > 0 ? valueSummary(this.dst, Math.min(got, size)) : null,
      bytes_hex:
        got > 0 ? safeHex(this.dst, Math.min(got, size), MAX_FIELD_BYTES) : "",
    });
  },
});

attachIf(ENABLE_FIELD_TRACE, "raw field write", ADDR.writeRaw, {
  onEnter() {
    const ctx = currentContext();
    if (ctx === null) {
      this.skip = true;
      return;
    }

    this.skip = false;
    this.ctx = ctx;
    this.stream = ptr(this.context.ecx);
    this.src = stackPtr(this.context, 0);
    this.size = stackU32(this.context, 1);
    const state = streamState(this.stream);
    this.offset = state !== null ? state.position : null;
  },
  onLeave(retval) {
    if (this.skip) {
      return;
    }

    const wrote = ptr(retval).toInt32();
    const size = this.size >>> 0;
    this.ctx.field_count++;
    emit({
      event: "field_write",
      seq: this.ctx.seq,
      direction: this.ctx.direction,
      type_id: this.ctx.type_id,
      type_name: this.ctx.type_name,
      stream: this.stream.toString(),
      offset: this.offset,
      size,
      returned: wrote,
      src: this.src.toString(),
      object_offset: objectOffset(this.ctx, this.src),
      value: valueSummary(this.src, size),
      bytes_hex: safeHex(this.src, size, MAX_FIELD_BYTES),
    });
  },
});

attachIf(ENABLE_STRING_TRACE, "string read", ADDR.readStringLenN, {
  onEnter() {
    const ctx = currentContext();
    if (ctx === null) {
      this.skip = true;
      return;
    }

    this.skip = false;
    this.ctx = ctx;
    this.stream = ptr(this.context.ecx);
    this.target = stackPtr(this.context, 0);
    this.lengthBytes = stackU32(this.context, 1);
    const state = streamState(this.stream);
    this.offset = state !== null ? state.position : null;
  },
  onLeave(retval) {
    if (this.skip) {
      return;
    }

    let text = null;
    try {
      const begin = this.target.add(8).readPointer();
      const end = this.target.add(12).readPointer();
      const length = Math.max(
        0,
        Math.min(pointerDelta(end, begin), MAX_STRING_CHARS),
      );
      text = safeUtf8(begin, length);
    } catch (_) {}

    emit({
      event: "string_read",
      seq: this.ctx.seq,
      direction: this.ctx.direction,
      type_id: this.ctx.type_id,
      type_name: this.ctx.type_name,
      stream: this.stream.toString(),
      offset: this.offset,
      length_bytes: this.lengthBytes,
      returned: ptr(retval).toInt32(),
      target: this.target.toString(),
      object_offset: objectOffset(this.ctx, this.target),
      text,
    });
  },
});

attachIf(ENABLE_STRING_TRACE, "string write", ADDR.writeStringLenN, {
  onEnter() {
    const ctx = currentContext();
    if (ctx === null) {
      return;
    }

    const stream = ptr(this.context.ecx);
    const source = stackPtr(this.context, 0);
    const lengthBytes = stackU32(this.context, 1);
    const state = streamState(stream);
    let text = null;

    try {
      const begin = source.add(8).readPointer();
      const end = source.add(12).readPointer();
      const length = Math.max(
        0,
        Math.min(pointerDelta(end, begin), MAX_STRING_CHARS),
      );
      text = safeUtf8(begin, length);
    } catch (_) {}

    emit({
      event: "string_write",
      seq: ctx.seq,
      direction: ctx.direction,
      type_id: ctx.type_id,
      type_name: ctx.type_name,
      stream: stream.toString(),
      offset: state !== null ? state.position : null,
      length_bytes: lengthBytes,
      source: source.toString(),
      object_offset: objectOffset(ctx, source),
      text,
    });
  },
});

emit({
  event: "logger_started",
  module: MODULE_NAME,
  module_base: mainModule.base.toString(),
  ghidra_image_base: ptr(GHIDRA_IMAGE_BASE).toString(),
  log_path: logFile !== null ? LOG_PATH : null,
  config: {
    stream_packet_body_log: ENABLE_STREAM_PACKET_BODY_LOG,
    dangerous_interior_packet_body_hooks:
      ENABLE_DANGEROUS_INTERIOR_PACKET_BODY_HOOKS,
    dangerous_interior_message_call_hooks:
      ENABLE_DANGEROUS_INTERIOR_MESSAGE_CALL_HOOKS,
    field_trace: ENABLE_FIELD_TRACE,
    string_trace: ENABLE_STRING_TRACE,
    max_packet_bytes: MAX_PACKET_BYTES,
    max_field_bytes: MAX_FIELD_BYTES,
  },
});
