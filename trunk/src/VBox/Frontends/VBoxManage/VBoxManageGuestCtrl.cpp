/* $Id$ */
/** @file
 * VBoxManage - Implementation of guestcontrol command.
 */

/*
 * Copyright (C) 2010-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "VBoxManage.h"

#ifndef VBOX_ONLY_DOCS

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/com/VirtualBox.h>
#include <VBox/com/EventQueue.h>

#include <VBox/HostServices/GuestControlSvc.h> /* for PROC_STS_XXX */

#include <iprt/asm.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/isofs.h>
#include <iprt/getopt.h>
#include <iprt/list.h>
#include <iprt/path.h>
#include <iprt/thread.h>

#ifdef USE_XPCOM_QUEUE
# include <sys/select.h>
# include <errno.h>
#endif

#include <signal.h>

#ifdef RT_OS_DARWIN
# include <CoreFoundation/CFRunLoop.h>
#endif

using namespace com;

/**
 * IVirtualBoxCallback implementation for handling the GuestControlCallback in
 * relation to the "guestcontrol * wait" command.
 */
/** @todo */

/** Set by the signal handler. */
static volatile bool    g_fGuestCtrlCanceled = false;

/*
 * Structure holding a directory entry.
 */
typedef struct DIRECTORYENTRY
{
    char       *pszSourcePath;
    char       *pszDestPath;
    RTLISTNODE  Node;
} DIRECTORYENTRY, *PDIRECTORYENTRY;

/**
 * Special exit codes for returning errors/information of a
 * started guest process to the command line VBoxManage was started from.
 * Useful for e.g. scripting.
 */
enum EXITCODE_EXEC
{
    EXITCODE_EXEC_SUCCESS      = RTEXITCODE_SUCCESS,
    /* Process exited normally but with an exit code <> 0. */
    EXITCODE_EXEC_CODE         = 16,
    EXITCODE_EXEC_FAILED       = 17,
    EXITCODE_EXEC_TERM_SIGNAL  = 18,
    EXITCODE_EXEC_TERM_ABEND   = 19,
    EXITCODE_EXEC_TIMEOUT      = 20,
    EXITCODE_EXEC_DOWN         = 21,
    EXITCODE_EXEC_CANCELED     = 22
};

/**
 * RTGetOpt-IDs for the guest execution control command line.
 */
enum GETOPTDEF_EXEC
{
    GETOPTDEF_EXEC_IGNOREORPHANEDPROCESSES = 1000,
    GETOPTDEF_EXEC_OUTPUTFORMAT,
    GETOPTDEF_EXEC_DOS2UNIX,
    GETOPTDEF_EXEC_UNIX2DOS,
    GETOPTDEF_EXEC_WAITFOREXIT,
    GETOPTDEF_EXEC_WAITFORSTDOUT,
    GETOPTDEF_EXEC_WAITFORSTDERR
};

enum OUTPUT_TYPE
{
    OUTPUT_TYPE_UNDEFINED = 0,
    OUTPUT_TYPE_DOS2UNIX  = 10,
    OUTPUT_TYPE_UNIX2DOS  = 20
};

#endif /* VBOX_ONLY_DOCS */

void usageGuestControl(PRTSTREAM pStrm)
{
    RTStrmPrintf(pStrm,
                 "VBoxManage guestcontrol     <vmname>|<uuid> exec[ute]\n"
                 "                            --image <path to program>\n"
                 "                            --username <name> --password <password>\n"
                 "                            [--dos2unix]\n"
                 "                            [--environment \"<NAME>=<VALUE> [<NAME>=<VALUE>]\"]\n"
                 "                            [--timeout <msec>] [--unix2dos] [--verbose]\n"
                 "                            [--wait-exit] [--wait-stdout] [--wait-stdout]"
                 //"                          [--output-format=<dos>|<unix>]\n"
                 "                            [--output-type=<binary>|<text>]\n"
                 "                            [-- [<argument1>] ... [<argumentN>]\n"
                 /** @todo Add a "--" parameter (has to be last parameter) to directly execute
                  *        stuff, e.g. "VBoxManage guestcontrol execute <VMName> --username <> ... -- /bin/rm -Rf /foo". */
                 "\n"
                 "                            <vmname>|<uuid> copyto|cp\n"
                 "                            <source on host> <destination on guest>\n"
                 "                            --username <name> --password <password>\n"
                 "                            [--dryrun] [--follow] [--recursive] [--verbose]\n"
                 "\n"
                 "                            <vmname>|<uuid> createdir[ectory]|mkdir|md\n"
                 "                            <directory to create on guest>\n"
                 "                            --username <name> --password <password>\n"
                 "                            [--parents] [--mode <mode>] [--verbose]\n"
                 "\n"
                 "                            <vmname>|<uuid> updateadditions\n"
                 "                            [--source <guest additions .ISO>] [--verbose]\n"
                 "\n");
}

#ifndef VBOX_ONLY_DOCS

/**
 * Signal handler that sets g_fGuestCtrlCanceled.
 *
 * This can be executed on any thread in the process, on Windows it may even be
 * a thread dedicated to delivering this signal.  Do not doing anything
 * unnecessary here.
 */
static void guestCtrlSignalHandler(int iSignal)
{
    NOREF(iSignal);
    ASMAtomicWriteBool(&g_fGuestCtrlCanceled, true);
}

/**
 * Installs a custom signal handler to get notified
 * whenever the user wants to intercept the program.
 */
static void ctrlSignalHandlerInstall()
{
    signal(SIGINT,   guestCtrlSignalHandler);
#ifdef SIGBREAK
    signal(SIGBREAK, guestCtrlSignalHandler);
#endif
}

/**
 * Uninstalls a previously installed signal handler.
 */
static void ctrlSignalHandlerUninstall()
{
    signal(SIGINT,   SIG_DFL);
#ifdef SIGBREAK
    signal(SIGBREAK, SIG_DFL);
#endif
}

/**
 * Translates a process status to a human readable
 * string.
 */
static const char *ctrlExecProcessStatusToText(ExecuteProcessStatus enmStatus)
{
    switch (enmStatus)
    {
        case ExecuteProcessStatus_Started:
            return "started";
        case ExecuteProcessStatus_TerminatedNormally:
            return "successfully terminated";
        case ExecuteProcessStatus_TerminatedSignal:
            return "terminated by signal";
        case ExecuteProcessStatus_TerminatedAbnormally:
            return "abnormally aborted";
        case ExecuteProcessStatus_TimedOutKilled:
            return "timed out";
        case ExecuteProcessStatus_TimedOutAbnormally:
            return "timed out, hanging";
        case ExecuteProcessStatus_Down:
            return "killed";
        case ExecuteProcessStatus_Error:
            return "error";
        default:
            break;
    }
    return "unknown";
}

