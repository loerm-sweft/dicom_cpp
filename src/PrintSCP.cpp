// PrintSCP.cpp
#include "PrintSCP.h"
#include <chrono>
#include <thread>
#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <windows.h>
#include <vector>
#include <iostream>
#include <mutex>
#include <cstring>

// -----------------------------
// PrintSCP Implementation
// -----------------------------
PrintSCP::PrintSCP() : currentAssociation_(nullptr) {
    std::cout << "🔄 تهيئة Print SCP..." << std::endl;
}

PrintSCP::~PrintSCP() {
    std::cout << "🧹 تنظيف Print SCP..." << std::endl;
}

/**
 * @brief Helper: send image buffer to Windows printer. Supports 8-bit grayscale and 24-bit RGB.
 * bitsPerPixel: 8 or 24
 */
bool PrintSCP::sendToPrinter(const Uint8* buffer,
                             unsigned long width,
                             unsigned long height,
                             const std::string& printerName,
                             int bitsPerPixel) {
    // Open target printer (NULL = default)
    HANDLE hPrinter = NULL;
    if (!OpenPrinterA(printerName.empty() ? NULL : printerName.c_str(), &hPrinter, NULL)) {
        std::cerr << "❌ Failed to open printer: " << printerName << " (using default?)" << std::endl;
        return false;
    }

    DOC_INFO_1A docInfo;
    ZeroMemory(&docInfo, sizeof(docInfo));
    docInfo.pDocName = (LPSTR)"DICOM Print";
    docInfo.pOutputFile = NULL;
    docInfo.pDatatype = (LPSTR)"RAW";

    if (StartDocPrinterA(hPrinter, 1, (LPBYTE)&docInfo) == 0) {
        std::cerr << "❌ Failed to start print doc" << std::endl;
        ClosePrinter(hPrinter);
        return false;
    }

    if (!StartPagePrinter(hPrinter)) {
        std::cerr << "❌ Failed to start page" << std::endl;
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);
        return false;
    }

    // Create device context for the printer
    HDC hDC = CreateDCA("WINSPOOL", printerName.empty() ? NULL : printerName.c_str(), NULL, NULL);
    if (!hDC) {
        std::cerr << "❌ Failed to create printer DC" << std::endl;
        EndPagePrinter(hPrinter);
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);
        return false;
    }

    BOOL result = FALSE;

    if (bitsPerPixel == 8) {
        // 8-bit grayscale: need BITMAPINFO with palette (256 entries)
        size_t bmiSize = sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD);
        BITMAPINFO* pbmi = (BITMAPINFO*)malloc(bmiSize);
        if (!pbmi) {
            std::cerr << "❌ Memory allocation failed for BITMAPINFO (8-bit)" << std::endl;
            DeleteDC(hDC);
            EndPagePrinter(hPrinter);
            EndDocPrinter(hPrinter);
            ClosePrinter(hPrinter);
            return false;
        }
        ZeroMemory(pbmi, bmiSize);
        pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        pbmi->bmiHeader.biWidth = (LONG)width;
        pbmi->bmiHeader.biHeight = -(LONG)height; // top-down
        pbmi->bmiHeader.biPlanes = 1;
        pbmi->bmiHeader.biBitCount = 8;
        pbmi->bmiHeader.biCompression = BI_RGB;
        pbmi->bmiHeader.biSizeImage = 0;

        for (int i = 0; i < 256; ++i) {
            pbmi->bmiColors[i].rgbBlue = (BYTE)i;
            pbmi->bmiColors[i].rgbGreen = (BYTE)i;
            pbmi->bmiColors[i].rgbRed = (BYTE)i;
            pbmi->bmiColors[i].rgbReserved = 0;
        }

        // StretchDIBits - it accepts 8-bit buffer with palette
        int ret = StretchDIBits(hDC,
                                0, 0, (int)width, (int)height,
                                0, 0, (int)width, (int)height,
                                buffer,
                                pbmi,
                                DIB_RGB_COLORS,
                                SRCCOPY);
        if (ret == GDI_ERROR) {
            std::cerr << "❌ StretchDIBits failed for 8-bit image" << std::endl;
            result = FALSE;
        } else {
            result = TRUE;
        }
        free(pbmi);
    } else if (bitsPerPixel == 24) {
        // 24-bit RGB: Windows expects BGR and each scanline padded to 4 bytes
        const int bytesPerPixel = 3;
        int srcStride = (int)(width * bytesPerPixel);
        int dstStride = ((srcStride + 3) / 4) * 4; // padded to 4 bytes
        size_t paddedSize = (size_t)dstStride * height;

        std::vector<BYTE> paddedBuffer(paddedSize);
        // Fill paddedBuffer row by row (Windows uses bottom-up unless height negative; we used top-down by negative height in header, so keep order top-down)
        for (unsigned long y = 0; y < height; ++y) {
            const Uint8* srcRow = buffer + (y * srcStride);
            BYTE* dstRow = paddedBuffer.data() + (y * dstStride);
            // Copy and swap R<->B to make BGR
            for (unsigned long x = 0; x < width; ++x) {
                // src: R G B  -> dst: B G R
                dstRow[x * 3 + 0] = srcRow[x * 3 + 2];
                dstRow[x * 3 + 1] = srcRow[x * 3 + 1];
                dstRow[x * 3 + 2] = srcRow[x * 3 + 0];
            }
            // remaining padding bytes are already zero-initialized by vector
        }

        BITMAPINFOHEADER bih;
        ZeroMemory(&bih, sizeof(bih));
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = (LONG)width;
        bih.biHeight = -(LONG)height; // top-down
        bih.biPlanes = 1;
        bih.biBitCount = 24;
        bih.biCompression = BI_RGB;
        bih.biSizeImage = (DWORD)paddedSize;

        // StretchDIBits expects a BITMAPINFO pointer; we can pass pointer to header
        int ret = StretchDIBits(hDC,
                                0, 0, (int)width, (int)height,
                                0, 0, (int)width, (int)height,
                                paddedBuffer.data(),
                                (BITMAPINFO*)&bih,
                                DIB_RGB_COLORS,
                                SRCCOPY);
        if (ret == GDI_ERROR) {
            std::cerr << "❌ StretchDIBits failed for 24-bit image" << std::endl;
            result = FALSE;
        } else {
            result = TRUE;
        }
    } else {
        std::cerr << "❌ Unsupported bitsPerPixel: " << bitsPerPixel << std::endl;
        result = FALSE;
    }

    // Cleanup GDI and printer
    DeleteDC(hDC);
    EndPagePrinter(hPrinter);
    EndDocPrinter(hPrinter);
    ClosePrinter(hPrinter);

    if (result)
        std::cout << "✅ Image successfully sent to printer" << std::endl;
    return result != FALSE;
}

