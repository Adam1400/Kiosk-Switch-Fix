/*
 * Copyright (c) 2019 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Lots of code from:
/*
*   This file is part of Luma3DS.
*   Copyright (C) 2016-2019 Aurora Wright, TuxSH
*
*   SPDX-License-Identifier: (MIT OR GPL-2.0-or-later)
*/

#include <string.h>

//#include "context.h"

#include "net.h"
#include "debug.h"

#include "../breakpoints.h"
#include "../software_breakpoints.h"
#include "../watchpoints.h"
#include "../fpu.h"

static TEMPORARY char g_gdbWorkBuffer[GDB_WORK_BUF_LEN];
static TEMPORARY char g_gdbBuffer[GDB_BUF_LEN + 4 + 1];

static const struct{
    char command;
    GDBCommandHandler handler;
} gdbCommandHandlers[] = {
    { '?', GDB_HANDLER(GetStopReason) },
    //{ '!', GDB_HANDLER(EnableExtendedMode) }, // note: stubbed
    //{ 'c', GDB_HANDLER(ContinueOrStepDeprecated) },
    //{ 'C', GDB_HANDLER(ContinueOrStepDeprecated) },
    { 'D', GDB_HANDLER(Detach) },
    { 'F', GDB_HANDLER(HioReply) },
    { 'g', GDB_HANDLER(ReadRegisters) },
    { 'G', GDB_HANDLER(WriteRegisters) },
    { 'H', GDB_HANDLER(SetThreadId) },
    { 'k', GDB_HANDLER(Kill) },
    { 'm', GDB_HANDLER(ReadMemory) },
    { 'M', GDB_HANDLER(WriteMemory) },
    { 'p', GDB_HANDLER(ReadRegister) },
    { 'P', GDB_HANDLER(WriteRegister) },
    { 'q', GDB_HANDLER(ReadQuery) },
    { 'Q', GDB_HANDLER(WriteQuery) },
    //{ 's', GDB_HANDLER(ContinueOrStepDeprecated) },
    //{ 'S', GDB_HANDLER(ContinueOrStepDeprecated) },
    { 'T', GDB_HANDLER(IsThreadAlive) },
    { 'v', GDB_HANDLER(VerboseCommand) },
    { 'X', GDB_HANDLER(WriteMemoryRaw) },
    { 'z', GDB_HANDLER(ToggleStopPoint) },
    { 'Z', GDB_HANDLER(ToggleStopPoint) },
};

static inline GDBCommandHandler GDB_GetCommandHandler(char command)
{
    static const u32 nbHandlers = sizeof(gdbCommandHandlers) / sizeof(gdbCommandHandlers[0]);

    size_t i;
    for (i = 0; i < nbHandlers && gdbCommandHandlers[i].command != command; i++);

    return i < nbHandlers ? gdbCommandHandlers[i].handler : GDB_HANDLER(Unsupported);
}

static int GDB_ProcessPacket(GDBContext *ctx, size_t len)
{
    int ret;

    ENSURE(ctx->state != GDB_STATE_DISCONNECTED);

    // Handle the packet...
    if (ctx->buffer[0] == '\x03') {
        GDB_BreakAllCores(ctx);
        ret = 0;
    } else {
        GDBCommandHandler handler = GDB_GetCommandHandler(ctx->buffer[1]);
        ctx->commandData = ctx->buffer + 2;
        ret = handler(ctx);
    }


    // State changes...
    if (ctx->state == GDB_STATE_DETACHING) {
        return -1;
    }

    return ret;
}

static size_t GDB_ReceiveDataCallback(TransportInterface *iface, void *ctxVoid)
{
    return (size_t)GDB_ReceivePacket((GDBContext *)ctxVoid);
}

