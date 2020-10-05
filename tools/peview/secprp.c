/*
 * Process Hacker -
 *   PE viewer
 *
 * Copyright (C) 2020 dmex
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

#include <peview.h>
#include <cryptuiapi.h>

typedef enum _PV_IMAGE_CERT_CATEGORY
{
    PV_IMAGE_CERT_CATEGORY_IMAGE,
    PV_IMAGE_CERT_CATEGORY_EMBEDDED,
    PV_IMAGE_CERT_CATEGORY_ARRAY,
    PV_IMAGE_CERT_CATEGORY_MAXIMUM
} PV_IMAGE_CERT_CATEGORY;

typedef struct _PV_PE_CERTIFICATE_CONTEXT
{
    HWND WindowHandle;
    HWND ListViewHandle;
    HIMAGELIST ListViewImageList;
    ULONG Count;
    ULONG ChainCount;
} PV_PE_CERTIFICATE_CONTEXT, * PPV_PE_CERTIFICATE_CONTEXT;

PPH_STRING PvpPeGetRelativeTimeString(
    _In_ PLARGE_INTEGER Time
    )
{
    LARGE_INTEGER time;
    LARGE_INTEGER currentTime;
    SYSTEMTIME timeFields;
    PPH_STRING timeRelativeString;
    PPH_STRING timeString;

    time = *Time;
    PhQuerySystemTime(&currentTime);
    timeRelativeString = PH_AUTO(PhFormatTimeSpanRelative(currentTime.QuadPart - time.QuadPart));

    PhLargeIntegerToLocalSystemTime(&timeFields, &time);
    timeString = PhaFormatDateTime(&timeFields);

    return PhaFormatString(L"%s ago (%s)", timeRelativeString->Buffer, timeString->Buffer);
}

BOOLEAN PvpPeAddCertificateInfo(
    _In_ PPV_PE_CERTIFICATE_CONTEXT Context,
    _In_ INT ListViewItemIndex,
    _In_ BOOLEAN NestedCertificate,
    _In_ PCCERT_CONTEXT CertificateContext
    )
{
    ULONG dataLength;
    LARGE_INTEGER fileTime;
    SYSTEMTIME systemTime;

    if (dataLength = CertGetNameString(CertificateContext, CERT_NAME_FRIENDLY_DISPLAY_TYPE, 0, NULL, NULL, 0))
    {
        PWSTR data = PhAllocateZero(dataLength * sizeof(WCHAR));

        if (CertGetNameString(CertificateContext, CERT_NAME_FRIENDLY_DISPLAY_TYPE, 0, NULL, data, dataLength))
        {
            PhSetListViewSubItem(Context->ListViewHandle, ListViewItemIndex, 1, data);
        }

        PhFree(data);
    }

    if (dataLength = CertGetNameString(CertificateContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, NULL, NULL, 0))
    {
        PWSTR data = PhAllocateZero(dataLength * sizeof(WCHAR));

        if (CertGetNameString(CertificateContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, NULL, data, dataLength))
        {
            PhSetListViewSubItem(Context->ListViewHandle, ListViewItemIndex, 2, data);
        }

        PhFree(data);
    }

    fileTime.QuadPart = 0;
    fileTime.LowPart = CertificateContext->pCertInfo->NotBefore.dwLowDateTime;
    fileTime.HighPart = CertificateContext->pCertInfo->NotBefore.dwHighDateTime;
    PhSetListViewSubItem(Context->ListViewHandle, ListViewItemIndex, 3, PvpPeGetRelativeTimeString(&fileTime)->Buffer);

    fileTime.QuadPart = 0;
    fileTime.LowPart = CertificateContext->pCertInfo->NotAfter.dwLowDateTime;
    fileTime.HighPart = CertificateContext->pCertInfo->NotAfter.dwHighDateTime;
    PhLargeIntegerToLocalSystemTime(&systemTime, &fileTime);
    PhSetListViewSubItem(Context->ListViewHandle, ListViewItemIndex, 4, PhaFormatDateTime(&systemTime)->Buffer);

    dataLength = 0;

    if (CertGetCertificateContextProperty(
        CertificateContext,
        CERT_HASH_PROP_ID,
        NULL,
        &dataLength
        ) && dataLength > 0)
    {
        PBYTE hash = PhAllocateZero(dataLength);

        if (CertGetCertificateContextProperty(CertificateContext, CERT_HASH_PROP_ID, hash, &dataLength))
        {
            PPH_STRING string;

            string = PhBufferToHexString(hash, dataLength);
            PhSetListViewSubItem(Context->ListViewHandle, ListViewItemIndex, 5, string->Buffer);
            PhDereferenceObject(string);
        }

        PhFree(hash);
    }

    if (!NestedCertificate) // TODO: Enable
    {
        CERT_ENHKEY_USAGE enhkeyUsage;
        CERT_USAGE_MATCH certUsage;
        CERT_CHAIN_PARA chainPara;
        PCCERT_CHAIN_CONTEXT chainContext;

        enhkeyUsage.cUsageIdentifier = 0;
        enhkeyUsage.rgpszUsageIdentifier = NULL;
        certUsage.dwType = USAGE_MATCH_TYPE_AND;
        certUsage.Usage = enhkeyUsage;
        chainPara.cbSize = sizeof(CERT_CHAIN_PARA);
        chainPara.RequestedUsage = certUsage;

        if (CertGetCertificateChain(
            NULL,
            CertificateContext,
            NULL,
            NULL,
            &chainPara,
            0,
            NULL,
            &chainContext
            ))
        {
            for (ULONG i = 0; i < chainContext->cChain; i++)
            {
                PCERT_SIMPLE_CHAIN chain = chainContext->rgpChain[i];

                for (ULONG ii = 0; ii < chain->cElement; ii++)
                {
                    PCERT_CHAIN_ELEMENT element = chain->rgpElement[ii];
                    INT lvItemIndex;
                    WCHAR number[PH_INT32_STR_LEN_1];

                    PhPrintUInt32(number, ++Context->ChainCount);
                    lvItemIndex = PhAddListViewGroupItem(
                        Context->ListViewHandle,
                        PV_IMAGE_CERT_CATEGORY_ARRAY,
                        MAXINT,
                        number,
                        (PVOID)element->pCertContext
                        );

                    PvpPeAddCertificateInfo(Context, lvItemIndex, TRUE, element->pCertContext);
                }
            }
        }
    }

    return TRUE;
}

PCMSG_SIGNER_INFO PvpPeGetSignerInfoIndex(
    _In_ HCRYPTMSG CryptMessageHandle,
    _In_ ULONG Index
    )
{
    ULONG signerInfoLength = 0;
    PCMSG_SIGNER_INFO signerInfo;

    if (!CryptMsgGetParam(
        CryptMessageHandle,
        CMSG_SIGNER_INFO_PARAM,
        Index,
        NULL,
        &signerInfoLength
        ))
    {
        return NULL;
    }

    signerInfo = PhAllocateZero(signerInfoLength);

    if (!CryptMsgGetParam(
        CryptMessageHandle,
        CMSG_SIGNER_INFO_PARAM,
        Index,
        signerInfo,
        &signerInfoLength
        ))
    {
        PhFree(signerInfo);
        return NULL;
    }

    return signerInfo;
}

VOID PvpPeEnumerateNestedSignatures(
    _In_ PPV_PE_CERTIFICATE_CONTEXT Context,
    _In_ PCMSG_SIGNER_INFO SignerInfo
    )
{
    HCERTSTORE cryptStoreHandle = NULL;
    HCRYPTMSG cryptMessageHandle = NULL;
    PCCERT_CONTEXT certificateContext = NULL;
    PCMSG_SIGNER_INFO cryptMessageSignerInfo = NULL;
    ULONG certificateEncoding;
    ULONG certificateContentType;
    ULONG certificateFormatType;
    ULONG index = ULONG_MAX;

    for (ULONG i = 0; i < SignerInfo->UnauthAttrs.cAttr; i++)
    {
        if (PhEqualBytesZ(SignerInfo->UnauthAttrs.rgAttr[i].pszObjId, szOID_NESTED_SIGNATURE, FALSE))
        {
            index = i;
            break;
        }
    }

    if (index == ULONG_MAX)
        return;

    if (CryptQueryObject(
        CERT_QUERY_OBJECT_BLOB,
        SignerInfo->UnauthAttrs.rgAttr[index].rgValue,
        CERT_QUERY_CONTENT_FLAG_ALL,
        CERT_QUERY_FORMAT_FLAG_ALL,
        0,
        &certificateEncoding,
        &certificateContentType,
        &certificateFormatType,
        &cryptStoreHandle,
        &cryptMessageHandle,
        NULL
        ))
    {
        ULONG signerCount = 0;
        ULONG signerLength = sizeof(ULONG);

        while (certificateContext = CertEnumCertificatesInStore(cryptStoreHandle, certificateContext))
        {
            INT lvItemIndex;
            WCHAR number[PH_INT32_STR_LEN_1];

            PhPrintUInt32(number, ++Context->Count);
            lvItemIndex = PhAddListViewGroupItem(
                Context->ListViewHandle,
                PV_IMAGE_CERT_CATEGORY_EMBEDDED,
                MAXINT,
                number,
                (PVOID)certificateContext
                );

            PvpPeAddCertificateInfo(Context, lvItemIndex, TRUE, certificateContext);
        }

        if (CryptMsgGetParam(cryptMessageHandle, CMSG_SIGNER_COUNT_PARAM, 0, &signerCount, &signerLength))
        {
            for (ULONG i = 0; i < signerCount; i++)
            {
                if (cryptMessageSignerInfo = PvpPeGetSignerInfoIndex(cryptMessageHandle, i))
                {
                    PvpPeEnumerateNestedSignatures(Context, cryptMessageSignerInfo);

                    PhFree(cryptMessageSignerInfo);
                }
            }
        }

        //if (certificateContext) CertFreeCertificateContext(certificateContext);
        //if (cryptStoreHandle) CertCloseStore(cryptStoreHandle, 0);
        //if (cryptMessageHandle) CryptMsgClose(cryptMessageHandle);
    }
}

VOID PvpPeEnumerateFileCertificates(
    _In_ PPV_PE_CERTIFICATE_CONTEXT Context
    )
{
    PIMAGE_DATA_DIRECTORY dataDirectory;
    HCERTSTORE cryptStoreHandle = NULL;
    PCCERT_CONTEXT certificateContext = NULL;
    HCRYPTMSG cryptMessageHandle = NULL;
    PCMSG_SIGNER_INFO cryptMessageSignerInfo = NULL;
    ULONG certificateEncoding;
    ULONG certificateContentType;
    ULONG certificateFormatType;

    if (NT_SUCCESS(PhGetMappedImageDataEntry(&PvMappedImage, IMAGE_DIRECTORY_ENTRY_SECURITY, &dataDirectory)))
    {
        LPWIN_CERTIFICATE certificateDirectory = PTR_ADD_OFFSET(PvMappedImage.ViewBase, dataDirectory->VirtualAddress);
        CERT_BLOB certificateBlob = { certificateDirectory->dwLength, certificateDirectory->bCertificate };

        CryptQueryObject(
            CERT_QUERY_OBJECT_BLOB,
            &certificateBlob,
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0,
            &certificateEncoding,
            &certificateContentType,
            &certificateFormatType,
            &cryptStoreHandle,
            &cryptMessageHandle,
            NULL
            );
    }

    if (!(cryptStoreHandle && cryptMessageHandle))
    {
        CryptQueryObject(
            CERT_QUERY_OBJECT_FILE,
            PhGetString(PvFileName),
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0,
            &certificateEncoding,
            &certificateContentType,
            &certificateFormatType,
            &cryptStoreHandle,
            &cryptMessageHandle,
            NULL
            );
    }

    if (cryptStoreHandle)
    {
        while (certificateContext = CertEnumCertificatesInStore(cryptStoreHandle, certificateContext))
        {
            INT lvItemIndex;
            WCHAR number[PH_INT32_STR_LEN_1];

            PhPrintUInt32(number, ++Context->Count);
            lvItemIndex = PhAddListViewGroupItem(
                Context->ListViewHandle,
                PV_IMAGE_CERT_CATEGORY_IMAGE,
                MAXINT,
                number,
                (PVOID)certificateContext
                );

            PvpPeAddCertificateInfo(Context, lvItemIndex, FALSE, certificateContext);
        }

        //if (certificateContext) CertFreeCertificateContext(certificateContext);
        //if (cryptStoreHandle) CertCloseStore(cryptStoreHandle, 0);
    }

    if (cryptMessageHandle)
    {
        ULONG signerCount = 0;
        ULONG signerLength = sizeof(ULONG);

        if (CryptMsgGetParam(cryptMessageHandle, CMSG_SIGNER_COUNT_PARAM, 0, &signerCount, &signerLength))
        {
            for (ULONG i = 0; i < signerCount; i++)
            {
                if (cryptMessageSignerInfo = PvpPeGetSignerInfoIndex(cryptMessageHandle, i))
                {
                    PvpPeEnumerateNestedSignatures(Context, cryptMessageSignerInfo);

                    PhFree(cryptMessageSignerInfo);
                }
            }
        }

        //if (cryptMessageHandle) CryptMsgClose(cryptMessageHandle);
    }
}

VOID PvpPeViewCertificateContext(
    _In_ HWND WindowHandle,
    _In_ PCCERT_CONTEXT CertContext
    )
{
    CRYPTUI_VIEWCERTIFICATE_STRUCT cryptViewCertInfo;
    HCERTSTORE certStore = CertContext->hCertStore;

    memset(&cryptViewCertInfo, 0, sizeof(CRYPTUI_VIEWCERTIFICATE_STRUCT));
    cryptViewCertInfo.dwSize = sizeof(CRYPTUI_VIEWCERTIFICATE_STRUCT);
    //cryptViewCertInfo.dwFlags = CRYPTUI_ENABLE_REVOCATION_CHECKING | CRYPTUI_ENABLE_REVOCATION_CHECK_END_CERT;
    cryptViewCertInfo.hwndParent = WindowHandle;
    cryptViewCertInfo.pCertContext = CertContext;
    cryptViewCertInfo.cStores = 1;
    cryptViewCertInfo.rghStores = &certStore;

    if (CryptUIDlgViewCertificate)
    {
#pragma warning(push)
#pragma warning(disable:6387)
        CryptUIDlgViewCertificate(&cryptViewCertInfo, NULL);
#pragma warning(pop)
    }

    //if (CryptUIDlgViewContext)
    //{
    //    CryptUIDlgViewContext(CERT_STORE_CERTIFICATE_CONTEXT, CertContext, WindowHandle, NULL, 0, NULL);
    //}
}

VOID PvpPeSaveCertificateContext(
    _In_ HWND WindowHandle,
    _In_ PCCERT_CONTEXT CertContext
    )
{
    CRYPTUI_WIZ_EXPORT_INFO cryptExportCertInfo;
    HCERTSTORE certStore = CertContext->hCertStore;

    memset(&cryptExportCertInfo, 0, sizeof(CRYPTUI_WIZ_EXPORT_INFO));
    cryptExportCertInfo.dwSize = sizeof(CRYPTUI_WIZ_EXPORT_INFO);
    cryptExportCertInfo.dwSubjectChoice = CRYPTUI_WIZ_EXPORT_CERT_CONTEXT;
    cryptExportCertInfo.pCertContext = CertContext;
    cryptExportCertInfo.cStores = 1;
    cryptExportCertInfo.rghStores = &certStore;

    if (CryptUIWizExport)
    {
        CryptUIWizExport(0, WindowHandle, NULL, &cryptExportCertInfo, NULL);
    }
}

INT_PTR CALLBACK PvpPeSecurityDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPV_PROPPAGECONTEXT propPageContext;
    PPV_PE_CERTIFICATE_CONTEXT context;

    if (!PvPropPageDlgProcHeader(hwndDlg, uMsg, lParam, &propSheetPage, &propPageContext))
        return FALSE;

    if (uMsg == WM_INITDIALOG)
    {
        context = propPageContext->Context = PhAllocate(sizeof(PV_PE_CERTIFICATE_CONTEXT));
        memset(context, 0, sizeof(PV_PE_CERTIFICATE_CONTEXT));
    }
    else
    {
        context = propPageContext->Context;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context->WindowHandle = hwndDlg;
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_LIST);
            
            PhSetListViewStyle(context->ListViewHandle, TRUE, TRUE);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 40, L"#");
            PhAddListViewColumn(context->ListViewHandle, 1, 1, 1, LVCFMT_LEFT, 100, L"Issued to");
            PhAddListViewColumn(context->ListViewHandle, 2, 2, 2, LVCFMT_LEFT, 100, L"Issued by");
            PhAddListViewColumn(context->ListViewHandle, 3, 3, 3, LVCFMT_LEFT, 100, L"From");
            PhAddListViewColumn(context->ListViewHandle, 4, 4, 4, LVCFMT_LEFT, 100, L"To");
            PhAddListViewColumn(context->ListViewHandle, 5, 5, 5, LVCFMT_LEFT, 100, L"Thumbprint");
            PhSetExtendedListView(context->ListViewHandle);
            PhLoadListViewColumnsFromSetting(L"ImageSecurityListViewColumns", context->ListViewHandle);
            PhLoadListViewSortColumnsFromSetting(L"ImageSecurityListViewSort", context->ListViewHandle);

            if (context->ListViewImageList = ImageList_Create(2, 20, ILC_COLOR, 1, 1))
                ListView_SetImageList(context->ListViewHandle, context->ListViewImageList, LVSIL_SMALL);

            ListView_EnableGroupView(context->ListViewHandle, TRUE);
            PhAddListViewGroup(context->ListViewHandle, PV_IMAGE_CERT_CATEGORY_IMAGE, L"Image certificates");
            PhAddListViewGroup(context->ListViewHandle, PV_IMAGE_CERT_CATEGORY_EMBEDDED, L"Nested certificates");
            PhAddListViewGroup(context->ListViewHandle, PV_IMAGE_CERT_CATEGORY_ARRAY, L"Chained certificates");

            PvpPeEnumerateFileCertificates(context);

            PhInitializeWindowTheme(hwndDlg, PeEnableThemeSupport);
        }
        break;
    case WM_DESTROY:
        {
            PhSaveListViewSortColumnsToSetting(L"ImageSecurityListViewSort", context->ListViewHandle);
            PhSaveListViewColumnsToSetting(L"ImageSecurityListViewColumns", context->ListViewHandle);

            if (context->ListViewImageList)
                ImageList_Destroy(context->ListViewImageList);

            PhFree(context);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PvAddPropPageLayoutItem(hwndDlg, hwndDlg, PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PvAddPropPageLayoutItem(hwndDlg, context->ListViewHandle, dialogItem, PH_ANCHOR_ALL);

                PvDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            PvHandleListViewNotifyForCopy(lParam, context->ListViewHandle);

            switch (header->code)
            {
            case NM_DBLCLK:
                {
                    if (header->hwndFrom == context->ListViewHandle)
                    {
                        PVOID* listviewItems;
                        ULONG numberOfItems;

                        PhGetSelectedListViewItemParams(context->ListViewHandle, &listviewItems, &numberOfItems);

                        if (numberOfItems != 0)
                        {
                            PvpPeViewCertificateContext(hwndDlg, listviewItems[0]);
                        }

                        PhFree(listviewItems);
                    }
                }
                break;
            }
        }
        break;
    case WM_CONTEXTMENU:
        {
            if ((HWND)wParam == context->ListViewHandle)
            {
                POINT point;
                PPH_EMENU menu;
                PPH_EMENU item;
                PVOID* listviewItems;
                ULONG numberOfItems;

                point.x = GET_X_LPARAM(lParam);
                point.y = GET_Y_LPARAM(lParam);

                if (point.x == -1 && point.y == -1)
                    PvGetListViewContextMenuPoint((HWND)wParam, &point);

                PhGetSelectedListViewItemParams((HWND)wParam, &listviewItems, &numberOfItems);

                if (numberOfItems != 0)
                {
                    menu = PhCreateEMenu();
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 1, L"View certificate...", NULL, NULL), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 2, L"Save certificate...", NULL, NULL), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, USHRT_MAX, L"&Copy", NULL, NULL), ULONG_MAX);
                    PvInsertCopyListViewEMenuItem(menu, USHRT_MAX, (HWND)wParam);

                    item = PhShowEMenu(
                        menu,
                        hwndDlg,
                        PH_EMENU_SHOW_SEND_COMMAND | PH_EMENU_SHOW_LEFTRIGHT,
                        PH_ALIGN_LEFT | PH_ALIGN_TOP,
                        point.x,
                        point.y
                        );

                    if (item)
                    {
                        if (!PvHandleCopyListViewEMenuItem(item))
                        {
                            switch (item->Id)
                            {
                            case 1:
                                PvpPeViewCertificateContext(hwndDlg, listviewItems[0]);                   
                                break;
                            case 2:
                                PvpPeSaveCertificateContext(hwndDlg, listviewItems[0]);
                                break;
                            case USHRT_MAX:
                                PvCopyListView((HWND)wParam);
                                break;
                            }
                        }
                    }

                    PhDestroyEMenu(menu);
                }

                PhFree(listviewItems);
            }
        }
        break;
    }

    return FALSE;
}