// -----------------------------
// handleAssociation
// -----------------------------
OFCondition PrintSCP::handleAssociation(T_ASC_Association* assoc) {
    currentAssociation_ = assoc;
    OFCondition cond = EC_Normal;
    T_DIMSE_Message msg;
    T_ASC_PresentationContextID presID;

    while (cond.good()) {
        cond = DIMSE_receiveCommand(assoc, DIMSE_NONBLOCKING, 30, &presID, &msg, NULL);

        if (cond.good()) {
            switch (msg.CommandField) {
                case DIMSE_N_CREATE_RQ:
                    std::cout << "🖨 استلام طلب N-CREATE" << std::endl;
                    cond = handleNCreateRequest(msg.msg.NCreateRQ, presID);
                    break;
                case DIMSE_N_ACTION_RQ:
                    std::cout << "⚡ استلام طلب N-ACTION" << std::endl;
                    cond = handleNActionRequest(msg.msg.NActionRQ, presID);
                    break;
                case DIMSE_N_DELETE_RQ:
                    std::cout << "🗑 استلام طلب N-DELETE" << std::endl;
                    cond = handleNDeleteRequest(msg.msg.NDeleteRQ, presID);
                    break;
                default:
                    std::cout << "❌ أمر DIMSE غير معروف: " << msg.CommandField << std::endl;
                    break;
            }
        } else if (cond == DIMSE_NODATAAVAILABLE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cond = EC_Normal;
        }
    }

    return cond;
}

