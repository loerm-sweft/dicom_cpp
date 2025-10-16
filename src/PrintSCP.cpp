#include "PrintSCP.h"
#include <chrono>
#include <thread>

PrintSCP::PrintSCP() : currentAssociation_(NULL) {
    std::cout << "🔄 تهيئة Print SCP..." << std::endl;
}

PrintSCP::~PrintSCP() {
    std::cout << "🧹 تنظيف Print SCP..." << std::endl;
}

OFCondition PrintSCP::handleAssociation(T_ASC_Association* assoc) {
    currentAssociation_ = assoc;
    OFCondition cond = EC_Normal;
    
    // استقبال ومعالجة رسائل DIMSE
    T_DIMSE_Message msg;
    T_ASC_PresentationContextID presID;
    
    while (cond.good()) {
        // استقبال الرسالة التالية
        cond = DIMSE_receiveCommand(assoc, DIMSE_NONBLOCKING, 30, &presID, &msg, NULL);
        
        if (cond.good()) {
            switch (msg.CommandField) {
                case DIMSE_N_CREATE_RQ:
                    std::cout << "🖨️ استلام طلب N-CREATE" << std::endl;
                    cond = handleNCreateRequest(msg.msg.NCreateRQ, presID);
                    break;
                    
                case DIMSE_N_ACTION_RQ:
                    std::cout << "⚡ استلام طلب N-ACTION" << std::endl;
                    cond = handleNActionRequest(msg.msg.NActionRQ, presID);
                    break;
                    
                case DIMSE_N_DELETE_RQ:
                    std::cout << "🗑️ استلام طلب N-DELETE" << std::endl;
                    cond = handleNDeleteRequest(msg.msg.NDeleteRQ, presID);
                    break;
                    
                default:
                    std::cout << "❌ أمر DIMSE غير معروف: " << msg.CommandField << std::endl;
                    // تجاهل الأوامر غير المعروفة بدلاً من إرجاع خطأ
                    break;
            }
        } else if (cond == DIMSE_NODATAAVAILABLE) {
            // لا توجد بيانات متاحة - هذه حالة طبيعية في الوضع NONBLOCKING
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cond = EC_Normal; // إعادة التعيين للمتابعة
        }
    }
    
    return cond;
}

OFCondition PrintSCP::handleNCreateRequest(const T_DIMSE_N_CreateRQ& req,
                                          T_ASC_PresentationContextID presID) {
    std::cout << "📋 SOP Class: " << req.AffectedSOPClassUID << std::endl;
    std::cout << "🔑 SOP Instance: " << req.AffectedSOPInstanceUID << std::endl;
    
    // تحديد نوع SOP Class ومعالجته
    OFCondition cond = EC_Normal;
    
    if (strcmp(req.AffectedSOPClassUID, UID_BasicFilmSessionSOPClass) == 0) {
        cond = handleFilmSessionCreate();
    }
    else if (strcmp(req.AffectedSOPClassUID, UID_BasicFilmBoxSOPClass) == 0) {
        cond = handleFilmBoxCreate();
    }
    else if (strcmp(req.AffectedSOPClassUID, UID_PrinterSOPClass) == 0) {
        cond = handlePrinterCreate();
    }
    else {
        std::cout << "❌ SOP Class غير معروف: " << req.AffectedSOPClassUID << std::endl;
        cond = EC_IllegalParameter;
    }
    
    // إرسال الرد
    Uint16 status = cond.good() ? STATUS_Success : STATUS_N_NoSuchAttribute;
    return sendNCreateResponse(req, presID, status);
}

OFCondition PrintSCP::handleNActionRequest(const T_DIMSE_N_ActionRQ& req,
                                          T_ASC_PresentationContextID presID) {
    std::cout << "⚡ معالجة N-ACTION للنوع: " << req.ActionTypeID << std::endl;
    std::cout << "📋 SOP Class: " << req.RequestedSOPClassUID << std::endl;
    std::cout << "🔑 SOP Instance: " << req.RequestedSOPInstanceUID << std::endl;
    
    // محاكاة عملية الطباعة
    std::cout << "🖨️ بدء محاكاة الطباعة..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "✅ اكتملت محاكاة الطباعة" << std::endl;
    
    // إرسال رد النجاح
    T_DIMSE_Message response;
    memset(&response, 0, sizeof(response));
    
    response.CommandField = DIMSE_N_ACTION_RSP;
    response.msg.NActionRSP.MessageIDBeingRespondedTo = req.MessageID;
    response.msg.NActionRSP.ActionTypeID = req.ActionTypeID;
    response.msg.NActionRSP.DimseStatus = STATUS_Success;
    response.msg.NActionRSP.DataSetType = DIMSE_DATASET_NULL;
    
    OFString sopClassUID = req.RequestedSOPClassUID;
    OFString sopInstanceUID = req.RequestedSOPInstanceUID;
    
    return DIMSE_sendMessageUsingMemoryData(currentAssociation_, presID, 
                                          &response, NULL, NULL, NULL, NULL);
}

