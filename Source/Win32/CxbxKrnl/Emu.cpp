// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->Emu.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_LOCAL_

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace xboxkrnl
{
    #include <xboxkrnl/xboxkrnl.h>
};

#include "Emu.h"
#include "EmuFS.h"
#include "EmuD3D8.h"
#include "EmuKrnl.h"
#include "EmuShared.h"
#include "HLEDataBase.h"

// ******************************************************************
// * global / static
// ******************************************************************
extern Xbe::TLS *g_pTLS = 0;
extern void     *g_pTLSData = 0;

// ******************************************************************
// * static
// ******************************************************************
static void *EmuLocateFunction(OOVPA *Oovpa, uint32 lower, uint32 upper);
static void  EmuInstallWrappers(OOVPATable *OovpaTable, uint32 OovpaTableSize, void (*Entry)(), Xbe::Header *pXbeHeader);
static int   ExitException(LPEXCEPTION_POINTERS e);

// ******************************************************************
// * func: DllMain
// ******************************************************************
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if(fdwReason == DLL_PROCESS_ATTACH)
        EmuShared::Init();
    
    if(fdwReason == DLL_PROCESS_DETACH)
        EmuShared::Cleanup();

    return TRUE;
}

// ******************************************************************
// * func: EmuNoFunc
// ******************************************************************
extern "C" CXBXKRNL_API void NTAPI EmuNoFunc()
{
    EmuSwapFS();   // Win2k/XP FS

    printf("Emu (0x%.08X): EmuNoFunc()\n", GetCurrentThreadId());

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuCleanThread
// ******************************************************************
static void EmuCleanThread()
{
    if(EmuIsXboxFS())
        EmuSwapFS();    // Win2k/XP FS

    EmuCleanupFS();

    TerminateThread(GetCurrentThread(), 0);
}

// ******************************************************************
// * func: EmuInit
// ******************************************************************
extern "C" CXBXKRNL_API void NTAPI EmuInit
(
    void                   *pTLSData, 
    Xbe::TLS               *pTLS,
    Xbe::LibraryVersion    *pLibraryVersion,
    DebugMode               DbgMode,
    char                   *szDebugFilename,
    Xbe::Header            *pXbeHeader,
    uint32                  dwXbeHeaderSize,
    void                  (*Entry)())
{
    g_pTLS = pTLS;
    g_pTLSData = pTLSData;

    // ******************************************************************
    // * debug console allocation (if configured)
    // ******************************************************************
    if(DbgMode == DM_CONSOLE)
    {
        if(AllocConsole())
        {
            freopen("CONOUT$", "wt", stdout);

            SetConsoleTitle("Cxbx : Kernel Debug Console");

            SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);
            
            printf("CxbxKrnl (0x%.08X): Debug console allocated (DM_CONSOLE).\n", GetCurrentThreadId());
        }
    }
    else if(DbgMode == DM_FILE)
    {
        FreeConsole();

        freopen(szDebugFilename, "wt", stdout);

        printf("Emu (0x%.08X): Debug console allocated (DM_FILE).\n", GetCurrentThreadId());
    }
    else
    {
        FreeConsole();

        char buffer[16];

        if(GetConsoleTitle(buffer, 16) != NULL)
            freopen("nul", "w", stdout);
    }

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    {
        #ifdef _DEBUG_TRACE
        printf("CxbxKrnl (0x%.08X): _DEBUG_TRACE enabled.\n", GetCurrentThreadId());

        printf("Emu: EmuInit\n"
               "(\n"
               "   pTLSData            : 0x%.08X\n"
               "   pTLS                : 0x%.08X\n"
               "   pLibraryVersion     : 0x%.08X\n"
               "   DebugConsole        : 0x%.08X\n"
               "   DebugFilename       : \"%s\"\n"
               "   pXBEHeader          : 0x%.08X\n"
               "   pXBEHeaderSize      : 0x%.08X\n"
               "   Entry               : 0x%.08X\n"
               ");\n",
               pTLSData, pTLS, pLibraryVersion, DbgMode, szDebugFilename, pXbeHeader, dwXbeHeaderSize, Entry);

        #else
        printf("CxbxKrnl (0x%.08X): _DEBUG_TRACE disabled.\n", GetCurrentThreadId());
        #endif
    }

    // ******************************************************************
    // * Load the necessary pieces of XBEHeader
    // ******************************************************************
    {
        Xbe::Header *MemXbeHeader = (Xbe::Header*)0x00010000;

        uint32 old_protection = 0;

        VirtualProtect(MemXbeHeader, 0x1000, PAGE_READWRITE, &old_protection);

        // we sure hope we aren't corrupting anything necessary for an .exe to survive :]
        MemXbeHeader->dwSizeofHeaders   = pXbeHeader->dwSizeofHeaders;
        MemXbeHeader->dwCertificateAddr = pXbeHeader->dwCertificateAddr;
        MemXbeHeader->dwPeHeapReserve   = pXbeHeader->dwPeHeapReserve;
        MemXbeHeader->dwPeHeapCommit    = pXbeHeader->dwPeHeapCommit;

        memcpy(&MemXbeHeader->dwInitFlags, &pXbeHeader->dwInitFlags, sizeof(pXbeHeader->dwInitFlags));

        memcpy((void*)pXbeHeader->dwCertificateAddr, &((uint08*)pXbeHeader)[pXbeHeader->dwCertificateAddr - 0x00010000], sizeof(Xbe::Certificate));
    }

    // ******************************************************************
    // * Initialize all components
    // ******************************************************************
    {
        EmuInitFS();

        EmuGenerateFS(pTLS, pTLSData);
    }

    // ******************************************************************
    // * Initialize OpenXDK emulation
    // ******************************************************************
    if(pLibraryVersion == 0)
        printf("Emu: Detected OpenXDK application...\n");

    uint32 xca;
    uint32 xcz;

    // ******************************************************************
    // * Initialize Microsoft XDK emulation
    // ******************************************************************
    if(pLibraryVersion != 0)
    {
        printf("Emu: Detected Microsoft XDK application...\n");

        uint32 dwLibraryVersions = pXbeHeader->dwLibraryVersions;
        uint32 dwHLEEntries      = HLEDataBaseSize/sizeof(HLEData);

        for(uint32 v=0;v<dwLibraryVersions;v++)
        {
            uint16 MajorVersion = pLibraryVersion[v].wMajorVersion;
            uint16 MinorVersion = pLibraryVersion[v].wMinorVersion;
            uint16 BuildVersion = pLibraryVersion[v].wBuildVersion;

            char szLibraryName[9] = {0};

            for(uint32 c=0;c<8;c++)
                szLibraryName[c] = pLibraryVersion[v].szName[c];

            printf("Emu: Locating HLE Information for %s %d.%d.%d...", szLibraryName, MajorVersion, MinorVersion, BuildVersion);

            bool found=false;

            for(uint32 d=0;d<dwHLEEntries;d++)
            {
                if(BuildVersion != HLEDataBase[d].BuildVersion || MinorVersion != HLEDataBase[d].MinorVersion || MajorVersion != HLEDataBase[d].MajorVersion || strcmp(szLibraryName, HLEDataBase[d].Library) != 0)
                    continue;

                found = true;

                printf("Found\n");

                EmuInstallWrappers(HLEDataBase[d].OovpaTable, HLEDataBase[d].OovpaTableSize, Entry, pXbeHeader);
            }

            if(!found)
                printf("Skipped\n");

            if(strcmp("XAPILIB", szLibraryName) == 0 && MajorVersion == 1 && MinorVersion == 0 && (BuildVersion == 4627 || BuildVersion == 4361 || BuildVersion == 4034 || BuildVersion == 3911))
            {
                printf("Emu: Locating Pre-Entry Execution...");

                uint32 lower = pXbeHeader->dwBaseAddr;
                uint32 upper = pXbeHeader->dwBaseAddr + pXbeHeader->dwSizeofImage;

                void *pFunc = EmuLocateFunction((OOVPA*)&__cinit_1_0_3911, lower, upper);

                if(pFunc == 0)
                    printf("Skipped\n");
                else
                {
                    xca = *(uint32*)((uint32)pFunc + 0x32);
                    xcz = *(uint32*)((uint32)pFunc + 0x39);

                    printf("Found! @ 0x%.08X -> 0x%.08X\n", xca, xcz);
                }
            }
        }

        EmuD3DInit(pXbeHeader, dwXbeHeaderSize);
    }

    // ******************************************************************
    // * Static/Constructors
    // ******************************************************************
    for(uint32 v=xca;v<xcz; v+=4)
    {
        uint32 pFunc = *(uint32*)v;

        if(pFunc != 0 && pFunc != -1)
        {
            printf("Emu (0x%.08X): Static/Constructor @ 0x%.08X\n", GetCurrentThreadId(), pFunc);

            EmuSwapFS();    // Xbox FS

            __asm call pFunc

            EmuSwapFS();    // Win2k/XP FS
        }
    }

    printf("Emu (0x%.08X): Initial thread starting.\n", GetCurrentThreadId());

    // ******************************************************************
    // * Entry Point
    // ******************************************************************
    __try
    {
        EmuSwapFS();   // XBox FS

        Entry();

        EmuSwapFS();   // Win2k/XP FS
    }
    __except(EmuException(GetExceptionInformation()))
    {
        printf("Emu: WARNING!! Problem with ExceptionFilter\n");
    }

    printf("Emu (0x%.08X): Initial thread ended.\n", GetCurrentThreadId());

    fflush(stdout);

    EmuCleanThread();

    return;
}