// -----------------------------
// N-CREATE
// -----------------------------
OFCondition PrintSCP::handleNCreateRequest(const T_DIMSE_N_CreateRQ& req,
                                           T_ASC_PresentationContextID presID) {
    std::cout << "📋 SOP Class: " << req.AffectedSOPClassUID << std::endl;
    std::cout << "🔑 SOP Instance: " << req.AffectedSOPInstanceUID << std::endl;

    // استلام الـ Dataset المرفق
    DcmDataset* dataset = nullptr;
    OFCondition cond = DIMSE_receiveDataSetInMemory(currentAssociation_, presID, &dataset, NULL, NULL);
    if (cond.bad() || !dataset) {
        std::cerr << "❌ لم يتم استلام Dataset" << std::endl;
        return sendNCreateResponse(req, presID, STATUS_ProcessingFailure);
    }

    // التشخيص: Transfer Syntax ووجود PixelData
    OFString txUID;
    dataset->findAndGetOFString(DCM_TransferSyntaxUID, txUID);
    std::cout << "Transfer Syntax UID (dataset): " << txUID << std::endl;

    DcmElement* pixElem = nullptr;
    if (dataset->findAndGetElement(DCM_PixelData, pixElem) == EC_Normal) {
        std::cout << "PixelData: present" << std::endl;
    } else {
        std::cout << "PixelData: NOT found" << std::endl;
    }

    // Use DicomImage to convert to displayable 8-bit (or 24-bit for color)
    DicomImage dcmImage(dataset, EXS_Unknown);
    if (dcmImage.getStatus() != EIS_Normal) {
        std::cerr << "❌ Error reading DICOM Image (status=" << dcmImage.getStatus() << ")\n";
        delete dataset;
        return sendNCreateResponse(req, presID, STATUS_CannotUnderstand);
    }

    dcmImage.setMinMaxWindow(); // apply automatic windowing

    const unsigned long width = dcmImage.getWidth();
    const unsigned long height = dcmImage.getHeight();

    const void* outData = dcmImage.getOutputData(8); // request 8-bit output; color images will be 3 bytes per pixel
    if (!outData) {
        std::cerr << "❌ getOutputData returned NULL" << std::endl;
        delete dataset;
        return sendNCreateResponse(req, presID, STATUS_ProcessingFailure);
    }

    // If monochrome, outData length = width*height (1 byte per pixel)
    // If color, outData length = width*height*3 (RGB)
    if (dcmImage.isMonochrome()) {
        std::vector<Uint8> buffer(width * height);
        memcpy(buffer.data(), outData, width * height);

        // MONOCHROME1 needs inversion
        if (dcmImage.getPhotometricInterpretation() == EPI_Monochrome1) {
            for (size_t i = 0; i < buffer.size(); ++i) buffer[i] = 255 - buffer[i];
        }

        // Send to printer as 8-bit grayscale
        if (!sendToPrinter(buffer.data(), width, height, std::string(), 8)) {
            std::cerr << "❌ sendToPrinter failed for monochrome image" << std::endl;
            delete dataset;
            return sendNCreateResponse(req, presID, STATUS_ProcessingFailure);
        }
    } else {
        // Color image - assume RGB interleaved (R G B)
        const size_t rgbSize = (size_t)width * (size_t)height * 3;
        std::vector<Uint8> rgbBuffer(rgbSize);
        memcpy(rgbBuffer.data(), outData, rgbSize);

        // sendToPrinter expects BGR; the function will swap bytes
        if (!sendToPrinter(rgbBuffer.data(), width, height, std::string(), 24)) {
            std::cerr << "❌ sendToPrinter failed for color image" << std::endl;
            delete dataset;
            return sendNCreateResponse(req, presID, STATUS_ProcessingFailure);
        }
    }

    delete dataset;
    return sendNCreateResponse(req, presID, STATUS_Success);
}

// -----------------------------
// N-ACTION
// -----------------------------
OFCondition PrintSCP::handleNActionRequest(const T_DIMSE_N_ActionRQ& req,
                                           T_ASC_PresentationContextID presID) {
    std::cout << "⚡ معالجة N-ACTION: " << req.ActionTypeID << std::endl;

    // استلام Dataset للطباعة
    DcmDataset* dataset = nullptr;
    OFCondition cond = DIMSE_receiveDataSetInMemory(currentAssociation_, presID, &dataset, NULL, NULL);
    if (cond.bad() || !dataset) {
        std::cerr << "❌ لم يتم استلام Dataset" << std::endl;
        return EC_IllegalParameter;
    }

    // معالجة مشابهة لـ N-CREATE: تحويل الصورة وإرسالها
    DicomImage dcmImage(dataset, EXS_Unknown);
    if (dcmImage.getStatus() != EIS_Normal) {
        std::cerr << "❌ خطأ في قراءة DICOM Image" << std::endl;
        delete dataset;
        return EC_CorruptedData;
    }

    dcmImage.setMinMaxWindow();
    const unsigned long width = dcmImage.getWidth();
    const unsigned long height = dcmImage.getHeight();

    const void* outData = dcmImage.getOutputData(8);
    if (!outData) {
        std::cerr << "❌ getOutputData returned NULL in N-ACTION" << std::endl;
        delete dataset;
        return EC_CorruptedData;
    }

    if (dcmImage.isMonochrome()) {
        std::vector<Uint8> buffer(width * height);
        memcpy(buffer.data(), outData, width * height);
        if (dcmImage.getPhotometricInterpretation() == EPI_Monochrome1)
            for (size_t i = 0; i < buffer.size(); ++i) buffer[i] = 255 - buffer[i];

        if (!sendToPrinter(buffer.data(), width, height, std::string(), 8)) {
            std::cerr << "❌ sendToPrinter failed (N-ACTION, mono)" << std::endl;
            delete dataset;
            return EC_CorruptedData;
        }
    } else {
        const size_t rgbSize = (size_t)width * (size_t)height * 3;
        std::vector<Uint8> rgbBuffer(rgbSize);
        memcpy(rgbBuffer.data(), outData, rgbSize);

        if (!sendToPrinter(rgbBuffer.data(), width, height, std::string(), 24)) {
            std::cerr << "❌ sendToPrinter failed (N-ACTION, color)" << std::endl;
            delete dataset;
            return EC_CorruptedData;
        }
    }

    delete dataset;

    // إرسال الرد
    T_DIMSE_Message rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.CommandField = DIMSE_N_ACTION_RSP;
    rsp.msg.NActionRSP.MessageIDBeingRespondedTo = req.MessageID;
    rsp.msg.NActionRSP.ActionTypeID = req.ActionTypeID;
    rsp.msg.NActionRSP.DimseStatus = STATUS_Success;
    rsp.msg.NActionRSP.DataSetType = DIMSE_DATASET_NULL;

    return DIMSE_sendMessageUsingMemoryData(currentAssociation_, presID, &rsp, NULL, NULL, NULL, NULL);
}

