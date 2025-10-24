cmake_minimum_required(VERSION 3.15)
project(DICOMPrintSCP)

# استخدام vcpkg toolchain
set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")

# إعداد C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
endif()


# البحث عن DCMTK
find_package(DCMTK REQUIRED CONFIG)

# إنشاء التنفيذي
add_executable(DICOMPrintSCP 
    src/main.cpp
    src/PrintSCP.cpp
)

 
 
# إعدادات الربط
target_link_libraries(DICOMPrintSCP PRIVATE 
    DCMTK::dcmdata
    DCMTK::dcmnet
    DCMTK::ofstd
    ws2_32
    netapi32
)

target_include_directories(DICOMPrintSCP PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
