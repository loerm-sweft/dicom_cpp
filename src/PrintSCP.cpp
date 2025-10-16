#include "PrintSCP.h"
#include <chrono>
#include <thread>
#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <windows.h>
#include <vector>
#include <iostream>
#include <mutex>

// -----------------------------
// دالة مساعدة لإرسال الصورة للطابعة
// -----------------------------
bool sendToPrinter(const Uint8* buffer, unsigned long width, unsigned long height, const std::string& printerName = "") {
    HANDLE hPrinter;
    if (!OpenPrinterA(printerName.empty() ? NULL : printerName.c_str(), &hPrinter, NULL)) {
        std::cerr << "❌ فشل في فتح الطابعة" << std::endl;
        return false;
    }

    DOC_INFO_1A docInfo;
    docInfo.pDocName = (LPSTR)"DICOM Print";
    docInfo.pOutputFile = NULL;
    docInfo.pDatatype = (LPSTR)"RAW";

    if (StartDocPrinterA(hPrinter, 1, (LPBYTE)&docInfo) == 0) {
        std::cerr << "❌ فشل في بدء مستند الطباعة" << std::endl;
        ClosePrinter(hPrinter);
        return false;
    }

    if (!StartPagePrinter(hPrinter)) {
        std::cerr << "❌ فشل في بدء الصفحة" << std::endl;
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);
        return false;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -(LONG)height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 8;
    bmi.bmiHeader.biCompression = BI_RGB;
    for (int i = 0; i < 256; ++i) {
        bmi.bmiColors[i].rgbBlue = i;
        bmi.bmiColors[i].rgbGreen = i;
        bmi.bmiColors[i].rgbRed = i;
        bmi.bmiColors[i].rgbReserved = 0;
    }

    HDC hDC = CreateDCA("WINSPOOL", printerName.empty() ? NULL : printerName.c_str(), NULL, NULL);
    if (!hDC) {
        std::cerr << "❌ فشل في إنشاء DC للطابعة" << std::endl;
        EndPagePrinter(hPrinter);
        EndDocPrinter(hPrinter);
        ClosePrinter(hPrinter);
        return false;
    }

    StretchDIBits(hDC, 0, 0, width, height, 0, 0, width, height, buffer, &bmi, DIB_RGB_COLORS, SRCCOPY);

    DeleteDC(hDC);
    EndPagePrinter(hPrinter);
    EndDocPrinter(hPrinter);
    ClosePrinter(hPrinter);

    std::cout << "✅ تم إرسال الصورة للطباعة بنجاح" << std::endl;
    return true;
}

// -----------------------------
// PrintSCP Implementation
// -----------------------------
PrintSCP::PrintSCP() : currentAssociation_(nullptr) {
    std::cout << "🔄 تهيئة Print SCP..." << std::endl;
}

PrintSCP::~PrintSCP() {
    std::cout << "🧹 تنظيف Print SCP..." << std::endl;
}

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

    // تحويل الصورة للطباعة
    DicomImage dcmImage(dataset, EXS_Unknown);
    if (dcmImage.getStatus() != EIS_Normal) {
        std::cerr << "❌ خطأ في قراءة DICOM Image" << std::endl;
        delete dataset;
        return sendNCreateResponse(req, presID, STATUS_CannotUnderstand);
    }

    dcmImage.setMinMaxWindow();
    const unsigned long width = dcmImage.getWidth();
    const unsigned long height = dcmImage.getHeight();
    std::vector<Uint8> buffer(width * height);

    if (dcmImage.isMonochrome()) {
        for (unsigned long y = 0; y < height; ++y)
            for (unsigned long x = 0; x < width; ++x)
                buffer[y * width + x] = static_cast<Uint8>(dcmImage.getPixel(x, y));

        if (dcmImage.getPhotometricInterpretation() == EPI_Monochrome1)
            for (auto& val : buffer) val = 255 - val;
    }

    sendToPrinter(buffer.data(), width, height);

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

    DicomImage dcmImage(dataset, EXS_Unknown);
    if (dcmImage.getStatus() != EIS_Normal) {
        std::cerr << "❌ خطأ في قراءة DICOM Image" << std::endl;
        delete dataset;
        return EC_CorruptedData;
    }

    dcmImage.setMinMaxWindow();
    const unsigned long width = dcmImage.getWidth();
    const unsigned long height = dcmImage.getHeight();
    std::vector<Uint8> buffer(width * height);

    if (dcmImage.isMonochrome()) {
        for (unsigned long y = 0; y < height; ++y)
            for (unsigned long x = 0; x < width; ++x)
                buffer[y * width + x] = static_cast<Uint8>(dcmImage.getPixel(x, y));

        if (dcmImage.getPhotometricInterpretation() == EPI_Monochrome1)
            for (auto& val : buffer) val = 255 - val;
    }

    sendToPrinter(buffer.data(), width, height);
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

    // Dataset اختياري للرد
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

