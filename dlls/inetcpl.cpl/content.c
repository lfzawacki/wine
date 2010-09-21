/*
 * Internet control panel applet: content propsheet
 *
 * Copyright 2010 Detlef Riekenberg
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
 *
 */

#define NONAMELESSUNION

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <cryptuiapi.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(inetcpl);

/*********************************************************************
 * display_cert_manager (internal)
 *
 * call cryptui to display a specific certificate manager dialog
 *
 */
static BOOL display_cert_manager(HWND parent, DWORD flags)
{
    CRYPTUI_CERT_MGR_STRUCT dlg;

    TRACE("(%p, 0x%x)\n", parent, flags);

    ZeroMemory(&dlg, sizeof(CRYPTUI_CERT_MGR_STRUCT));
    dlg.dwSize = sizeof(CRYPTUI_CERT_MGR_STRUCT);
    dlg.hwndParent = parent;
    dlg.dwFlags = flags;

    return CryptUIDlgCertMgr(&dlg);
}

/*********************************************************************
 * LaunchSiteCertDialog (inetcpl.@)
 *
 * Launch a dialog to manage personal certificates
 *
 * PARAMS
 *  hWnd    [I] Handle for the parrent window
 *
 * RETURNS
 *  Failure: FALSE
 *  Success: TRUE
 *
 * NOTES
 *  rundll32 callable function: rundll32 inetcpl.cpl,LaunchSiteCertDialog
 *
 */
BOOL WINAPI LaunchSiteCertDialog(HWND parent)
{
    return display_cert_manager(parent, 0);
}