// ******************************************************************
// * func: EmuCleanup
// ******************************************************************
extern "C" CXBXKRNL_API void NTAPI EmuCleanup(const char *szErrorMessage)
{
    // ******************************************************************
    // * Print out ErrorMessage (if exists)
    // ******************************************************************
    if(szErrorMessage != NULL)
    {
        char buffer[255];

        sprintf(buffer, "CxbxKrnl: Recieved Fatal Message \"%s\"\n", szErrorMessage);

        printf("%s", buffer);

        MessageBox(NULL, buffer, "CxbxKrnl", MB_OK | MB_ICONEXCLAMATION);
    }

    printf("CxbxKrnl: Terminating Process\n");
    fflush(stdout);

    // ******************************************************************
    // * Cleanup debug output
    // ******************************************************************
    {
        FreeConsole();

        char buffer[16];

        if(GetConsoleTitle(buffer, 16) != NULL)
            freopen("nul", "w", stdout);
    }

    TerminateProcess(GetCurrentProcess(), 0);

    return;
}

// ******************************************************************
// * func: EmuPanic
// ******************************************************************
extern "C" CXBXKRNL_API void NTAPI EmuPanic()
{
    if(EmuIsXboxFS())
        EmuSwapFS();   // Win2k/XP FS

    printf("Emu (0x%.08X): EmuPanic()\n", GetCurrentThreadId());

    EmuCleanup("Kernel Panic!");

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuInstallWrapper
// ******************************************************************
inline void EmuInstallWrapper(void *FunctionAddr, void *WrapperAddr)
{
    uint08 *FuncBytes = (uint08*)FunctionAddr;

    *(uint08*)&FuncBytes[0] = 0xE9;
    *(uint32*)&FuncBytes[1] = (uint32)WrapperAddr - (uint32)FunctionAddr - 5;
}

// ******************************************************************
// * func: EmuLocateFunction
// ******************************************************************
void *EmuLocateFunction(OOVPA *Oovpa, uint32 lower, uint32 upper)
{
    uint32 count = Oovpa->Count;

    // ******************************************************************
    // * Large
    // ******************************************************************
    if(Oovpa->Large == 1)
    {
        LOOVPA<1> *Loovpa = (LOOVPA<1>*)Oovpa;

        upper -= Loovpa->Lovp[count-1].Offset;

        // ******************************************************************
        // * Search all of the image memory
        // ******************************************************************
        for(uint32 cur=lower;cur<upper;cur++)
        {
            uint32  v=0;

            // ******************************************************************
            // * check all pairs, moving on if any do not match
            // ******************************************************************
            for(v=0;v<count;v++)
            {
                uint32 Offset = Loovpa->Lovp[v].Offset;
                uint32 Value  = Loovpa->Lovp[v].Value;

                uint08 RealValue = *(uint08*)(cur + Offset);

                if(RealValue != Value)
                    break;
            }

            // ******************************************************************
            // * success if we found all pairs
            // ******************************************************************
            if(v == count)
                return (void*)cur;
        }
    }
    // ******************************************************************
    // * Small
    // ******************************************************************
    else
    {
        SOOVPA<1> *Soovpa = (SOOVPA<1>*)Oovpa;

        upper -= Soovpa->Sovp[count-1].Offset;

        // ******************************************************************
        // * Search all of the image memory
        // ******************************************************************
        for(uint32 cur=lower;cur<upper;cur++)
        {
            uint32  v=0;

            // ******************************************************************
            // * check all pairs, moving on if any do not match
            // ******************************************************************
            for(v=0;v<count;v++)
            {
                uint32 Offset = Soovpa->Sovp[v].Offset;
                uint32 Value  = Soovpa->Sovp[v].Value;

                uint08 RealValue = *(uint08*)(cur + Offset);

                if(RealValue != Value)
                    break;
            }

            // ******************************************************************
            // * success if we found all pairs
            // ******************************************************************
            if(v == count)
                return (void*)cur;
        }
    }

    return 0;
}

// ******************************************************************
// * func: EmuInstallWrappers
// ******************************************************************
void EmuInstallWrappers(OOVPATable *OovpaTable, uint32 OovpaTableSize, void (*Entry)(), Xbe::Header *pXbeHeader)
{
    uint32 lower = pXbeHeader->dwBaseAddr;
    uint32 upper = pXbeHeader->dwBaseAddr + pXbeHeader->dwSizeofImage;

    // ******************************************************************
    // * traverse the full OOVPA table
    // ******************************************************************
    for(uint32 a=0;a<OovpaTableSize/sizeof(OOVPATable);a++)
    {
        #ifdef _DEBUG_TRACE
        printf("Emu: Searching for %s...", OovpaTable[a].szFuncName);
        #endif

        OOVPA *Oovpa = OovpaTable[a].Oovpa;

        void *pFunc = EmuLocateFunction(Oovpa, lower, upper);

        if(pFunc != 0)
        {
            #ifdef _DEBUG_TRACE
            printf("Found @ 0x%.08X\n", pFunc);
            #endif

            EmuInstallWrapper(pFunc, OovpaTable[a].lpRedirect);
        }
        else
        {
            printf("None (OK)\n");
        }
    }
}

// ******************************************************************
// * func: EmuException
// ******************************************************************
int EmuException(LPEXCEPTION_POINTERS e)
{
	// ******************************************************************
	// * Debugging Information
	// ******************************************************************
	{
		printf("\n");
		printf("CxbxKrnl(0x%.08X): Recieved Exception : 0x%.08X\n", GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode);
		printf("\n");
	}

	// ******************************************************************
	// * Notify User
	// ******************************************************************
	{
		char buffer[256];

		sprintf(buffer, "Recieved Exception [0x%.08X]\n\nPress 'OK' to terminate emulation.\nPress 'Cancel' to debug.", e->ExceptionRecord->ExceptionCode);

		if(MessageBox(NULL, buffer, "Cxbx", MB_ICONSTOP | MB_OKCANCEL) == IDOK)
			ExitProcess(1);
	}

    return EXCEPTION_CONTINUE_SEARCH;
}


// ******************************************************************
// * func: ExitException
// ******************************************************************
int ExitException(LPEXCEPTION_POINTERS e)
{
    static int count = 0;

    // ******************************************************************
	// * Debugging Information
	// ******************************************************************
	{
		printf("\n");
		printf("Recieved Exit Exception : 0x%.08X\n", e->ExceptionRecord->ExceptionCode);
		printf("\n");
	}

    MessageBox(NULL, "Warning: Could not safely terminate process!", "Cxbx", MB_OK);

    count++;

    if(count > 1)
    {
        MessageBox(NULL, "Warning: Multiple Problems!", "Cxbx", MB_OK);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    ExitProcess(1);

    return EXCEPTION_CONTINUE_SEARCH;
}