#include <iostream>
#include <winsock2.h>
#include <windows.h>

// DCMTK headers
#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmnet/dimse.h>
#include <dcmtk/dcmnet/diutil.h>
#include <dcmtk/dcmnet/assoc.h>
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcuid.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "netapi32.lib")

#define PORT 11112
#define AE_TITLE "DICOM_PRINT_SCP"

// Supported SOP Classes for DICOM Print
const char* PRINT_SOP_CLASSES[] = {
    UID_BasicFilmSessionSOPClass,
    UID_BasicFilmBoxSOPClass,
    UID_BasicGrayscaleImageBoxSOPClass,
    UID_PrinterSOPClass,
    UID_BasicColorImageBoxSOPClass,
    UID_BasicGrayscalePrintManagementMetaSOPClass,
    NULL
};

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        std::cout << "\nReceived stop signal. Shutting down..." << std::endl;
        exit(0);
    }
    return TRUE;
}

// Function to accept presentation contexts for print SOP classes
OFCondition acceptPrintPresentationContexts(T_ASC_Parameters* params) {
    OFCondition cond = EC_Normal;
    
    int presentationContextCount = ASC_countPresentationContexts(params);
    std::cout << "Number of presentation contexts: " << presentationContextCount << std::endl;
    
    for (int i = 0; i < presentationContextCount; i++) {
        T_ASC_PresentationContext pc;
        cond = ASC_getPresentationContext(params, i, &pc);
        if (cond.good()) {
            std::cout << "Context " << (int)pc.presentationContextID 
                      << ": " << pc.abstractSyntax << std::endl;
            
            // Check if this is a print-related SOP Class
            bool isPrintSOP = false;
            for (int j = 0; PRINT_SOP_CLASSES[j] != NULL; j++) {
                if (strcmp(pc.abstractSyntax, PRINT_SOP_CLASSES[j]) == 0) {
                    isPrintSOP = true;
                    break;
                }
            }
            
            if (isPrintSOP) {
                // Accept print SOP classes with the first proposed transfer syntax
                cond = ASC_acceptPresentationContext(
                    params, 
                    pc.presentationContextID, 
                    pc.proposedTransferSyntaxes[0],  // Use first proposed transfer syntax
                    ASC_SC_ROLE_DEFAULT
                );
                if (cond.good()) {
                    std::cout << "  -> ACCEPTED (Print SOP)" << std::endl;
                } else {
                    std::cerr << "  -> Failed to accept: " << cond.text() << std::endl;
                }
            } else {
                // For non-print SOP classes, we can either reject or ignore
                // In this case, we'll just not accept them (they will be rejected automatically)
                std::cout << "  -> IGNORED (Not a Print SOP)" << std::endl;
            }
        }
    }
    
    return cond;
}

