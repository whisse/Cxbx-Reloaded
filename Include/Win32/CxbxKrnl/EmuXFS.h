// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->EmuXFS.h
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
#ifndef EMUXFS_H
#define EMUXFS_H

// ******************************************************************
// * func: EmuXSwapFS
// ******************************************************************
// *
// * This function is used to swap between the native Win2k/XP FS:
// * structure, and the EmuX FS: structure. Before running Windows
// * code, you *must* swap over to Win2k/XP FS. Similarly, before
// * running Xbox code, you *must* swap back over to EmuX FS.
// *
// ******************************************************************
static inline void EmuXSwapFS()
{
    __asm
    {
        mov ax, fs:[0x14]   // FS.ArbitraryUserPointer
        mov fs, ax
    }
}

// ******************************************************************
// * func: EmuXGenerateFS
// ******************************************************************
void EmuXGenerateFS();

// ******************************************************************
// * func: EmuXInitFS
// ******************************************************************
void EmuXInitFS();

#endif