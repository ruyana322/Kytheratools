# Versi minimum CMake bawaan Android Studio
cmake_minimum_required(VERSION 3.10.2)

# Nama proyek C++
project("kythera_engine")

# Flag compiler -O3, OpenMP, fast-math, c++17 untuk performa AI & threading maksimal
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -O3 -fopenmp -ffast-math")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -fopenmp -ffast-math")

# ==========================================
# 1. SETUP MESIN NCNN VULKAN
# ==========================================
set(NCNN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Ncnn-vulkan)
include_directories(${NCNN_DIR}/${ANDROID_ABI}/include/ncnn)

add_library(ncnn STATIC IMPORTED)
set_target_properties(ncnn PROPERTIES IMPORTED_LOCATION
    ${NCNN_DIR}/${ANDROID_ABI}/lib/libncnn.a)

# ==========================================
# 2. SETUP HEADER OPENCV & MNN
# ==========================================
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ==========================================
# 3. PANGGIL LIBRARY BAWAAN ANDROID
# ==========================================
find_library(log-lib log)
find_library(jnigraphics-lib jnigraphics) 
find_library(android-lib android)         
find_library(vulkan-lib vulkan)           

# ==========================================
# 4. RAKIT SEMUA JEMBATAN C++ (BRIDGE)
# ==========================================

# A. NCNN Bridge (Utama)
add_library(ncnn_bridge SHARED NcnnVideoBridge.cpp)
target_link_libraries(ncnn_bridge
                      ncnn
                      ${vulkan-lib}
                      ${log-lib}
                      ${jnigraphics-lib}
                      ${android-lib})

# B. OpenCV Bridge
add_library(opencv_bridge SHARED OpenCvBridge.cpp)
target_link_libraries(opencv_bridge
                      ${log-lib}
                      ${jnigraphics-lib}
                      ${android-lib})

# C. MNN Bridge
add_library(mnn_bridge SHARED MnnVideoBridge.cpp)
target_link_libraries(mnn_bridge
                      ${log-lib}
                      ${jnigraphics-lib}
                      ${android-lib})