// Simple function to handle received associations
void handleAssociation(T_ASC_Association* assoc) {
    if (!assoc) return;
    
    std::cout << "Processing DICOM Print requests..." << std::endl;
    
    T_DIMSE_Message msg;
    T_ASC_PresentationContextID presID;
    OFCondition cond = EC_Normal;
    
    // Handle incoming messages for a short time
    for (int i = 0; i < 10; i++) { // Limit to 10 messages to prevent hanging
        cond = DIMSE_receiveCommand(assoc, DIMSE_NONBLOCKING, 1, &presID, &msg, NULL);
        
        if (cond.good()) {
            switch (msg.CommandField) {
                case DIMSE_N_CREATE_RQ:
                    std::cout << "  Received N-CREATE" << std::endl;
                    std::cout << "    SOP Class: " << msg.msg.NCreateRQ.AffectedSOPClassUID << std::endl;
                    // Send success response
                    {
                        T_DIMSE_Message rsp;
                        memset(&rsp, 0, sizeof(rsp));
                        rsp.CommandField = DIMSE_N_CREATE_RSP;
                        rsp.msg.NCreateRSP.MessageIDBeingRespondedTo = msg.msg.NCreateRQ.MessageID;
                        rsp.msg.NCreateRSP.DimseStatus = STATUS_Success;
                        rsp.msg.NCreateRSP.DataSetType = DIMSE_DATASET_NULL;
                        DIMSE_sendMessageUsingMemoryData(assoc, presID, &rsp, NULL, NULL, NULL, NULL);
                    }
                    break;
                    
                case DIMSE_N_ACTION_RQ:
                    std::cout << "  Received N-ACTION" << std::endl;
                    // Send success response
                    {
                        T_DIMSE_Message rsp;
                        memset(&rsp, 0, sizeof(rsp));
                        rsp.CommandField = DIMSE_N_ACTION_RSP;
                        rsp.msg.NActionRSP.MessageIDBeingRespondedTo = msg.msg.NActionRQ.MessageID;
                        rsp.msg.NActionRSP.DimseStatus = STATUS_Success;
                        rsp.msg.NActionRSP.DataSetType = DIMSE_DATASET_NULL;
                        DIMSE_sendMessageUsingMemoryData(assoc, presID, &rsp, NULL, NULL, NULL, NULL);
                    }
                    break;
                    
                case DIMSE_N_DELETE_RQ:
                    std::cout << "  Received N-DELETE" << std::endl;
                    // Send success response
                    {
                        T_DIMSE_Message rsp;
                        memset(&rsp, 0, sizeof(rsp));
                        rsp.CommandField = DIMSE_N_DELETE_RSP;
                        rsp.msg.NDeleteRSP.MessageIDBeingRespondedTo = msg.msg.NDeleteRQ.MessageID;
                        rsp.msg.NDeleteRSP.DimseStatus = STATUS_Success;
                        rsp.msg.NDeleteRSP.DataSetType = DIMSE_DATASET_NULL;
                        DIMSE_sendMessageUsingMemoryData(assoc, presID, &rsp, NULL, NULL, NULL, NULL);
                    }
                    break;
                    
                default:
                    if (msg.CommandField != 0) { // Ignore empty messages
                        std::cout << "  Received DIMSE command: " << msg.CommandField << std::endl;
                    }
                    break;
            }
        } else if (cond != DIMSE_NODATAAVAILABLE) {
            break; // Break on real errors
        }
        
        Sleep(100); // Small delay between checks
    }
    
    std::cout << "Finished processing requests" << std::endl;
}

int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "   DICOM Print SCP - C++/DCMTK   " << std::endl;
    std::cout << "        Windows Version          " << std::endl;
    std::cout << "==================================" << std::endl;

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return 1;
    }

    // Initialize DCMTK network
    T_ASC_Network* network = NULL;
    OFCondition cond = ASC_initializeNetwork(NET_ACCEPTOR, PORT, 30, &network);
    if (cond.bad()) {
        std::cerr << "Failed to initialize DCMTK network: " << cond.text() << std::endl;
        WSACleanup();
        return 1;
    }

    std::cout << "Starting DICOM Print SCP..." << std::endl;
    std::cout << "AE Title: " << AE_TITLE << std::endl;
    std::cout << "Port: " << PORT << std::endl;
    std::cout << "Waiting for connections..." << std::endl;
    std::cout << "==================================" << std::endl;

    while (true) {
        T_ASC_Association* assoc = NULL;
        
        // Receive association
        cond = ASC_receiveAssociation(network, &assoc, ASC_DEFAULTMAXPDU);
        
        if (cond.bad()) {
            if (cond == DUL_PEERREQUESTEDRELEASE || cond == DUL_PEERABORTEDASSOCIATION) {
                // Normal disconnection
                continue;
            }
            continue;
        }

        std::cout << "New connection from: " << assoc->params->DULparams.callingAPTitle << std::endl;

        // Accept presentation contexts for print SOP classes
        cond = acceptPrintPresentationContexts(assoc->params);
        if (cond.bad()) {
            std::cerr << "Failed to accept presentation contexts: " << cond.text() << std::endl;
            ASC_dropAssociation(assoc);
            ASC_destroyAssociation(&assoc);
            continue;
        }

        // Acknowledge association
        cond = ASC_acknowledgeAssociation(assoc);
        if (cond.bad()) {
            std::cerr << "Failed to acknowledge association: " << cond.text() << std::endl;
            ASC_dropAssociation(assoc);
            ASC_destroyAssociation(&assoc);
            continue;
        }

        std::cout << "Association accepted successfully!" << std::endl;

        // Handle the association
        handleAssociation(assoc);

        // Drop association
        ASC_dropAssociation(assoc);
        ASC_destroyAssociation(&assoc);
        
        std::cout << "Connection closed" << std::endl;
        std::cout << "==================================" << std::endl;
        std::cout << "Waiting for new connections..." << std::endl;
    }

    ASC_dropNetwork(&network);
    WSACleanup();
    return 0;
}