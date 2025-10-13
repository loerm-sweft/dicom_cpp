@echo off
chcp 65001 > nul
echo ==================================
echo    DICOM Print SCP - Ready
echo ==================================

cd build
echo Starting DICOM Print SCP Server...
echo AE Title: DICOM_PRINT_SCP
echo Port: 11112
echo.
echo Use Ctrl+C to stop the server
echo ==================================
Release\DICOMPrintSCP.exe