static int ctrlExecProcessStatusToExitCode(ExecuteProcessStatus enmStatus, ULONG uExitCode)
{
    int rc = EXITCODE_EXEC_SUCCESS;
    switch (enmStatus)
    {
        case ExecuteProcessStatus_Started:
            rc = EXITCODE_EXEC_SUCCESS;
            break;
        case ExecuteProcessStatus_TerminatedNormally:
            rc = !uExitCode ? EXITCODE_EXEC_SUCCESS : EXITCODE_EXEC_CODE;
            break;
        case ExecuteProcessStatus_TerminatedSignal:
            rc = EXITCODE_EXEC_TERM_SIGNAL;
            break;
        case ExecuteProcessStatus_TerminatedAbnormally:
            rc = EXITCODE_EXEC_TERM_ABEND;
            break;
        case ExecuteProcessStatus_TimedOutKilled:
            rc = EXITCODE_EXEC_TIMEOUT;
            break;
        case ExecuteProcessStatus_TimedOutAbnormally:
            rc = EXITCODE_EXEC_TIMEOUT;
            break;
        case ExecuteProcessStatus_Down:
            /* Service/OS is stopping, process was killed, so
             * not exactly an error of the started process ... */
            rc = EXITCODE_EXEC_DOWN;
            break;
        case ExecuteProcessStatus_Error:
            rc = EXITCODE_EXEC_FAILED;
            break;
        default:
            AssertMsgFailed(("Unknown exit code (%u) from guest process returned!\n", enmStatus));
            break;
    }
    return rc;
}

static int ctrlPrintError(com::ErrorInfo &errorInfo)
{
    if (   errorInfo.isFullAvailable()
        || errorInfo.isBasicAvailable())
    {
        /* If we got a VBOX_E_IPRT error we handle the error in a more gentle way
         * because it contains more accurate info about what went wrong. */
        if (errorInfo.getResultCode() == VBOX_E_IPRT_ERROR)
            RTMsgError("%ls.", errorInfo.getText().raw());
        else
        {
            RTMsgError("Error details:");
            GluePrintErrorInfo(errorInfo);
        }
        return VERR_GENERAL_FAILURE; /** @todo */
    }
    AssertMsgFailedReturn(("Object has indicated no error!?\n"),
                          VERR_INVALID_PARAMETER);
}

static int ctrlPrintError(IUnknown *pObj, const GUID &aIID)
{
    com::ErrorInfo ErrInfo(pObj, aIID);
    return ctrlPrintError(ErrInfo);
}


static int ctrlPrintProgressError(ComPtr<IProgress> progress)
{
    int rc;
    BOOL fCanceled;
    if (   SUCCEEDED(progress->COMGETTER(Canceled(&fCanceled)))
        && fCanceled)
    {
        rc = VERR_CANCELLED;
    }
    else
    {
        com::ProgressErrorInfo ErrInfo(progress);
        rc = ctrlPrintError(ErrInfo);
    }
    return rc;
}

/**
 * Un-initializes the VM after guest control usage.
 */
static void ctrlUninitVM(HandlerArg *pArg)
{
    AssertPtrReturnVoid(pArg);
    if (pArg->session)
        pArg->session->UnlockMachine();
}

/**
 * Initializes the VM for IGuest operations.
 *
 * That is, checks whether it's up and running, if it can be locked (shared
 * only) and returns a valid IGuest pointer on success.
 *
 * @return  IPRT status code.
 * @param   pArg            Our command line argument structure.
 * @param   pszNameOrId     The VM's name or UUID.
 * @param   pGuest          Where to return the IGuest interface pointer.
 */
static int ctrlInitVM(HandlerArg *pArg, const char *pszNameOrId, ComPtr<IGuest> *pGuest)
{
    AssertPtrReturn(pArg, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszNameOrId, VERR_INVALID_PARAMETER);

    /* Lookup VM. */
    ComPtr<IMachine> machine;
    /* Assume it's an UUID. */
    HRESULT rc;
    CHECK_ERROR(pArg->virtualBox, FindMachine(Bstr(pszNameOrId).raw(),
                                              machine.asOutParam()));
    if (FAILED(rc))
        return VERR_NOT_FOUND;

    /* Machine is running? */
    MachineState_T machineState;
    CHECK_ERROR_RET(machine, COMGETTER(State)(&machineState), 1);
    if (machineState != MachineState_Running)
    {
        RTMsgError("Machine \"%s\" is not running (currently %s)!\n",
                   pszNameOrId, machineStateToName(machineState, false));
        return VERR_VM_INVALID_VM_STATE;
    }

    do
    {
        /* Open a session for the VM. */
        CHECK_ERROR_BREAK(machine, LockMachine(pArg->session, LockType_Shared));
        /* Get the associated console. */
        ComPtr<IConsole> console;
        CHECK_ERROR_BREAK(pArg->session, COMGETTER(Console)(console.asOutParam()));
        /* ... and session machine. */
        ComPtr<IMachine> sessionMachine;
        CHECK_ERROR_BREAK(pArg->session, COMGETTER(Machine)(sessionMachine.asOutParam()));
        /* Get IGuest interface. */
        CHECK_ERROR_BREAK(console, COMGETTER(Guest)(pGuest->asOutParam()));
    } while (0);

    if (FAILED(rc))
        ctrlUninitVM(pArg);
    return SUCCEEDED(rc) ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
}

