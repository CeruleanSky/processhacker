/*
 * Process Hacker -
 *   column chooser
 *
 * Copyright (C) 2010 wj32
 * Copyright (C) 2017-2020 dmex
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <phapp.h>
#include <settings.h>
#include <phsettings.h>

typedef struct _COLUMNS_DIALOG_CONTEXT
{
    HWND ControlHandle;
    HFONT ControlFont;
    ULONG Type;
    PPH_LIST Columns;

    ULONG CXWidth;
    HBRUSH BrushNormal;
    HBRUSH BrushPushed;
    HBRUSH BrushHot;
    COLORREF TextColor;

    HWND InactiveWindowHandle;
    HWND ActiveWindowHandle;
    HWND SearchInactiveHandle;
    HWND SearchActiveHandle;
    PPH_LIST InactiveListArray;
    PPH_LIST ActiveListArray;
    PPH_STRING InactiveSearchboxText;
    PPH_STRING ActiveSearchboxText;

} COLUMNS_DIALOG_CONTEXT, *PCOLUMNS_DIALOG_CONTEXT;

INT_PTR CALLBACK PhpColumnsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

VOID PhShowChooseColumnsDialog(
    _In_ HWND ParentWindowHandle,
    _In_ HWND ControlHandle,
    _In_ ULONG Type
    )
{
    COLUMNS_DIALOG_CONTEXT context;

    memset(&context, 0, sizeof(COLUMNS_DIALOG_CONTEXT));
    context.ControlHandle = ControlHandle;
    context.Type = Type;

    if (Type == PH_CONTROL_TYPE_TREE_NEW)
        context.Columns = PhCreateList(TreeNew_GetColumnCount(ControlHandle));
    else
        return;

    DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_CHOOSECOLUMNS),
        ParentWindowHandle,
        PhpColumnsDlgProc,
        (LPARAM)&context
        );

    PhDereferenceObject(context.Columns);
}

static int __cdecl PhpColumnsCompareDisplayIndexTn(
    _In_ const void *elem1,
    _In_ const void *elem2
    )
{
    PPH_TREENEW_COLUMN column1 = *(PPH_TREENEW_COLUMN *)elem1;
    PPH_TREENEW_COLUMN column2 = *(PPH_TREENEW_COLUMN *)elem2;

    return uintcmp(column1->DisplayIndex, column2->DisplayIndex);
}

static int __cdecl PhpInactiveColumnsCompareNameTn(
    _In_ const void *elem1,
    _In_ const void *elem2
    )
{
    PWSTR column1 = *(PWSTR *)elem1;
    PWSTR column2 = *(PWSTR *)elem2;

    return PhCompareStringZ(column1, column2, FALSE);
}

_Success_(return != ULONG_MAX)
static ULONG IndexOfStringInList(
    _In_ PPH_LIST List,
    _In_ PWSTR String
    )
{
    for (ULONG i = 0; i < List->Count; i++)
    {
        if (PhEqualStringZ(List->Items[i], String, FALSE))
            return i;
    }

    return ULONG_MAX;
}

static HFONT PhpColumnsGetCurrentFont(
    VOID
    )
{
    HFONT result = NULL;
    LOGFONT font;
    PPH_STRING fontHexString;

    fontHexString = PhGetStringSetting(L"Font");

    if (fontHexString->Length / sizeof(WCHAR) / 2 == sizeof(LOGFONT))
    {
        if (PhHexStringToBuffer(&fontHexString->sr, (PUCHAR)&font))
        {
            result = CreateFontIndirect(&font);
        }
    }

    return result;
}

BOOLEAN PhpColumnsWordMatchStringRef(
    _In_ PPH_STRING SearchboxText,
    _In_ PPH_STRINGREF Text
    )
{
    PH_STRINGREF part;
    PH_STRINGREF remainingPart;

    remainingPart = SearchboxText->sr;

    while (remainingPart.Length)
    {
        PhSplitStringRefAtChar(&remainingPart, L'|', &part, &remainingPart);

        if (part.Length)
        {
            if (PhFindStringInStringRef(Text, &part, TRUE) != -1)
                return TRUE;
        }
    }

    return FALSE;
}

INT_PTR CALLBACK PhpColumnsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PCOLUMNS_DIALOG_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PCOLUMNS_DIALOG_CONTEXT)lParam;
        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
    }

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            ULONG count;
            ULONG total;
            ULONG i;
            PPH_LIST displayOrderList = NULL;

            PhCenterWindow(hwndDlg, GetParent(hwndDlg));
            PhSetApplicationWindowIcon(hwndDlg);

            context->InactiveWindowHandle = GetDlgItem(hwndDlg, IDC_INACTIVE);
            context->ActiveWindowHandle = GetDlgItem(hwndDlg, IDC_ACTIVE);
            context->SearchInactiveHandle = GetDlgItem(hwndDlg, IDC_SEARCH);
            context->SearchActiveHandle = GetDlgItem(hwndDlg, IDC_FILTER);
            context->InactiveListArray = PhCreateList(1);
            context->ActiveListArray = PhCreateList(1);
            context->ControlFont = PhpColumnsGetCurrentFont();
            context->InactiveSearchboxText = PhReferenceEmptyString();
            context->ActiveSearchboxText = PhReferenceEmptyString();

            PhCreateSearchControl(hwndDlg, context->SearchInactiveHandle, L"Inactive columns...");
            PhCreateSearchControl(hwndDlg, context->SearchActiveHandle, L"Active columns...");

            if (context->ControlFont)
            {
                SetWindowFont(context->InactiveWindowHandle, context->ControlFont, TRUE);
                SetWindowFont(context->ActiveWindowHandle, context->ControlFont, TRUE);
            }

            if (PhGetIntegerSetting(L"EnableThemeSupport"))
            {
                context->CXWidth = PH_SCALE_DPI(16);
                context->BrushNormal = CreateSolidBrush(RGB(43, 43, 43));
                context->BrushHot = CreateSolidBrush(RGB(128, 128, 128));
                context->BrushPushed = CreateSolidBrush(RGB(153, 209, 255));
                context->TextColor = RGB(0xff, 0xff, 0xff);
                //context->FontHandle = PhDuplicateFont(GetWindowFont(Phmain));
            }
            else
            {
                context->CXWidth = PH_SCALE_DPI(16);
                context->BrushNormal = GetSysColorBrush(COLOR_WINDOW);
                context->BrushHot = CreateSolidBrush(RGB(145, 201, 247));
                context->BrushPushed = CreateSolidBrush(RGB(153, 209, 255));
                context->TextColor = GetSysColor(COLOR_WINDOWTEXT);
                //context->FontHandle = PhDuplicateFont(GetWindowFont(ToolBarHandle));
            }

            if (context->Type == PH_CONTROL_TYPE_TREE_NEW)
            {
                PH_TREENEW_COLUMN column;

                count = 0;
                total = TreeNew_GetColumnCount(context->ControlHandle);
                i = 0;

                displayOrderList = PhCreateList(total);

                while (count < total)
                {
                    if (TreeNew_GetColumn(context->ControlHandle, i, &column))
                    {
                        PPH_TREENEW_COLUMN copy;

                        if (column.Fixed)
                        {
                            i++;
                            total--;
                            continue;
                        }

                        copy = PhAllocateCopy(&column, sizeof(PH_TREENEW_COLUMN));
                        PhAddItemList(context->Columns, copy);
                        count++;

                        if (column.Visible)
                        {
                            PhAddItemList(displayOrderList, copy);
                        }
                        else
                        {
                            PhAddItemList(context->InactiveListArray, column.Text);
                        }
                    }

                    i++;
                }

                qsort(displayOrderList->Items, displayOrderList->Count, sizeof(PVOID), PhpColumnsCompareDisplayIndexTn);
            }

            qsort(context->InactiveListArray->Items, context->InactiveListArray->Count, sizeof(PVOID), PhpInactiveColumnsCompareNameTn);

            for (i = 0; i < context->InactiveListArray->Count; i++)
            {
                ListBox_InsertItemData(context->InactiveWindowHandle, i, context->InactiveListArray->Items[i]);
            }

            if (displayOrderList)
            {
                for (i = 0; i < displayOrderList->Count; i++)
                {
                    if (context->Type == PH_CONTROL_TYPE_TREE_NEW)
                    {
                        PPH_TREENEW_COLUMN copy = displayOrderList->Items[i];

                        PhAddItemList(context->ActiveListArray, copy->Text);
                        ListBox_InsertItemData(context->ActiveWindowHandle, i, copy->Text);
                    }
                }

                PhDereferenceObject(displayOrderList);
            }

            SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_INACTIVE, LBN_SELCHANGE), (LPARAM)context->InactiveWindowHandle);
            SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_ACTIVE, LBN_SELCHANGE), (LPARAM)context->ActiveWindowHandle);

            PhInitializeWindowTheme(hwndDlg, PhEnableThemeSupport);

            PhSetDialogFocus(hwndDlg, GetDlgItem(hwndDlg, IDCANCEL));
        }
        break;
    case WM_DESTROY:
        {
            for (ULONG i = 0; i < context->Columns->Count; i++)
                PhFree(context->Columns->Items[i]);

            if (context->BrushHot)
                DeleteBrush(context->BrushHot);
            if (context->BrushPushed)
                DeleteBrush(context->BrushPushed);
            if (context->ControlFont)
                DeleteFont(context->ControlFont);
            if (context->InactiveListArray)
                PhDereferenceObject(context->InactiveListArray);
            if (context->ActiveListArray)
                PhDereferenceObject(context->ActiveListArray);
            if (context->InactiveSearchboxText)
                PhDereferenceObject(context->InactiveSearchboxText);
            if (context->ActiveSearchboxText)
                PhDereferenceObject(context->ActiveSearchboxText);

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_CMD(wParam, lParam))
            {
            case EN_CHANGE:
                {
                    SendMessage(context->InactiveWindowHandle, WM_SETREDRAW, FALSE, 0);
                    SendMessage(context->ActiveWindowHandle, WM_SETREDRAW, FALSE, 0);

                    if (GET_WM_COMMAND_HWND(wParam, lParam) == context->SearchInactiveHandle)
                    {
                        PPH_STRING newSearchboxText;

                        newSearchboxText = PH_AUTO(PhGetWindowText(context->SearchInactiveHandle));

                        if (!PhEqualString(context->InactiveSearchboxText, newSearchboxText, FALSE))
                        {
                            PhSwapReference(&context->InactiveSearchboxText, newSearchboxText);

                            ListBox_ResetContent(context->InactiveWindowHandle);

                            if (PhIsNullOrEmptyString(context->InactiveSearchboxText))
                            {
                                for (ULONG i = 0; i < context->InactiveListArray->Count; i++)
                                    ListBox_InsertItemData(context->InactiveWindowHandle, i, context->InactiveListArray->Items[i]);
                            }
                            else
                            {
                                ULONG index = 0;

                                for (ULONG i = 0; i < context->InactiveListArray->Count; i++)
                                {
                                    PH_STRINGREF text;

                                    PhInitializeStringRefLongHint(&text, context->InactiveListArray->Items[i]);

                                    if (PhpColumnsWordMatchStringRef(context->InactiveSearchboxText, &text))
                                    {
                                        ListBox_InsertItemData(context->InactiveWindowHandle, index, context->InactiveListArray->Items[i]);
                                        index++;
                                    }
                                }
                            }
                        }
                    }
                    else if (GET_WM_COMMAND_HWND(wParam, lParam) == context->SearchActiveHandle)
                    {
                        PPH_STRING newSearchboxText;

                        newSearchboxText = PH_AUTO(PhGetWindowText(context->SearchActiveHandle));

                        if (!PhEqualString(context->ActiveSearchboxText, newSearchboxText, FALSE))
                        {
                            PhSwapReference(&context->ActiveSearchboxText, newSearchboxText);

                            ListBox_ResetContent(context->ActiveWindowHandle);

                            if (PhIsNullOrEmptyString(context->ActiveSearchboxText))
                            {
                                for (ULONG i = 0; i < context->ActiveListArray->Count; i++)
                                    ListBox_InsertItemData(context->ActiveWindowHandle, i, context->ActiveListArray->Items[i]);
                            }
                            else
                            {
                                ULONG index = 0;

                                for (ULONG i = 0; i < context->ActiveListArray->Count; i++)
                                {
                                    PH_STRINGREF text;

                                    PhInitializeStringRefLongHint(&text, context->ActiveListArray->Items[i]);

                                    if (PhpColumnsWordMatchStringRef(context->ActiveSearchboxText, &text))
                                    {
                                        ListBox_InsertItemData(context->ActiveWindowHandle, index, context->ActiveListArray->Items[i]);
                                        index++;
                                    }
                                }
                            }
                        }
                    }

                    SendMessage(context->InactiveWindowHandle, WM_SETREDRAW, TRUE, 0);
                    SendMessage(context->ActiveWindowHandle, WM_SETREDRAW, TRUE, 0);
                }
                break;
            }

            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
                EndDialog(hwndDlg, IDCANCEL);
                break;
            case IDOK:
                {
                    #define ORDER_LIMIT 200
                    ULONG i;
                    INT orderArray[ORDER_LIMIT];
                    INT maxOrder;

                    memset(orderArray, 0, sizeof(orderArray));
                    maxOrder = 0;

                    if (context->Type == PH_CONTROL_TYPE_TREE_NEW)
                    {
                        // Apply visiblity settings and build the order array.

                        TreeNew_SetRedraw(context->ControlHandle, FALSE);

                        for (i = 0; i < context->Columns->Count; i++)
                        {
                            PPH_TREENEW_COLUMN column = context->Columns->Items[i];
                            ULONG index;

                            index = IndexOfStringInList(context->ActiveListArray, column->Text);
                            column->Visible = index != ULONG_MAX;

                            TreeNew_SetColumn(context->ControlHandle, TN_COLUMN_FLAG_VISIBLE, column);

                            if (column->Visible && index < ORDER_LIMIT)
                            {
                                orderArray[index] = column->Id;

                                if ((ULONG)maxOrder < index + 1)
                                    maxOrder = index + 1;
                            }
                        }

                        // Apply display order.
                        TreeNew_SetColumnOrderArray(context->ControlHandle, maxOrder, orderArray);

                        TreeNew_SetRedraw(context->ControlHandle, TRUE);

                        InvalidateRect(context->ControlHandle, NULL, FALSE);
                    }

                    EndDialog(hwndDlg, IDOK);
                }
                break;
            case IDC_INACTIVE:
                {
                    switch (HIWORD(wParam))
                    {
                    case LBN_DBLCLK:
                        {
                            SendMessage(hwndDlg, WM_COMMAND, IDC_SHOW, 0);
                        }
                        break;
                    case LBN_SELCHANGE:
                        {
                            INT sel = ListBox_GetCurSel(context->InactiveWindowHandle);

                            EnableWindow(GetDlgItem(hwndDlg, IDC_SHOW), sel != -1);
                        }
                        break;
                    }
                }
                break;
            case IDC_ACTIVE:
                {
                    switch (HIWORD(wParam))
                    {
                    case LBN_DBLCLK:
                        {
                            SendMessage(hwndDlg, WM_COMMAND, IDC_HIDE, 0);
                        }
                        break;
                    case LBN_SELCHANGE:
                        {
                            INT sel = ListBox_GetCurSel(context->ActiveWindowHandle);
                            INT count = ListBox_GetCount(context->ActiveWindowHandle);
                            INT total = context->ActiveListArray->Count;

                            EnableWindow(GetDlgItem(hwndDlg, IDC_HIDE), sel != -1 && total != 1);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEUP), sel != 0 && sel != -1);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEDOWN), sel != count - 1 && sel != -1);
                        }
                        break;
                    }
                }
                break;
            case IDC_SHOW:
                {
                    INT sel;
                    INT count;
                    PWSTR string;

                    sel = ListBox_GetCurSel(context->InactiveWindowHandle);
                    count = ListBox_GetCount(context->InactiveWindowHandle);

                    if (string = (PWSTR)ListBox_GetItemData(context->InactiveWindowHandle, sel))
                    {
                        ULONG index = IndexOfStringInList(context->InactiveListArray, string);

                        if (index != ULONG_MAX)
                        {
                            PVOID item = context->InactiveListArray->Items[index];

                            PhRemoveItemsList(context->InactiveListArray, index, 1);
                            PhAddItemList(context->ActiveListArray, item);

                            ListBox_DeleteString(context->InactiveWindowHandle, sel);
                            ListBox_AddItemData(context->ActiveWindowHandle, item);
                        }

                        count--;

                        if (sel >= count - 1)
                            sel = count - 1;

                        ListBox_SetCurSel(context->InactiveWindowHandle, sel);

                        SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_INACTIVE, LBN_SELCHANGE), (LPARAM)context->InactiveWindowHandle);
                        SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_ACTIVE, LBN_SELCHANGE), (LPARAM)context->ActiveWindowHandle);
                    }
                }
                break;
            case IDC_HIDE:
                {
                    INT sel;
                    INT count;
                    INT total;
                    PWSTR string;

                    sel = ListBox_GetCurSel(context->ActiveWindowHandle);
                    count = ListBox_GetCount(context->ActiveWindowHandle);
                    total = context->ActiveListArray->Count;

                    if (total != 1)
                    {
                        if (string = (PWSTR)ListBox_GetItemData(context->ActiveWindowHandle, sel))
                        {
                            ULONG index = IndexOfStringInList(context->ActiveListArray, string);

                            if (index != ULONG_MAX)
                            {
                                PVOID item = context->ActiveListArray->Items[index];

                                PhRemoveItemsList(context->ActiveListArray, index, 1);
                                PhAddItemList(context->InactiveListArray, item);

                                ListBox_DeleteString(context->ActiveWindowHandle, sel);
                                ListBox_AddItemData(context->InactiveWindowHandle, item);
                                ListBox_SetCurSel(context->ActiveWindowHandle, sel);
                            }

                            SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_INACTIVE, LBN_SELCHANGE), (LPARAM)context->InactiveWindowHandle);
                            SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_ACTIVE, LBN_SELCHANGE), (LPARAM)context->ActiveWindowHandle);
                        }
                    }
                }
                break;
            case IDC_MOVEUP:
                {
                    INT sel;
                    INT count;
                    PWSTR string;

                    sel = ListBox_GetCurSel(context->ActiveWindowHandle);
                    count = ListBox_GetCount(context->ActiveWindowHandle);

                    if (sel != 0)
                    {
                        if (string = (PWSTR)ListBox_GetItemData(context->ActiveWindowHandle, sel))
                        {
                            ULONG index = IndexOfStringInList(context->ActiveListArray, string);

                            if (index != ULONG_MAX)
                            {
                                PVOID item = context->ActiveListArray->Items[index];
                                PhRemoveItemsList(context->ActiveListArray, index, 1);
                                PhInsertItemList(context->ActiveListArray, index - 1, item);

                                ListBox_DeleteString(context->ActiveWindowHandle, sel);
                                sel -= 1;
                                ListBox_InsertItemData(context->ActiveWindowHandle, sel, item);
                                ListBox_SetCurSel(context->ActiveWindowHandle, sel);

                                EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEUP), sel != 0);
                                EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEDOWN), sel != count - 1);
                            }
                        }
                    }
                }
                break;
            case IDC_MOVEDOWN:
                {
                    INT sel;
                    INT count;
                    PPH_STRING string;

                    sel = ListBox_GetCurSel(context->ActiveWindowHandle);
                    count = ListBox_GetCount(context->ActiveWindowHandle);

                    if (sel != count - 1)
                    {
                        PWSTR temp;

                        if (!(temp = (PWSTR)ListBox_GetItemData(context->ActiveWindowHandle, sel)))
                            break;

                        if (string = PhCreateString(temp))
                        {
                            ULONG index = IndexOfStringInList(context->ActiveListArray, PhGetString(string));

                            if (index != ULONG_MAX)
                            {
                                PVOID item = context->ActiveListArray->Items[index];
                                PhRemoveItemsList(context->ActiveListArray, index, 1);
                                PhInsertItemList(context->ActiveListArray, index + 1, item);
                            }

                            ListBox_DeleteString(context->ActiveWindowHandle, sel);
                            sel += 1;
                            ListBox_InsertItemData(context->ActiveWindowHandle, sel, PhGetString(string));
                            ListBox_SetCurSel(context->ActiveWindowHandle, sel);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEUP), sel != 0);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEDOWN), sel != count - 1);
                            PhClearReference(&string);
                        }
                    }
                }
                break;
            }
        }
        break;
     case WM_DRAWITEM:
         {
             LPDRAWITEMSTRUCT drawInfo = (LPDRAWITEMSTRUCT)lParam;

             if (
                 drawInfo->hwndItem == context->InactiveWindowHandle ||
                 drawInfo->hwndItem == context->ActiveWindowHandle
                 )
             {
                 HDC bufferDc;
                 HBITMAP bufferBitmap;
                 HBITMAP oldBufferBitmap;
                 PWSTR string;
                 RECT bufferRect =
                 {
                     0, 0,
                     drawInfo->rcItem.right - drawInfo->rcItem.left,
                     drawInfo->rcItem.bottom - drawInfo->rcItem.top
                 };
                 BOOLEAN isSelected = (drawInfo->itemState & ODS_SELECTED) == ODS_SELECTED;
                 BOOLEAN isFocused = (drawInfo->itemState & ODS_FOCUS) == ODS_FOCUS;

                 if (drawInfo->itemID == LB_ERR)
                     break;

                 if (!(string = (PWSTR)ListBox_GetItemData(drawInfo->hwndItem, drawInfo->itemID)))
                     break;

                 bufferDc = CreateCompatibleDC(drawInfo->hDC);
                 bufferBitmap = CreateCompatibleBitmap(drawInfo->hDC, bufferRect.right, bufferRect.bottom);

                 oldBufferBitmap = SelectBitmap(bufferDc, bufferBitmap);
                 SelectFont(bufferDc, context->ControlFont);
                 SetBkMode(bufferDc, TRANSPARENT);

                 if (isFocused)
                 {
                     FillRect(bufferDc, &bufferRect, context->BrushHot);
                     //FrameRect(bufferDc, &bufferRect, GetStockBrush(BLACK_BRUSH));
                     SetTextColor(bufferDc, context->TextColor);
                 }
                 else
                 {
                     FillRect(bufferDc, &bufferRect, context->BrushNormal);
                     //FrameRect(bufferDc, &bufferRect, GetSysColorBrush(COLOR_HIGHLIGHTTEXT));
                     SetTextColor(bufferDc, context->TextColor);
                 }

                 bufferRect.left += 5;
                 DrawText(
                     bufferDc,
                     string,
                     -1,
                     &bufferRect,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOCLIP
                    );
                 bufferRect.left -= 5;

                 BitBlt(
                     drawInfo->hDC,
                     drawInfo->rcItem.left,
                     drawInfo->rcItem.top,
                     drawInfo->rcItem.right,
                     drawInfo->rcItem.bottom,
                     bufferDc,
                     0,
                     0,
                     SRCCOPY
                    );

                 SelectBitmap(bufferDc, oldBufferBitmap);
                 DeleteBitmap(bufferBitmap);
                 DeleteDC(bufferDc);

                 return TRUE;
             }
         }
         break;
    }

    return FALSE;
}