static void GDB_Disconnect(GDBContext *ctx)
{
    GDB_DetachFromContext(ctx);

    ctx->flags = 0;
    ctx->state = GDB_STATE_DISCONNECTED;

    ctx->selectedThreadId = 0;
    ctx->selectedThreadIdForContinuing = 0;
    ctx->sentDebugEventCoreList = 0;
    ctx->acknowledgedDebugEventCoreList = 0;

    ctx->attachedCoreList = 0;
    ctx->sendOwnDebugEventDisallowed = false;
    ctx->catchThreadEvents = false;
    ctx->lastSentPacketSize = 0;
    ctx->lastDebugEvent = NULL;

    ctx->processEnded = false;
    ctx->processExited = false;

    ctx->noAckSent = false;

    ctx->currentHioRequestTargetAddr = 0;
    memset(&ctx->currentHioRequest, 0, sizeof(PackedGdbHioRequest));

    ctx->targetXmlLen = 0;
}

static void GDB_ProcessDataCallback(TransportInterface *iface, void *ctxVoid, size_t sz)
{
    int r = (int)sz;
    GDBContext *ctx = (GDBContext *)ctxVoid;

    if (r == -1) {
        // Not sure if GDB has something to forcefully close connections over UART...
        char c = '\x04'; // ctrl-D
        transportInterfaceWriteData(iface, &c, 1);
        GDB_Disconnect(ctx);
    }

    r = GDB_ProcessPacket(ctx, sz);
    if (r == -1) {
        GDB_Disconnect(ctx);
    }
}

void GDB_InitializeContext(GDBContext *ctx, TransportInterfaceType ifaceType, u32 ifaceId, u32 ifaceFlags)
{
    memset(ctx, 0, sizeof(GDBContext));
    ctx->workBuffer = g_gdbWorkBuffer;
    ctx->buffer = g_gdbBuffer;
    ctx->transportInterface = transportInterfaceCreate(
        ifaceType,
        ifaceId,
        ifaceFlags,
        GDB_ReceiveDataCallback,
        GDB_ProcessDataCallback,
        ctx
    );
}

void GDB_AttachToContext(GDBContext *ctx)
{
    // TODO: move the debug traps enable here?

    ctx->attachedCoreList = getActiveCoreMask();

    // We're in full-stop mode at this point
    // Break cores, but don't send the debug event (it will be fetched with '?')
    // Initialize lastDebugEvent

    debugManagerSetReportingEnabled(true);
    ctx->sendOwnDebugEventDisallowed = true;

    GDB_BreakAllCores(ctx);

    DebugEventInfo *info = debugManagerGetDebugEvent(currentCoreCtx->coreId);
    info->preprocessed = true;
    info->handled = true;
    ctx->lastDebugEvent = info;

    ctx->state = GDB_STATE_ATTACHED;

    ctx->sendOwnDebugEventDisallowed = false;
}

void GDB_DetachFromContext(GDBContext *ctx)
{
    removeAllWatchpoints();
    removeAllBreakpoints();
    removeAllSoftwareBreakpoints(true);

    // Reports to gdb are prevented because of "detaching" state?

    // TODO: disable debug traps

    if(ctx->flags & GDB_FLAG_TERMINATE) {
        // TODO: redefine what it means for thermosphère, if anything.
        ctx->processEnded = true;
        ctx->processExited = false;
    }

    ctx->currentHioRequestTargetAddr = 0;
    memset(&ctx->currentHioRequest, 0, sizeof(PackedGdbHioRequest));

    debugManagerSetReportingEnabled(false);
    debugManagerContinueCores(getActiveCoreMask());
}

void GDB_AcquireContext(GDBContext *ctx)
{
    transportInterfaceAcquire(ctx->transportInterface);
}

void GDB_ReleaseContext(GDBContext *ctx)
{
    transportInterfaceRelease(ctx->transportInterface);
}

void GDB_MigrateRxIrq(GDBContext *ctx, u32 coreId)
{
    fpuCleanInvalidateRegisterCache();
    transportInterfaceSetInterruptAffinity(ctx->transportInterface, BIT(coreId));
}

GDB_DECLARE_HANDLER(Unsupported)
{
    return GDB_ReplyEmpty(ctx);
}

GDB_DECLARE_HANDLER(EnableExtendedMode)
{
    // We don't support it for now...
    return GDB_HandleUnsupported(ctx);
}