/* <Missing docuemntation> */
static int handleCtrlExecProgram(ComPtr<IGuest> guest, HandlerArg *pArg)
{
    AssertPtrReturn(pArg, VERR_INVALID_PARAMETER);

    /*
     * Parse arguments.
     */
    if (pArg->argc < 2) /* At least the command we want to execute in the guest should be present :-). */
        return errorSyntax(USAGE_GUESTCONTROL, "Incorrect parameters");

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--dos2unix",                     GETOPTDEF_EXEC_DOS2UNIX,                  RTGETOPT_REQ_NOTHING },
        { "--environment",                  'e',                                      RTGETOPT_REQ_STRING  },
        { "--flags",                        'f',                                      RTGETOPT_REQ_STRING  },
        { "--ignore-operhaned-processes",   GETOPTDEF_EXEC_IGNOREORPHANEDPROCESSES,   RTGETOPT_REQ_NOTHING },
        { "--image",                        'i',                                      RTGETOPT_REQ_STRING  },
        { "--password",                     'p',                                      RTGETOPT_REQ_STRING  },
        { "--timeout",                      't',                                      RTGETOPT_REQ_UINT32  },
        { "--unix2dos",                     GETOPTDEF_EXEC_UNIX2DOS,                  RTGETOPT_REQ_NOTHING },
        { "--username",                     'u',                                      RTGETOPT_REQ_STRING  },
        { "--verbose",                      'v',                                      RTGETOPT_REQ_NOTHING },
        { "--wait-exit",                    GETOPTDEF_EXEC_WAITFOREXIT,               RTGETOPT_REQ_NOTHING },
        { "--wait-stdout",                  GETOPTDEF_EXEC_WAITFORSTDOUT,             RTGETOPT_REQ_NOTHING },
        { "--wait-stderr",                  GETOPTDEF_EXEC_WAITFORSTDERR,             RTGETOPT_REQ_NOTHING }
    };

    int                     ch;
    RTGETOPTUNION           ValueUnion;
    RTGETOPTSTATE           GetState;
    RTGetOptInit(&GetState, pArg->argc, pArg->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);

    Utf8Str                 Utf8Cmd;
    uint32_t                fFlags = 0;
    com::SafeArray<IN_BSTR> args;
    com::SafeArray<IN_BSTR> env;
    Utf8Str                 Utf8UserName;
    Utf8Str                 Utf8Password;
    uint32_t                cMsTimeout      = 0;
    OUTPUT_TYPE             eOutputType     = OUTPUT_TYPE_UNDEFINED;
    bool                    fOutputBinary   = false;
    bool                    fWaitForExit    = false;
    bool                    fWaitForStdOut  = false;
    bool                    fWaitForStdErr  = false;
    bool                    fVerbose        = false;

    int                     vrc             = VINF_SUCCESS;
    while (   (ch = RTGetOpt(&GetState, &ValueUnion))
           && RT_SUCCESS(vrc))
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            case GETOPTDEF_EXEC_DOS2UNIX:
                if (eOutputType != OUTPUT_TYPE_UNDEFINED)
                    return errorSyntax(USAGE_GUESTCONTROL, "More than one output type (dos2unix/unix2dos) specified!");
                eOutputType = OUTPUT_TYPE_DOS2UNIX;
                break;

            case 'e': /* Environment */
            {
                char **papszArg;
                int cArgs;

                vrc = RTGetOptArgvFromString(&papszArg, &cArgs, ValueUnion.psz, NULL);
                if (RT_FAILURE(vrc))
                    return errorSyntax(USAGE_GUESTCONTROL, "Failed to parse environment value, rc=%Rrc", vrc);
                for (int j = 0; j < cArgs; j++)
                    env.push_back(Bstr(papszArg[j]).raw());

                RTGetOptArgvFree(papszArg);
                break;
            }

            case GETOPTDEF_EXEC_IGNOREORPHANEDPROCESSES:
                fFlags |= ExecuteProcessFlag_IgnoreOrphanedProcesses;
                break;

            case 'i':
                Utf8Cmd = ValueUnion.psz;
                break;

            /** @todo Add a hidden flag. */

            case 'p': /* Password */
                Utf8Password = ValueUnion.psz;
                break;

            case 't': /* Timeout */
                cMsTimeout = ValueUnion.u32;
                break;

            case GETOPTDEF_EXEC_UNIX2DOS:
                if (eOutputType != OUTPUT_TYPE_UNDEFINED)
                    return errorSyntax(USAGE_GUESTCONTROL, "More than one output type (dos2unix/unix2dos) specified!");
                eOutputType = OUTPUT_TYPE_UNIX2DOS;
                break;

            case 'u': /* User name */
                Utf8UserName = ValueUnion.psz;
                break;

            case 'v': /* Verbose */
                fVerbose = true;
                break;

            case GETOPTDEF_EXEC_WAITFOREXIT:
                fWaitForExit = true;
                break;

            case GETOPTDEF_EXEC_WAITFORSTDOUT:
                fWaitForExit = true;
                fWaitForStdOut = true;
                break;

            case GETOPTDEF_EXEC_WAITFORSTDERR:
                fWaitForExit = true;
                fWaitForStdErr = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                if (Utf8Cmd.isEmpty())
                    Utf8Cmd = ValueUnion.psz;
                else
                {
                    /* Push current parameter to vector. */
                    args.push_back(Bstr(ValueUnion.psz).raw());

                    /*
                     * Add all following parameters after this one to our guest process
                     * argument vector.
                     */
                    while (   (ch = RTGetOpt(&GetState, &ValueUnion))
                           && RT_SUCCESS(vrc))
                    {
                        /*
                         * Is this option unknown or not recognized an option? Then just
                         * add the raw string value to our argument vector.
                         */
                        if (   ch == VINF_GETOPT_NOT_OPTION
                            || ch == VERR_GETOPT_UNKNOWN_OPTION)
                            args.push_back(Bstr(ValueUnion.psz).raw());
                        /*
                         * If this is an option/parameter we already defined for our actual
                         * execution command we need to take the pDef->pszLong value instead.
                         */
                        else if (ValueUnion.pDef)
                            args.push_back(Bstr(ValueUnion.pDef->pszLong).raw());
                        else
                            AssertMsgFailed(("Unknown parameter type detected!\n"));
                    }
                }
                break;
            }

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    if (Utf8Cmd.isEmpty())
        return errorSyntax(USAGE_GUESTCONTROL, "No command to execute specified!");

    if (Utf8UserName.isEmpty())
        return errorSyntax(USAGE_GUESTCONTROL, "No user name specified!");

    /*
     * <missing comment indicating that we're done parsing args and started doing something else>
     */
    HRESULT rc = S_OK;
    if (fVerbose)
    {
        if (cMsTimeout == 0)
            RTPrintf("Waiting for guest to start process ...\n");
        else
            RTPrintf("Waiting for guest to start process (within %ums)\n", cMsTimeout);
    }

    /* Get current time stamp to later calculate rest of timeout left. */
    uint64_t u64StartMS = RTTimeMilliTS();

    /* Execute the process. */
    int rcProc;
    ComPtr<IProgress> progress;
    ULONG uPID = 0;
    rc = guest->ExecuteProcess(Bstr(Utf8Cmd).raw(),
                               fFlags,
                               ComSafeArrayAsInParam(args),
                               ComSafeArrayAsInParam(env),
                               Bstr(Utf8UserName).raw(),
                               Bstr(Utf8Password).raw(),
                               cMsTimeout,
                               &uPID,
                               progress.asOutParam());
    if (FAILED(rc))
        return ctrlPrintError(guest, COM_IIDOF(IGuest));
    else
    {
        if (fVerbose)
            RTPrintf("Process '%s' (PID: %u) started\n", Utf8Cmd.c_str(), uPID);
        if (fWaitForExit)
        {
            if (fVerbose)
            {
                if (cMsTimeout) /* Wait with a certain timeout. */
                {
                    /* Calculate timeout value left after process has been started.  */
                    uint64_t u64Elapsed = RTTimeMilliTS() - u64StartMS;
                    /* Is timeout still bigger than current difference? */
                    if (cMsTimeout > u64Elapsed)
                        RTPrintf("Waiting for process to exit (%ums left) ...\n", cMsTimeout - u64Elapsed);
                    else
                        RTPrintf("No time left to wait for process!\n"); /** @todo a bit misleading ... */
                }
                else /* Wait forever. */
                    RTPrintf("Waiting for process to exit ...\n");
            }

            /* Setup signal handling if cancelable. */
            ASSERT(progress);
            bool fCanceledAlready = false;
            BOOL fCancelable;
            HRESULT hrc = progress->COMGETTER(Cancelable)(&fCancelable);
            if (FAILED(hrc))
                fCancelable = FALSE;
            if (fCancelable)
                ctrlSignalHandlerInstall();

            /* Wait for process to exit ... */
            BOOL fCompleted    = FALSE;
            BOOL fCanceled     = FALSE;
            int  cMilliesSleep = 0;
            while (SUCCEEDED(progress->COMGETTER(Completed(&fCompleted))))
            {
                SafeArray<BYTE> aOutputData;
                ULONG cbOutputData = 0;

                /*
                 * Some data left to output?
                 */
                if (   fWaitForStdOut
                    || fWaitForStdErr)
                {
                    /** @todo r=bird: The timeout argument is bogus in several
                     * ways:
                     *  1. RT_MAX will evaluate the arguments twice, which may
                     *     result in different values because RTTimeMilliTS()
                     *     returns a higher value the 2nd time. Worst case:
                     *     Imagine when RT_MAX calculates the remaining time
                     *     out (first expansion) there is say 60 ms left.  Then
                     *     we're preempted and rescheduled after, say, 120 ms.
                     *     We call RTTimeMilliTS() again and ends up with a
                     *     value -60 ms, which translate to a UINT32_MAX - 59
                     *     ms timeout.
                     *
                     *  2. When the period expires, we will wait forever since
                     *     both 0 and -1 mean indefinite timeout with this API,
                     *     at least that's one way of reading the main code.
                     *
                     *  3. There is a signed/unsigned ambiguity in the
                     *     RT_MAX expression.  The left hand side is signed
                     *     integer (0), the right side is unsigned 64-bit. From
                     *     what I can tell, the compiler will treat this as
                     *     unsigned 64-bit and never return 0.
                     */
                    /** @todo r=bird: We must separate stderr and stdout
                     *        output, seems bunched together here which
                     *        won't do the trick for unix BOFHs. */
                    rc = guest->GetProcessOutput(uPID, 0 /* aFlags */,
                                                 RT_MAX(0, cMsTimeout - (RTTimeMilliTS() - u64StartMS)) /* Timeout in ms */,
                                                 _64K, ComSafeArrayAsOutParam(aOutputData));
                    if (FAILED(rc))
                    {
                        vrc = ctrlPrintError(guest, COM_IIDOF(IGuest));
                        cbOutputData = 0;
                    }
                    else
                    {
                        cbOutputData = aOutputData.size();
                        if (cbOutputData > 0)
                        {
                            /** @todo r=bird: Use a VFS I/O stream filter for doing this, it's a
                            *        generic problem and the new VFS APIs will handle it more
                            *        transparently. (requires writing dos2unix/unix2dos filters ofc) */
                            if (eOutputType != OUTPUT_TYPE_UNDEFINED)
                            {
                                /*
                                 * If aOutputData is text data from the guest process' stdout or stderr,
                                 * it has a platform dependent line ending. So standardize on
                                 * Unix style, as RTStrmWrite does the LF -> CR/LF replacement on
                                 * Windows. Otherwise we end up with CR/CR/LF on Windows.
                                 */
                                ULONG cbOutputDataPrint = cbOutputData;
                                for (BYTE *s = aOutputData.raw(), *d = s;
                                     s - aOutputData.raw() < (ssize_t)cbOutputData;
                                     s++, d++)
                                {
                                    if (*s == '\r')
                                    {
                                        /* skip over CR, adjust destination */
                                        d--;
                                        cbOutputDataPrint--;
                                    }
                                    else if (s != d)
                                        *d = *s;
                                }
                                RTStrmWrite(g_pStdOut, aOutputData.raw(), cbOutputDataPrint);
                            }
                            else /* Just dump all data as we got it ... */
                                RTStrmWrite(g_pStdOut, aOutputData.raw(), cbOutputData);
                        }
                    }
                }

                /* No more output data left? */
                if (cbOutputData <= 0)
                {
                    /* Only break out from process handling loop if we processed (displayed)
                     * all output data or if there simply never was output data and the process
                     * has been marked as complete. */
                    if (fCompleted)
                        break;
                }

                /* Process async cancelation */
                if (g_fGuestCtrlCanceled && !fCanceledAlready)
                {
                    hrc = progress->Cancel();
                    if (SUCCEEDED(hrc))
                        fCanceledAlready = TRUE;
                    else
                        g_fGuestCtrlCanceled = false;
                }

                /* Progress canceled by Main API? */
                if (   SUCCEEDED(progress->COMGETTER(Canceled(&fCanceled)))
                    && fCanceled)
                    break;

                /* Did we run out of time? */
                if (   cMsTimeout
                    && RTTimeMilliTS() - u64StartMS > cMsTimeout)
                {
                    progress->Cancel();
                    break;
                }
            } /* while */

            /* Undo signal handling */
            if (fCancelable)
                ctrlSignalHandlerUninstall();

            /* Report status back to the user. */
            if (fCanceled)
            {
                if (fVerbose)
                    RTPrintf("Process execution canceled!\n");
                rcProc = EXITCODE_EXEC_CANCELED;
            }
            else if (   fCompleted
                     && SUCCEEDED(rc)) /* The GetProcessOutput rc. */
            {
                LONG iRc;
                CHECK_ERROR_RET(progress, COMGETTER(ResultCode)(&iRc), rc);
                if (FAILED(iRc))
                    vrc = ctrlPrintProgressError(progress);
                else
                {
                    ExecuteProcessStatus retStatus;
                    ULONG uRetExitCode, uRetFlags;
                    rc = guest->GetProcessStatus(uPID, &uRetExitCode, &uRetFlags, &retStatus);
                    if (SUCCEEDED(rc) && fVerbose)
                        RTPrintf("Exit code=%u (Status=%u [%s], Flags=%u)\n", uRetExitCode, retStatus, ctrlExecProcessStatusToText(retStatus), uRetFlags);
                    rcProc = ctrlExecProcessStatusToExitCode(retStatus, uRetExitCode);
                }
            }
            else
            {
                if (fVerbose)
                    RTPrintf("Process execution aborted!\n");
                rcProc = EXITCODE_EXEC_TERM_ABEND;
            }
        }
    }

    if (RT_FAILURE(vrc) || FAILED(rc))
        return RTEXITCODE_FAILURE;
    return rcProc;
}

