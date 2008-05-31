//  Copyright (C) 1999 AT&T Laboratories Cambridge. All Rights Reserved.
//
//  This file was part of the VNC system.
//
//  The VNC system is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//
// If the source code for the VNC system is not available from the place 
// whence you received this file, check http://www.uk.research.att.com/vnc or contact
// the authors on vnc@uk.research.att.com for information on obtaining it.
//
// This file has been adapted to the Win32 version of Apcupsd
// by Kern E. Sibbald.  Many thanks to ATT and James Weatherall,
// the original author, for providing an excellent template.
//
// Copyright (2000-2003) Kern E. Sibbald
//



// winStat.cpp

// Implementation of the Status dialog

#include <windows.h>

#include "winups.h"
#include "winstat.h"

extern void FillStatusBox(HWND hwnd, int id_list);

// Constructor/destructor
upsStatus::upsStatus()
{
    m_dlgvisible = FALSE;
}

upsStatus::~upsStatus()
{
}

// Initialisation
BOOL
upsStatus::Init()
{
    return TRUE;
}

// Dialog box handling functions
void
upsStatus::Show(BOOL show)
{
    if (show) {
        if (!m_dlgvisible) {
            DialogBoxParam(hAppInstance,
                        MAKEINTRESOURCE(IDD_STATUS),
                        NULL,
                        (DLGPROC)DialogProc,
                        (LONG)this);
        }
    }
}

BOOL CALLBACK
upsStatus::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // We use the dialog-box's USERDATA to store a _this pointer
    // This is set only once WM_INITDIALOG has been recieved, though!
    upsStatus *_this = (upsStatus *)GetWindowLong(hwnd, GWL_USERDATA);

    switch (uMsg) {
    case WM_INITDIALOG:
        // Retrieve the Dialog box parameter and use it as a pointer
        // to the calling vncProperties object
        SetWindowLong(hwnd, GWL_USERDATA, lParam);
        _this = (upsStatus *)lParam;

        // Show the dialog
        SetForegroundWindow(hwnd);

        _this->m_dlgvisible = TRUE;

        FillStatusBox(hwnd, IDC_LIST);

        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDCANCEL:
        case IDOK:
            // Close the dialog
            EndDialog(hwnd, TRUE);

            _this->m_dlgvisible = FALSE;

            return TRUE;
        }

        break;

    case WM_DESTROY:
        EndDialog(hwnd, FALSE);
        _this->m_dlgvisible = FALSE;
        return TRUE;
    }
    return 0;
}