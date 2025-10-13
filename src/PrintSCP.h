#pragma once

#include <iostream>
#include <string>
#include <map>
#include <mutex>
#include <thread>

// DCMTK Headers
#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmnet/dimse.h>
#include <dcmtk/dcmnet/diutil.h>
#include <dcmtk/dcmnet/assoc.h>
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dctk.h>

class PrintSCP {
private:
    std::mutex sessionMutex_;
    std::map<std::string, std::string> printSessions_;
    T_ASC_Association* currentAssociation_;

public:
    PrintSCP();
    virtual ~PrintSCP();

    // معالجة الاتصال المبسطة
    OFCondition handleAssociation(T_ASC_Association* assoc);

protected:
    // معالجة طلبات DIMSE المبسطة
    virtual OFCondition handleNCreateRequest(const T_DIMSE_N_CreateRQ& req,
                                           T_ASC_PresentationContextID presID);
    
    virtual OFCondition handleNActionRequest(const T_DIMSE_N_ActionRQ& req,
                                           T_ASC_PresentationContextID presID);
    
    virtual OFCondition handleNDeleteRequest(const T_DIMSE_N_DeleteRQ& req,
                                           T_ASC_PresentationContextID presID);

private:
    OFCondition handleFilmSessionCreate();
    OFCondition handleFilmBoxCreate();
    OFCondition handlePrinterCreate();
    
    // إرسال ردود DIMSE
    OFCondition sendNCreateResponse(const T_DIMSE_N_CreateRQ& req,
                                   T_ASC_PresentationContextID presID,
                                   Uint16 status);
};