/**
 * Appends a new file/directory entry to a given list.
 *
 * @return  IPRT status code.
 * @param   pszFileSource       Full qualified source path of file to copy (optional).
 * @param   pszFileDest         Full qualified destination path (optional).
 * @param   pList               Copy list used for insertion.
 */
static int ctrlDirectoryEntryAppend(const char *pszFileSource, const char *pszFileDest,
                                    PRTLISTNODE pList)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertReturn(pszFileSource || pszFileDest, VERR_INVALID_PARAMETER);

    PDIRECTORYENTRY pNode = (PDIRECTORYENTRY)RTMemAlloc(sizeof(DIRECTORYENTRY));
    if (pNode == NULL)
        return VERR_NO_MEMORY;

    pNode->pszSourcePath = NULL;
    pNode->pszDestPath = NULL;

    if (pszFileSource)
    {
        pNode->pszSourcePath = RTStrDup(pszFileSource);
        AssertPtrReturn(pNode->pszSourcePath, VERR_NO_MEMORY);
    }
    if (pszFileDest)
    {
        pNode->pszDestPath = RTStrDup(pszFileDest);
        AssertPtrReturn(pNode->pszDestPath, VERR_NO_MEMORY);
    }

    pNode->Node.pPrev = NULL;
    pNode->Node.pNext = NULL;
    RTListAppend(pList, &pNode->Node);
    return VINF_SUCCESS;
}