// -----------------------------
// N-DELETE
// -----------------------------
OFCondition PrintSCP::handleNDeleteRequest(const T_DIMSE_N_DeleteRQ& req,
                                           T_ASC_PresentationContextID presID) {
    std::cout << "🗑 معالجة N-DELETE" << std::endl;
    T_DIMSE_Message rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.CommandField = DIMSE_N_DELETE_RSP;
    rsp.msg.NDeleteRSP.MessageIDBeingRespondedTo = req.MessageID;
    rsp.msg.NDeleteRSP.DimseStatus = STATUS_Success;
    rsp.msg.NDeleteRSP.DataSetType = DIMSE_DATASET_NULL;
    return DIMSE_sendMessageUsingMemoryData(currentAssociation_, presID, &rsp, NULL, NULL, NULL, NULL);
}

// -----------------------------
// إرسال N-CREATE Response
// -----------------------------
OFCondition PrintSCP::sendNCreateResponse(const T_DIMSE_N_CreateRQ& req,
                                          T_ASC_PresentationContextID presID,
                                          Uint16 status) {
    if (!currentAssociation_) return EC_IllegalCall;

    T_DIMSE_Message response;
    memset(&response, 0, sizeof(response));
    response.CommandField = DIMSE_N_CREATE_RSP;
    response.msg.NCreateRSP.MessageIDBeingRespondedTo = req.MessageID;
    response.msg.NCreateRSP.DimseStatus = status;
    response.msg.NCreateRSP.DataSetType = DIMSE_DATASET_NULL;

    // Optional dataset in response
    DcmDataset* rspDataset = nullptr;
    if (status == STATUS_Success) {
        rspDataset = new DcmDataset();
        rspDataset->putAndInsertString(DCM_NumberOfCopies, "1");
        rspDataset->putAndInsertString(DCM_PrintPriority, "MED");
        rspDataset->putAndInsertString(DCM_MediumType, "PAPER");
    }

    OFCondition sendCond = DIMSE_sendMessageUsingMemoryData(
        currentAssociation_, presID, &response, NULL, rspDataset, NULL, NULL);

    delete rspDataset;

    if (sendCond.good())
        std::cout << "✅ تم إرسال رد N-CREATE بنجاح" << std::endl;
    else
        std::cerr << "❌ فشل في إرسال رد N-CREATE: " << sendCond.text() << std::endl;

    return sendCond;
}

// -----------------------------
// إنشاء جلسة فيلم
// -----------------------------
OFCondition PrintSCP::handleFilmSessionCreate() {
    std::cout << "🎞 معالجة إنشاء جلسة فيلم" << std::endl;
    std::lock_guard<std::mutex> lock(sessionMutex_);
    printSessions_["default_session"] = "ACTIVE";
    std::cout << "✅ إنشاء جلسة طباعة جديدة" << std::endl;
    return EC_Normal;
}

// -----------------------------
// إنشاء صندوق فيلم
// -----------------------------
OFCondition PrintSCP::handleFilmBoxCreate() {
    std::cout << "📦 معالجة إنشاء صندوق فيلم" << std::endl;
    return EC_Normal;
}

// -----------------------------
// إنشاء طابعة
// -----------------------------
OFCondition PrintSCP::handlePrinterCreate() {
    std::cout << "🖨 معالجة إنشاء طابعة" << std::endl;
    return EC_Normal;
}
