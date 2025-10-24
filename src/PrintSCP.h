#pragma once

#include <iostream>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

// ====================
// DCMTK Headers
// ====================
#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmnet/dimse.h>
#include <dcmtk/dcmnet/diutil.h>
#include <dcmtk/dcmnet/assoc.h>
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmimgle/dcmimage.h> // لإدارة الصور الطبية DicomImage

// ====================
// Windows Headers للطباعة
// ====================
#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @class PrintSCP
 * @brief خادم DICOM Print SCP يتعامل مع أوامر N-CREATE / N-ACTION / N-DELETE
 *        ويحول بيانات DICOM إلى صور يمكن طباعتها عبر طابعة ويندوز.
 */
class PrintSCP {
private:
    std::mutex sessionMutex_; ///< قفل لحماية جلسات الطباعة
    std::map<std::string, std::string> printSessions_; ///< تخزين جلسات الطباعة
    T_ASC_Association* currentAssociation_; ///< الاتصال الحالي مع العميل DICOM

public:
    PrintSCP();
    virtual ~PrintSCP();

    /**
     * @brief معالجة جلسة اتصال DICOM واحدة (Association)
     * @param assoc مؤشر إلى جلسة الاتصال
     * @return حالة التنفيذ (DCMTK OFCondition)
     */
    OFCondition handleAssociation(T_ASC_Association* assoc);

protected:
    /**
     * @brief معالجة طلب N-CREATE (إنشاء كائنات طباعة)
     */
    virtual OFCondition handleNCreateRequest(const T_DIMSE_N_CreateRQ& req,
                                             T_ASC_PresentationContextID presID);

    /**
     * @brief معالجة طلب N-ACTION (تنفيذ الطباعة الفعلية)
     */
    virtual OFCondition handleNActionRequest(const T_DIMSE_N_ActionRQ& req,
                                             T_ASC_PresentationContextID presID);

    /**
     * @brief معالجة طلب N-DELETE (حذف جلسة أو فيلم)
     */
    virtual OFCondition handleNDeleteRequest(const T_DIMSE_N_DeleteRQ& req,
                                             T_ASC_PresentationContextID presID);

private:
    /**
     * @brief إنشاء جلسة فيلم جديدة (Film Session)
     */
    OFCondition handleFilmSessionCreate();

    /**
     * @brief إنشاء صندوق فيلم (Film Box)
     */
    OFCondition handleFilmBoxCreate();

    /**
     * @brief إنشاء طابعة (Printer Instance)
     */
    OFCondition handlePrinterCreate();

    /**
     * @brief إرسال رد N-CREATE إلى الجهاز المرسل
     */
    OFCondition sendNCreateResponse(const T_DIMSE_N_CreateRQ& req,
                                    T_ASC_PresentationContextID presID,
                                    Uint16 status);

    /**
     * @brief دالة إرسال الصورة إلى طابعة النظام (Windows Printer)
     * 
     * @param buffer بيانات الصورة (8bit أو 24bit)
     * @param width عرض الصورة
     * @param height ارتفاع الصورة
     * @param printerName اسم الطابعة المستهدفة (افتراضي الطابعة الافتراضية)
     * @param bitsPerPixel عمق البت (8 = رمادي، 24 = ملون)
     */
    bool sendToPrinter(const Uint8* buffer,
                       unsigned long width,
                       unsigned long height,
                       const std::string& printerName = "",
                       int bitsPerPixel = 8);
};