/**
 * Destroys a directory list.
 *
 * @param   pList               Pointer to list to destroy.
 */
static void ctrlDirectoryListDestroy(PRTLISTNODE pList)
{
    AssertPtr(pList);

    /* Destroy file list. */
    PDIRECTORYENTRY pNode = RTListGetFirst(pList, DIRECTORYENTRY, Node);
    while (pNode)
    {
        PDIRECTORYENTRY pNext = RTListNodeGetNext(&pNode->Node, DIRECTORYENTRY, Node);
        bool fLast = RTListNodeIsLast(pList, &pNode->Node);

        if (pNode->pszSourcePath)
            RTStrFree(pNode->pszSourcePath);
        if (pNode->pszDestPath)
            RTStrFree(pNode->pszDestPath);
        RTListNodeRemove(&pNode->Node);
        RTMemFree(pNode);

        if (fLast)
            break;

        pNode = pNext;
    }
}


/**
 * Reads a specified directory (recursively) based on the copy flags
 * and appends all matching entries to the supplied list.
 *
 * @return  IPRT status code.
 * @param   pszRootDir          Directory to start with. Must end with
 *                              a trailing slash and must be absolute.
 * @param   pszSubDir           Sub directory part relative to the root
 *                              directory; needed for recursion.
 * @param   pszFilter           Search filter (e.g. *.pdf).
 * @param   pszDest             Destination directory.
 * @param   fFlags              Copy flags.
 * @param   pcObjects           Where to store the overall objects to
 *                              copy found.
 * @param   pList               Pointer to the object list to use.
 */
