/*
 * Copyright 2010 Jacek Caban for CodeWeavers
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

#include <stdarg.h>

#define COBJMACROS

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <ole2.h>
#include <activscp.h>

#include <wine/debug.h>
#include <wine/unicode.h>

WINE_DEFAULT_DEBUG_CHANNEL(wscript);

static HRESULT WINAPI ActiveScriptSite_QueryInterface(IActiveScriptSite *iface,
                                                      REFIID riid, void **ppv)
{
    if(IsEqualGUID(riid, &IID_IUnknown)) {
        WINE_TRACE("(IID_IUnknown %p)\n", ppv);
        *ppv = iface;
    }else if(IsEqualGUID(riid, &IID_IActiveScriptSite)) {
        WINE_TRACE("(IID_IActiveScriptSite %p)\n", ppv);
        *ppv = iface;
    }else {
        *ppv = NULL;
        WINE_TRACE("(%s %p)\n", wine_dbgstr_guid(riid), ppv);
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI ActiveScriptSite_AddRef(IActiveScriptSite *iface)
{
    return 2;
}

static ULONG WINAPI ActiveScriptSite_Release(IActiveScriptSite *iface)
{
    return 1;
}

static HRESULT WINAPI ActiveScriptSite_GetLCID(IActiveScriptSite *iface, LCID *plcid)
{
    WINE_TRACE("()\n");

    *plcid = GetUserDefaultLCID();
    return S_OK;
}

static HRESULT WINAPI ActiveScriptSite_GetItemInfo(IActiveScriptSite *iface,
        LPCOLESTR pstrName, DWORD dwReturnMask, IUnknown **ppunkItem, ITypeInfo **ppti)
{
    WINE_FIXME("(%s %x %p %p)\n", wine_dbgstr_w(pstrName), dwReturnMask, ppunkItem, ppti);
    return E_NOTIMPL;
}

static HRESULT WINAPI ActiveScriptSite_GetDocVersionString(IActiveScriptSite *iface,
        BSTR *pbstrVersion)
{
    WINE_FIXME("()\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI ActiveScriptSite_OnScriptTerminate(IActiveScriptSite *iface,
        const VARIANT *pvarResult, const EXCEPINFO *pexcepinfo)
{
    WINE_FIXME("()\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI ActiveScriptSite_OnStateChange(IActiveScriptSite *iface,
        SCRIPTSTATE ssScriptState)
{
    WINE_TRACE("(%x)\n", ssScriptState);
    return S_OK;
}

static HRESULT WINAPI ActiveScriptSite_OnScriptError(IActiveScriptSite *iface,
        IActiveScriptError *pscripterror)
{
    WINE_FIXME("()\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI ActiveScriptSite_OnEnterScript(IActiveScriptSite *iface)
{
    WINE_TRACE("()\n");
    return S_OK;
}

static HRESULT WINAPI ActiveScriptSite_OnLeaveScript(IActiveScriptSite *iface)
{
    WINE_TRACE("()\n");
    return S_OK;
}

static IActiveScriptSiteVtbl ActiveScriptSiteVtbl = {
    ActiveScriptSite_QueryInterface,
    ActiveScriptSite_AddRef,
    ActiveScriptSite_Release,
    ActiveScriptSite_GetLCID,
    ActiveScriptSite_GetItemInfo,
    ActiveScriptSite_GetDocVersionString,
    ActiveScriptSite_OnScriptTerminate,
    ActiveScriptSite_OnStateChange,
    ActiveScriptSite_OnScriptError,
    ActiveScriptSite_OnEnterScript,
    ActiveScriptSite_OnLeaveScript
};

IActiveScriptSite script_site = { &ActiveScriptSiteVtbl };

static BOOL get_engine_clsid(const WCHAR *ext, CLSID *clsid)
{
    WCHAR fileid[64], progid[64];
    DWORD res;
    LONG size;
    HKEY hkey;
    HRESULT hres;

    static const WCHAR script_engineW[] =
        {'\\','S','c','r','i','p','t','E','n','g','i','n','e',0};

    res = RegOpenKeyW(HKEY_CLASSES_ROOT, ext, &hkey);
    if(res != ERROR_SUCCESS)
        return FALSE;

    size = sizeof(fileid)/sizeof(WCHAR);
    res = RegQueryValueW(hkey, NULL, fileid, &size);
    RegCloseKey(hkey);
    if(res != ERROR_SUCCESS)
        return FALSE;

    WINE_TRACE("fileid is %s\n", wine_dbgstr_w(fileid));

    strcatW(fileid, script_engineW);
    res = RegOpenKeyW(HKEY_CLASSES_ROOT, fileid, &hkey);
    if(res != ERROR_SUCCESS)
        return FALSE;

    size = sizeof(progid)/sizeof(WCHAR);
    res = RegQueryValueW(hkey, NULL, progid, &size);
    RegCloseKey(hkey);
    if(res != ERROR_SUCCESS)
        return FALSE;

    WINE_TRACE("ProgID is %s\n", wine_dbgstr_w(progid));

    hres = CLSIDFromProgID(progid, clsid);
    return SUCCEEDED(hres);
}

static HRESULT create_engine(CLSID *clsid, IActiveScript **script_ret,
        IActiveScriptParse **parser)
{
    IActiveScript *script;
    IUnknown *unk;
    HRESULT hres;

    hres = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER|CLSCTX_INPROC_HANDLER,
            &IID_IUnknown, (void**)&unk);
    if(FAILED(hres))
        return FALSE;

    hres = IUnknown_QueryInterface(unk, &IID_IActiveScript, (void**)&script);
    IUnknown_Release(unk);
    if(FAILED(hres))
        return FALSE;

    hres = IActiveScript_QueryInterface(script, &IID_IActiveScriptParse, (void**)parser);
    if(FAILED(hres)) {
        IActiveScript_Release(script);
        return FALSE;
    }

    *script_ret = script;
    return TRUE;
}

static HRESULT init_engine(IActiveScript *script, IActiveScriptParse *parser)
{
    HRESULT hres;

    static const WCHAR wscriptW[] = {'W','S','c','r','i','p','t',0};
    static const WCHAR wshW[] = {'W','S','H',0};

    hres = IActiveScript_SetScriptSite(script, &script_site);
    if(FAILED(hres))
        return FALSE;

    hres = IActiveScriptParse64_InitNew(parser);
    if(FAILED(hres))
        return FALSE;

    hres = IActiveScript_AddNamedItem(script, wscriptW, SCRIPTITEM_ISVISIBLE);
    if(FAILED(hres))
        return FALSE;

    hres = IActiveScript_AddNamedItem(script, wshW, SCRIPTITEM_ISVISIBLE);
    if(FAILED(hres))
        return FALSE;

    hres = IActiveScript_SetScriptState(script, SCRIPTSTATE_INITIALIZED);
    return SUCCEEDED(hres);
}

static BSTR get_script_str(const WCHAR *filename)
{
    const char *file_map;
    HANDLE file, map;
    DWORD size, len;
    BSTR ret;

    file = CreateFileW(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
    if(file == INVALID_HANDLE_VALUE)
        return NULL;

    size = GetFileSize(file, NULL);
    map = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(file);
    if(map == INVALID_HANDLE_VALUE)
        return NULL;

    file_map = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(map);
    if(!file_map)
        return NULL;

    len = MultiByteToWideChar(CP_ACP, 0, file_map, size, NULL, 0);
    ret = SysAllocStringLen(NULL, len);
    MultiByteToWideChar(CP_ACP, 0, file_map, size, ret, len);

    UnmapViewOfFile(file_map);
    return ret;
}

static void run_script(const WCHAR *filename, IActiveScript *script, IActiveScriptParse *parser)
{
    BSTR text;
    HRESULT hres;

    text = get_script_str(filename);
    if(!text) {
        WINE_FIXME("Could not get script text\n");
        return;
    }

    hres = IActiveScriptParse64_ParseScriptText(parser, text, NULL, NULL, NULL, 1, 1,
            SCRIPTTEXT_HOSTMANAGESSOURCE|SCRIPTITEM_ISVISIBLE, NULL, NULL);
    SysFreeString(text);
    if(FAILED(hres)) {
        WINE_FIXME("ParseScriptText failed: %08x\n", hres);
        return;
    }

    hres = IActiveScript_SetScriptState(script, SCRIPTSTATE_STARTED);
    if(FAILED(hres))
        WINE_FIXME("SetScriptState failed: %08x\n", hres);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPWSTR cmdline, int cmdshow)
{
    IActiveScriptParse *parser;
    IActiveScript *script;
    const WCHAR *ext;
    CLSID clsid;

    WINE_TRACE("(%p %p %s %x)\n", hInst, hPrevInst, wine_dbgstr_w(cmdline), cmdshow);

    if(!*cmdline)
        return 1;

    ext = strchrW(cmdline, '.');
    if(!ext)
        ext = cmdline;
    if(!get_engine_clsid(ext, &clsid)) {
        WINE_FIXME("Could not fine engine for %s\n", wine_dbgstr_w(ext));
        return 1;
    }

    CoInitialize(NULL);

    if(!create_engine(&clsid, &script, &parser)) {
        WINE_FIXME("Could not create script engine\n");
        CoUninitialize();
        return 1;
    }

    if(init_engine(script, parser)) {
        run_script(cmdline, script, parser);
        IActiveScript_Close(script);
    }else {
        WINE_FIXME("Script initialization failed\n");
    }

    IActiveScript_Release(script);
    IUnknown_Release(parser);

    CoUninitialize();

    return 0;
}