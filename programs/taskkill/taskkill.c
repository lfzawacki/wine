/*
 * Task termination utility
 *
 * Copyright 2008 Andrew Riedi
 * Copyright 2010 Andrew Nguyen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <windows.h>
#include <wine/debug.h>
#include <wine/unicode.h>

#include "taskkill.h"

WINE_DEFAULT_DEBUG_CHANNEL(taskkill);

int force_termination;

WCHAR **task_list;
unsigned int task_count;

struct pid_close_info
{
    DWORD pid;
    BOOL found;
};

static int taskkill_vprintfW(const WCHAR *msg, va_list va_args)
{
    int wlen;
    DWORD count, ret;
    WCHAR msg_buffer[8192];

    wlen = vsprintfW(msg_buffer, msg, va_args);

    ret = WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), msg_buffer, wlen, &count, NULL);
    if (!ret)
    {
        DWORD len;
        char *msgA;

        len = WideCharToMultiByte(GetConsoleOutputCP(), 0, msg_buffer, wlen,
            NULL, 0, NULL, NULL);
        msgA = HeapAlloc(GetProcessHeap(), 0, len);
        if (!msgA)
            return 0;

        WideCharToMultiByte(GetConsoleOutputCP(), 0, msg_buffer, wlen, msgA, len,
            NULL, NULL);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msgA, len, &count, FALSE);
        HeapFree(GetProcessHeap(), 0, msgA);
    }

    return count;
}

static int taskkill_printfW(const WCHAR *msg, ...)
{
    va_list va_args;
    int len;

    va_start(va_args, msg);
    len = taskkill_vprintfW(msg, va_args);
    va_end(va_args);

    return len;
}

static int taskkill_message_printfW(int msg, ...)
{
    va_list va_args;
    WCHAR msg_buffer[8192];
    int len;

    LoadStringW(GetModuleHandleW(NULL), msg, msg_buffer,
        sizeof(msg_buffer)/sizeof(WCHAR));

    va_start(va_args, msg);
    len = taskkill_vprintfW(msg_buffer, va_args);
    va_end(va_args);

    return len;
}

static int taskkill_message(int msg)
{
    static const WCHAR formatW[] = {'%','s',0};
    WCHAR msg_buffer[8192];

    LoadStringW(GetModuleHandleW(NULL), msg, msg_buffer,
        sizeof(msg_buffer)/sizeof(WCHAR));

    return taskkill_printfW(formatW, msg_buffer);
}

/* Post WM_CLOSE to all top-level windows belonging to the process with specified PID. */
static BOOL CALLBACK pid_enum_proc(HWND hwnd, LPARAM lParam)
{
    struct pid_close_info *info = (struct pid_close_info *)lParam;
    DWORD hwnd_pid;

    GetWindowThreadProcessId(hwnd, &hwnd_pid);

    if (hwnd_pid == info->pid)
    {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        info->found = TRUE;
    }

    return TRUE;
}

/* The implemented task enumeration and termination behavior does not
 * exactly match native behavior. On Windows:
 *
 * In the case of terminating by process name, specifying a particular
 * process name more times than the number of running instances causes
 * all instances to be terminated, but termination failure messages to
 * be printed as many times as the difference between the specification
 * quantity and the number of running instances.
 *
 * Successful terminations are all listed first in order, with failing
 * terminations being listed at the end.
 *
 * A PID of zero causes taskkill to warn about the inability to terminate
 * system processes. */
static int send_close_messages(void)
{
    unsigned int i;
    int status_code = 0;

    for (i = 0; i < task_count; i++)
    {
        WCHAR *p = task_list[i];
        BOOL is_numeric = TRUE;

        /* Determine whether the string is not numeric. */
        while (*p)
        {
            if (!isdigitW(*p++))
            {
                is_numeric = FALSE;
                break;
            }
        }

        if (is_numeric)
        {
            DWORD pid = atoiW(task_list[i]);
            struct pid_close_info info = { pid };

            EnumWindows(pid_enum_proc, (LPARAM)&info);
            if (info.found)
                taskkill_message_printfW(STRING_CLOSE_PID_SEARCH, pid);
            else
            {
                taskkill_message_printfW(STRING_SEARCH_FAILED, task_list[i]);
                status_code = 128;
            }
        }
        else
            WINE_FIXME("Termination by name is not implemented\n");
    }

    return status_code;
}

static BOOL add_to_task_list(WCHAR *name)
{
    static unsigned int list_size = 16;

    if (!task_list)
    {
        task_list = HeapAlloc(GetProcessHeap(), 0,
                                   list_size * sizeof(*task_list));
        if (!task_list)
            return FALSE;
    }
    else if (task_count == list_size)
    {
        void *realloc_list;

        list_size *= 2;
        realloc_list = HeapReAlloc(GetProcessHeap(), 0, task_list,
                                   list_size * sizeof(*task_list));
        if (!realloc_list)
            return FALSE;

        task_list = realloc_list;
    }

    task_list[task_count++] = name;
    return TRUE;
}

/* FIXME Argument processing does not match behavior observed on Windows.
 * Stringent argument counting and processing is performed, and unrecognized
 * options are detected as parameters when placed after options that accept one. */
static BOOL process_arguments(int argc, WCHAR *argv[])
{
    static const WCHAR slashForceTerminate[] = {'/','f',0};
    static const WCHAR slashImage[] = {'/','i','m',0};
    static const WCHAR slashPID[] = {'/','p','i','d',0};
    static const WCHAR slashHelp[] = {'/','?',0};

    if (argc > 1)
    {
        int i;
        BOOL has_im = 0, has_pid = 0;

        /* Only the lone help option is recognized. */
        if (argc == 2 && !strcmpW(slashHelp, argv[1]))
        {
            taskkill_message(STRING_USAGE);
            exit(0);
        }

        for (i = 1; i < argc; i++)
        {
            int got_im = 0, got_pid = 0;

            if (!strcmpiW(slashForceTerminate, argv[i]))
                force_termination = 1;
            /* Options /IM and /PID appear to behave identically, except for
             * the fact that they cannot be specified at the same time. */
            else if ((got_im = !strcmpiW(slashImage, argv[i])) ||
                     (got_pid = !strcmpiW(slashPID, argv[i])))
            {
                if (!argv[i + 1])
                {
                    taskkill_message_printfW(STRING_MISSING_PARAM, argv[i]);
                    taskkill_message(STRING_USAGE);
                    return FALSE;
                }

                if (got_im) has_im = 1;
                if (got_pid) has_pid = 1;

                if (has_im && has_pid)
                {
                    taskkill_message(STRING_MUTUAL_EXCLUSIVE);
                    taskkill_message(STRING_USAGE);
                    return FALSE;
                }

                if (!add_to_task_list(argv[i + 1]))
                    return FALSE;
                i++;
            }
            else
            {
                taskkill_message(STRING_INVALID_OPTION);
                taskkill_message(STRING_USAGE);
                return FALSE;
            }
        }
    }
    else
    {
        taskkill_message(STRING_MISSING_OPTION);
        taskkill_message(STRING_USAGE);
        return FALSE;
    }

    return TRUE;
}

int wmain(int argc, WCHAR *argv[])
{
    int status_code = 0;

    if (!process_arguments(argc, argv))
    {
        HeapFree(GetProcessHeap(), 0, task_list);
        return 1;
    }

    if (force_termination)
        WINE_FIXME("Forced termination is not implemented\n");
    else
        status_code = send_close_messages();

    HeapFree(GetProcessHeap(), 0, task_list);
    return status_code;
}