OFCondition PrintSCP::handleNDeleteRequest(const T_DIMSE_N_DeleteRQ& req,
                                          T_ASC_PresentationContextID presID) {
    std::cout << "🗑️ معالجة N-DELETE" << std::endl;
    std::cout << "📋 SOP Class: " << req.RequestedSOPClassUID << std::endl;
    std::cout << "🔑 SOP Instance: " << req.RequestedSOPInstanceUID << std::endl;
    
    const char* instanceUID = req.RequestedSOPInstanceUID;
    if (instanceUID) {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        printSessions_.erase(instanceUID);
        std::cout << "✅ حذف جلسة: " << instanceUID << std::endl;
    }
    
    // إرسال رد النجاح
    T_DIMSE_Message response;
    memset(&response, 0, sizeof(response));
    
    response.CommandField = DIMSE_N_DELETE_RSP;
    response.msg.NDeleteRSP.MessageIDBeingRespondedTo = req.MessageID;
    response.msg.NDeleteRSP.DimseStatus = STATUS_Success;
    response.msg.NDeleteRSP.DataSetType = DIMSE_DATASET_NULL;
    
    OFString sopClassUID = req.RequestedSOPClassUID;
    OFString sopInstanceUID = req.RequestedSOPInstanceUID;
    
    return DIMSE_sendMessageUsingMemoryData(currentAssociation_, presID, 
                                          &response, NULL, NULL, NULL, NULL);
}

OFCondition PrintSCP::sendNCreateResponse(const T_DIMSE_N_CreateRQ& req,
                                         T_ASC_PresentationContextID presID,
                                         Uint16 status) {
    if (!currentAssociation_) {
        return EC_IllegalCall;
    }
    
    // إعداد رسالة الرد
    T_DIMSE_Message response;
    memset(&response, 0, sizeof(response));
    
    response.CommandField = DIMSE_N_CREATE_RSP;
    response.msg.NCreateRSP.MessageIDBeingRespondedTo = req.MessageID;
    response.msg.NCreateRSP.DimseStatus = status;
    response.msg.NCreateRSP.DataSetType = DIMSE_DATASET_NULL;
    
    // إنشاء مجموعة بيانات الرد (اختياري)
    DcmDataset* rspDataset = new DcmDataset();
    if (rspDataset && status == STATUS_Success) {
        // إضافة بعض السمات الأساسية للرد
        rspDataset->putAndInsertString(DCM_NumberOfCopies, "1");
        rspDataset->putAndInsertString(DCM_PrintPriority, "MED");
        rspDataset->putAndInsertString(DCM_MediumType, "PAPER");
    }
    
    // إرسال الرد
    OFCondition sendCond = DIMSE_sendMessageUsingMemoryData(
        currentAssociation_, presID, &response, NULL, rspDataset, NULL, NULL);
    
    if (rspDataset) {
        delete rspDataset;
    }
    
    if (sendCond.good()) {
        std::cout << "✅ تم إرسال رد N-CREATE بنجاح" << std::endl;
    } else {
        std::cerr << "❌ فشل في إرسال رد N-CREATE: " << sendCond.text() << std::endl;
    }
    
    return sendCond;
}

OFCondition PrintSCP::handleFilmSessionCreate() {
    std::cout << "🎞️ معالجة إنشاء جلسة فيلم" << std::endl;
    
    // حفظ الجلسة
    std::lock_guard<std::mutex> lock(sessionMutex_);
    // نستخدم معرف افتراضي حيث أننا لا نحصل على البيانات
    printSessions_["default_session"] = "ACTIVE";
    std::cout << "✅ إنشاء جلسة طباعة جديدة" << std::endl;
    
    return EC_Normal;
}

OFCondition PrintSCP::handleFilmBoxCreate() {
    std::cout << "📦 معالجة إنشاء صندوق فيلم" << std::endl;
    return EC_Normal;
}

OFCondition PrintSCP::handlePrinterCreate() {
    std::cout << "🖨️ معالجة إنشاء طابعة" << std::endl;
    return EC_Normal;
}