static int ctrlCopyDirectoryRead(const char *pszRootDir, const char *pszSubDir,
                                 const char *pszFilter, const char *pszDest,
                                 uint32_t fFlags, uint32_t *pcObjects, PRTLISTNODE pList)
{
    AssertPtrReturn(pszRootDir, VERR_INVALID_POINTER);
    /* Sub directory is optional. */
    /* Filter directory is optional. */
    AssertPtrReturn(pszDest, VERR_INVALID_POINTER);
    AssertPtrReturn(pcObjects, VERR_INVALID_POINTER);
    AssertPtrReturn(pList, VERR_INVALID_POINTER);

    PRTDIR pDir = NULL;

    int rc = VINF_SUCCESS;
    char szCurDir[RTPATH_MAX];
    /* Construct current path. */
    if (RTStrPrintf(szCurDir, sizeof(szCurDir), pszRootDir))
    {
        if (pszSubDir != NULL)
            rc = RTPathAppend(szCurDir, sizeof(szCurDir), pszSubDir);
    }
    else
        rc = VERR_NO_MEMORY;

    if (RT_SUCCESS(rc))
    {
        /* Open directory without a filter - RTDirOpenFiltered unfortunately
         * cannot handle sub directories so we have to do the filtering ourselves. */
        rc = RTDirOpen(&pDir, szCurDir);
        for (;RT_SUCCESS(rc);)
        {
            RTDIRENTRY DirEntry;
            rc = RTDirRead(pDir, &DirEntry, NULL);
            if (RT_FAILURE(rc))
            {
                if (rc == VERR_NO_MORE_FILES)
                    rc = VINF_SUCCESS;
                break;
            }
            switch (DirEntry.enmType)
            {
                case RTDIRENTRYTYPE_DIRECTORY:
                    /* Skip "." and ".." entrires. */
                    if (   !strcmp(DirEntry.szName, ".")
                        || !strcmp(DirEntry.szName, ".."))
                    {
                        break;
                    }
                    if (fFlags & CopyFileFlag_Recursive)
                    {
                        char *pszNewSub = NULL;
                        if (pszSubDir)
                            RTStrAPrintf(&pszNewSub, "%s%s/", pszSubDir, DirEntry.szName);
                        else
                            RTStrAPrintf(&pszNewSub, "%s/", DirEntry.szName);

                        if (pszNewSub)
                        {
                            rc = ctrlCopyDirectoryRead(pszRootDir, pszNewSub,
                                                       pszFilter, pszDest,
                                                       fFlags, pcObjects, pList);
                            RTStrFree(pszNewSub);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                    break;

                case RTDIRENTRYTYPE_SYMLINK:
                    if (   (fFlags & CopyFileFlag_Recursive)
                        && (fFlags & CopyFileFlag_FollowLinks))
                    {
                        /* Fall through to next case is intentional. */
                    }
                    else
                        break;

                case RTDIRENTRYTYPE_FILE:
                {
                    bool fProcess = false;
                    if (pszFilter && RTStrSimplePatternMatch(pszFilter, DirEntry.szName))
                        fProcess = true;
                    else if (!pszFilter)
                        fProcess = true;

                    if (fProcess)
                    {
                        char *pszFileSource = NULL;
                        char *pszFileDest = NULL;
                        if (RTStrAPrintf(&pszFileSource, "%s%s%s",
                                         pszRootDir, pszSubDir ? pszSubDir : "",
                                         DirEntry.szName) >= 0)
                        {
                            if (RTStrAPrintf(&pszFileDest, "%s%s%s",
                                             pszDest, pszSubDir ? pszSubDir : "",
                                             DirEntry.szName) <= 0)
                            {
                                rc = VERR_NO_MEMORY;
                            }
                        }
                        else
                            rc = VERR_NO_MEMORY;

                        if (RT_SUCCESS(rc))
                        {
                            rc = ctrlDirectoryEntryAppend(pszFileSource, pszFileDest, pList);
                            if (RT_SUCCESS(rc))
                                *pcObjects = *pcObjects + 1;
                        }

                        if (pszFileSource)
                            RTStrFree(pszFileSource);
                        if (pszFileDest)
                            RTStrFree(pszFileDest);
                    }
                }
                break;

                default:
                    break;
            }
            if (RT_FAILURE(rc))
                break;
        }
    }

    if (pDir)
        RTDirClose(pDir);
    return rc;
}

/**
 * Initializes the copy process and builds up an object list
 * with all required information to start the actual copy process.
 *
 * @return  IPRT status code.
 * @param   pszSource           Source path on host to use.
 * @param   pszDest             Destination path on guest to use.
 * @param   fFlags              Copy flags.
 * @param   pcObjects           Where to store the count of objects to be copied.
 * @param   pList               Where to store the object list.
 */
static int ctrlCopyInit(const char *pszSource, const char *pszDest, uint32_t fFlags,
                        uint32_t *pcObjects, PRTLISTNODE pList)
{
    AssertPtrReturn(pszSource, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDest, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcObjects, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pList, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    char *pszSourceAbs = RTPathAbsDup(pszSource);
    if (pszSourceAbs)
    {
        if (   RTPathFilename(pszSourceAbs)
            && RTFileExists(pszSourceAbs)) /* We have a single file ... */
        {
            char *pszDestAbs = RTStrDup(pszDest);
            if (pszDestAbs)
            {
                /* Do we have a trailing slash for the destination?
                 * Then this is a directory ... */
                size_t cch = strlen(pszDestAbs);
                if (    cch > 1
                    && (   RTPATH_IS_SLASH(pszDestAbs[cch - 1])
                        || RTPATH_IS_SLASH(pszDestAbs[cch - 2])
                       )
                   )
                {
                    rc = RTStrAAppend(&pszDestAbs, RTPathFilename(pszSourceAbs));
                }
                else
                {
                    /* Since the desetination seems not to be a directory,
                     * we assume that this is the absolute path to the destination
                     * file -> nothing to do here ... */
                }

                if (RT_SUCCESS(rc))
                {
                    RTListInit(pList);
                    rc = ctrlDirectoryEntryAppend(pszSourceAbs, pszDestAbs, pList);
                    *pcObjects = 1;
                }
                RTStrFree(pszDestAbs);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else /* ... or a directory. */
        {
            /* Append trailing slash to absolute directory. */
            if (RTDirExists(pszSourceAbs))
                RTStrAAppend(&pszSourceAbs, RTPATH_SLASH_STR);

            /* Extract directory filter (e.g. "*.exe"). */
            char *pszFilter = RTPathFilename(pszSourceAbs);
            char *pszSourceAbsRoot = RTStrDup(pszSourceAbs);
            char *pszDestAbs = RTStrDup(pszDest);
            if (   pszSourceAbsRoot
                && pszDestAbs)
            {
                if (pszFilter)
                {
                    RTPathStripFilename(pszSourceAbsRoot);
                    rc = RTStrAAppend(&pszSourceAbsRoot, RTPATH_SLASH_STR);
                }
                else
                {
                    /*
                     * If we have more than one file to copy, make sure that we have
                     * a trailing slash so that we can construct a full path name
                     * (e.g. "foo.txt" -> "c:/foo/temp.txt") as destination.
                     */
                    size_t cch = strlen(pszSourceAbsRoot);
                    if (    cch > 1
                        &&  !RTPATH_IS_SLASH(pszSourceAbsRoot[cch - 1])
                        &&  !RTPATH_IS_SLASH(pszSourceAbsRoot[cch - 2]))
                    {
                        rc = RTStrAAppend(&pszSourceAbsRoot, RTPATH_SLASH_STR);
                    }
                }

                if (RT_SUCCESS(rc))
                {
                    /*
                     * Make sure we have a valid destination path. All we can do
                     * here is to check whether we have a trailing slash -- the rest
                     * (i.e. path creation, rights etc.) needs to be done inside the guest.
                     */
                    size_t cch = strlen(pszDestAbs);
                    if (    cch > 1
                        &&  !RTPATH_IS_SLASH(pszDestAbs[cch - 1])
                        &&  !RTPATH_IS_SLASH(pszDestAbs[cch - 2]))
                    {
                        rc = RTStrAAppend(&pszDestAbs, RTPATH_SLASH_STR);
                    }
                }

                if (RT_SUCCESS(rc))
                {
                    RTListInit(pList);
                    rc = ctrlCopyDirectoryRead(pszSourceAbsRoot, NULL /* Sub directory */,
                                               pszFilter, pszDestAbs,
                                               fFlags, pcObjects, pList);
                    if (RT_SUCCESS(rc) && *pcObjects == 0)
                        rc = VERR_NOT_FOUND;
                }

                if (pszDestAbs)
                    RTStrFree(pszDestAbs);
                if (pszSourceAbsRoot)
                    RTStrFree(pszSourceAbsRoot);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        RTStrFree(pszSourceAbs);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}

/**
 * Copys a file from host to the guest.
 *
 * @return  IPRT status code.
 * @param   pGuest          IGuest interface pointer.
 * @param   fVerbose        Verbose flag.
 * @param   pszSource       Source path of existing host file to copy.
 * @param   pszDest         Destination path on guest to copy the file to.
 * @param   pszUserName     User name on guest to use for the copy operation.
 * @param   pszPassword     Password of user account.
 * @param   fFlags          Copy flags.
 */
static int ctrlCopyFileToGuest(IGuest *pGuest, bool fVerbose, const char *pszSource, const char *pszDest,
                               const char *pszUserName, const char *pszPassword,
                               uint32_t fFlags)
{
    AssertPtrReturn(pGuest, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszSource, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDest, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszUserName, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszPassword, VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;
    ComPtr<IProgress> progress;
    HRESULT rc = pGuest->CopyToGuest(Bstr(pszSource).raw(), Bstr(pszDest).raw(),
                                     Bstr(pszUserName).raw(), Bstr(pszPassword).raw(),
                                     fFlags, progress.asOutParam());
    if (FAILED(rc))
        vrc = ctrlPrintError(pGuest, COM_IIDOF(IGuest));
    else
    {
        rc = showProgress(progress);
        if (FAILED(rc))
            vrc = ctrlPrintProgressError(progress);
    }
    return vrc;
}

static int handleCtrlCopyTo(ComPtr<IGuest> guest, HandlerArg *pArg)
{
    AssertPtrReturn(pArg, VERR_INVALID_PARAMETER);

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--dryrun",              'd',         RTGETOPT_REQ_NOTHING },
        { "--follow",              'F',         RTGETOPT_REQ_NOTHING },
        { "--password",            'p',         RTGETOPT_REQ_STRING  },
        { "--recursive",           'R',         RTGETOPT_REQ_NOTHING },
        { "--username",            'u',         RTGETOPT_REQ_STRING  },
        { "--verbose",             'v',         RTGETOPT_REQ_NOTHING }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, pArg->argc, pArg->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);

    Utf8Str Utf8Source;
    Utf8Str Utf8Dest;
    Utf8Str Utf8UserName;
    Utf8Str Utf8Password;
    uint32_t fFlags = CopyFileFlag_None;
    bool fVerbose = false;
    bool fCopyRecursive = false;
    bool fDryRun = false;

    int vrc = VINF_SUCCESS;
    uint32_t idxNonOption = 0;
    while (ch = RTGetOpt(&GetState, &ValueUnion))
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            case 'd': /* Dry run */
                fDryRun = true;
                break;

            case 'F': /* Follow symlinks */
                fFlags |= CopyFileFlag_FollowLinks;
                break;

            case 'p': /* Password */
                Utf8Password = ValueUnion.psz;
                break;

            case 'R': /* Recursive processing */
                fFlags |= CopyFileFlag_Recursive;
                break;

            case 'u': /* User name */
                Utf8UserName = ValueUnion.psz;
                break;

            case 'v': /* Verbose */
                fVerbose = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                /* Get the actual source + destination. */
                switch (idxNonOption)
                {
                    case 0:
                        Utf8Source = ValueUnion.psz;
                        break;

                    case 1:
                        Utf8Dest = ValueUnion.psz;
                        break;

                    default:
                        return errorSyntax(USAGE_GUESTCONTROL, "Too many parameters specified, only source and destination allowed!");
                        /* Never reached. */
                        break;
                }
                idxNonOption++;
                if (idxNonOption == UINT32_MAX)
                    return errorSyntax(USAGE_GUESTCONTROL, "Too many files specified!");
                break;
            }

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    if (Utf8Source.isEmpty())
        return errorSyntax(USAGE_GUESTCONTROL,
                           "No source specified!");

    if (Utf8Dest.isEmpty())
        return errorSyntax(USAGE_GUESTCONTROL,
                           "No destination specified!");

    if (Utf8UserName.isEmpty())
        return errorSyntax(USAGE_GUESTCONTROL,
                           "No user name specified!");

    /*
     * Done parsing arguments, do stuff.
     */
    HRESULT rc = S_OK;
    if (fVerbose)
    {
        if (fDryRun)
            RTPrintf("Dry run - no files copied!\n");
        RTPrintf("Gathering file information ...\n");
    }

    RTLISTNODE listToCopy;
    uint32_t cObjects = 0;
    vrc = ctrlCopyInit(Utf8Source.c_str(), Utf8Dest.c_str(), fFlags,
                       &cObjects, &listToCopy);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_NOT_FOUND:
                RTMsgError("No files to copy found!\n");
                break;

            case VERR_FILE_NOT_FOUND:
                RTMsgError("Source path \"%s\" not found!\n", Utf8Source.c_str());
                break;

            default:
                RTMsgError("Failed to initialize, rc=%Rrc\n", vrc);
                break;
        }
    }
    else
    {
        PDIRECTORYENTRY pNode;
        if (RT_SUCCESS(vrc))
        {
            if (fVerbose)
            {
                if (fCopyRecursive)
                    RTPrintf("Recursively copying \"%s\" to \"%s\" (%u file(s)) ...\n",
                             Utf8Source.c_str(), Utf8Dest.c_str(), cObjects);
                else
                    RTPrintf("Copying \"%s\" to \"%s\" (%u file(s)) ...\n",
                             Utf8Source.c_str(), Utf8Dest.c_str(), cObjects);
            }

            uint32_t iObject = 1;
            RTListForEach(&listToCopy, pNode, DIRECTORYENTRY, Node)
            {
                if (!fDryRun)
                {
                    if (fVerbose)
                        RTPrintf("Copying \"%s\" to \"%s\" (%u/%u) ...\n",
                                 pNode->pszSourcePath, pNode->pszDestPath, iObject, cObjects);
                    /* Finally copy the desired file (if no dry run selected). */
                    if (!fDryRun)
                        vrc = ctrlCopyFileToGuest(guest, fVerbose, pNode->pszSourcePath, pNode->pszDestPath,
                                                  Utf8UserName.c_str(), Utf8Password.c_str(), fFlags);
                }
                if (RT_FAILURE(vrc))
                    break;
                iObject++;
            }
            if (RT_SUCCESS(vrc) && fVerbose)
                RTPrintf("Copy operation successful!\n");
        }
        ctrlDirectoryListDestroy(&listToCopy);
    }

    if (RT_FAILURE(vrc))
        rc = VBOX_E_IPRT_ERROR;
    return SUCCEEDED(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static int handleCtrlCreateDirectory(ComPtr<IGuest> guest, HandlerArg *pArg)
{
    AssertPtrReturn(pArg, VERR_INVALID_PARAMETER);

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--mode",                'm',         RTGETOPT_REQ_UINT32  },
        { "--parents",             'P',         RTGETOPT_REQ_NOTHING },
        { "--password",            'p',         RTGETOPT_REQ_STRING  },
        { "--username",            'u',         RTGETOPT_REQ_STRING  },
        { "--verbose",             'v',         RTGETOPT_REQ_NOTHING }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, pArg->argc, pArg->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);

    Utf8Str Utf8UserName;
    Utf8Str Utf8Password;
    uint32_t fFlags = CreateDirectoryFlag_None;
    uint32_t uMode = 0;
    bool fVerbose = false;

    RTLISTNODE listDirs;
    uint32_t cDirs = 0;
    RTListInit(&listDirs);

    int vrc = VINF_SUCCESS;
    bool fUsageOK = true;
    while (   (ch = RTGetOpt(&GetState, &ValueUnion))
           && RT_SUCCESS(vrc))
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            case 'm': /* Mode */
                uMode = ValueUnion.u32;
                break;

            case 'P': /* Create parents */
                fFlags |= CreateDirectoryFlag_Parents;
                break;

            case 'p': /* Password */
                Utf8Password = ValueUnion.psz;
                break;

            case 'u': /* User name */
                Utf8UserName = ValueUnion.psz;
                break;

            case 'v': /* Verbose */
                fVerbose = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                vrc = ctrlDirectoryEntryAppend(NULL,              /* No source given */
                                               ValueUnion.psz,    /* Destination */
                                               &listDirs);
                if (RT_SUCCESS(vrc))
                {
                    cDirs++;
                    if (cDirs == UINT32_MAX)
                    {
                        RTMsgError("Too many directories specified! Aborting.\n");
                        vrc = VERR_TOO_MUCH_DATA;
                    }
                }
                break;
            }

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    if (!fUsageOK)
        return errorSyntax(USAGE_GUESTCONTROL, "Incorrect parameters");

    if (!cDirs)
        return errorSyntax(USAGE_GUESTCONTROL,
                           "No directory to create specified!");

    if (Utf8UserName.isEmpty())
        return errorSyntax(USAGE_GUESTCONTROL,
                           "No user name specified!");

    HRESULT rc = S_OK;
    if (fVerbose && cDirs > 1)
        RTPrintf("Creating %u directories ...\n", cDirs);

    PDIRECTORYENTRY pNode;
    RTListForEach(&listDirs, pNode, DIRECTORYENTRY, Node)
    {
        if (fVerbose)
            RTPrintf("Creating directory \"%s\" ...\n", pNode->pszDestPath);

        ComPtr<IProgress> progress;
        rc = guest->CreateDirectory(Bstr(pNode->pszDestPath).raw(),
                                    Bstr(Utf8UserName).raw(), Bstr(Utf8Password).raw(),
                                    uMode, fFlags, progress.asOutParam());
        if (FAILED(rc))
        {
            ctrlPrintError(guest, COM_IIDOF(IGuest)); /* (return code ignored, save original rc) */
            break;
        }
    }
    ctrlDirectoryListDestroy(&listDirs);
    return SUCCEEDED(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static int handleCtrlUpdateAdditions(ComPtr<IGuest> guest, HandlerArg *pArg)
{
    AssertPtrReturn(pArg, VERR_INVALID_PARAMETER);

    /*
     * Check the syntax.  We can deduce the correct syntax from the number of
     * arguments.
     */
    Utf8Str Utf8Source;
    bool fVerbose = false;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--source",              's',         RTGETOPT_REQ_STRING  },
        { "--verbose",             'v',         RTGETOPT_REQ_NOTHING }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, pArg->argc, pArg->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);

    int vrc = VINF_SUCCESS;
    while (   (ch = RTGetOpt(&GetState, &ValueUnion))
           && RT_SUCCESS(vrc))
    {
        switch (ch)
        {
            case 's':
                Utf8Source = ValueUnion.psz;
                break;

            case 'v':
                fVerbose = true;
                break;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    if (fVerbose)
        RTPrintf("Updating Guest Additions ...\n");

#ifdef DEBUG_andy
    if (Utf8Source.isEmpty())
        Utf8Source = "c:\\Downloads\\VBoxGuestAdditions-r67158.iso";
#endif

    /* Determine source if not set yet. */
    if (Utf8Source.isEmpty())
    {
        char strTemp[RTPATH_MAX];
        vrc = RTPathAppPrivateNoArch(strTemp, sizeof(strTemp));
        AssertRC(vrc);
        Utf8Str Utf8Src1 = Utf8Str(strTemp).append("/VBoxGuestAdditions.iso");

        vrc = RTPathExecDir(strTemp, sizeof(strTemp));
        AssertRC(vrc);
        Utf8Str Utf8Src2 = Utf8Str(strTemp).append("/additions/VBoxGuestAdditions.iso");

        /* Check the standard image locations */
        if (RTFileExists(Utf8Src1.c_str()))
            Utf8Source = Utf8Src1;
        else if (RTFileExists(Utf8Src2.c_str()))
            Utf8Source = Utf8Src2;
        else
        {
            RTMsgError("Source could not be determined! Please use --source to specify a valid source.\n");
            vrc = VERR_FILE_NOT_FOUND;
        }
    }
    else if (!RTFileExists(Utf8Source.c_str()))
    {
        RTMsgError("Source \"%s\" does not exist!\n", Utf8Source.c_str());
        vrc = VERR_FILE_NOT_FOUND;
    }

    if (RT_SUCCESS(vrc))
    {
        if (fVerbose)
            RTPrintf("Using source: %s\n", Utf8Source.c_str());

        HRESULT rc = S_OK;
        ComPtr<IProgress> progress;
        CHECK_ERROR(guest, UpdateGuestAdditions(Bstr(Utf8Source).raw(),
                                                /* Wait for whole update process to complete. */
                                                AdditionsUpdateFlag_None,
                                                progress.asOutParam()));
        if (FAILED(rc))
            vrc = ctrlPrintError(guest, COM_IIDOF(IGuest));
        else
        {
            rc = showProgress(progress);
            if (FAILED(rc))
                vrc = ctrlPrintProgressError(progress);
            else if (fVerbose)
                RTPrintf("Guest Additions update successful.\n");
        }
    }

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/**
 * Access the guest control store.
 *
 * @returns program exit code.
 * @note see the command line API description for parameters
 */
int handleGuestControl(HandlerArg *pArg)
{
    AssertPtrReturn(pArg, VERR_INVALID_PARAMETER);

    HandlerArg arg = *pArg;
    arg.argc = pArg->argc - 2; /* Skip VM name and sub command. */
    arg.argv = pArg->argv + 2; /* Same here. */

    ComPtr<IGuest> guest;
    int vrc = ctrlInitVM(pArg, pArg->argv[0] /* VM Name */, &guest);
    if (RT_SUCCESS(vrc))
    {
        int rcExit;
        if (   !strcmp(pArg->argv[1], "exec")
            || !strcmp(pArg->argv[1], "execute"))
        {
            rcExit = handleCtrlExecProgram(guest, &arg);
        }
        else if (   !strcmp(pArg->argv[1], "copyto")
                 || !strcmp(pArg->argv[1], "cp"))
        {
            rcExit = handleCtrlCopyTo(guest, &arg);
        }
        else if (   !strcmp(pArg->argv[1], "createdirectory")
                 || !strcmp(pArg->argv[1], "createdir")
                 || !strcmp(pArg->argv[1], "mkdir")
                 || !strcmp(pArg->argv[1], "md"))
        {
            rcExit = handleCtrlCreateDirectory(guest, &arg);
        }
        else if (   !strcmp(pArg->argv[1], "updateadditions")
                 || !strcmp(pArg->argv[1], "updateadds"))
        {
            rcExit = handleCtrlUpdateAdditions(guest, &arg);
        }
        else
            rcExit = errorSyntax(USAGE_GUESTCONTROL, "No sub command specified!");

        ctrlUninitVM(pArg);
        return rcExit;
    }
    return RTEXITCODE_FAILURE;
}

#endif /* !VBOX_ONLY_DOCS */

