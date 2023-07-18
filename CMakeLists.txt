cmake_minimum_required(VERSION 2.5)
project(mymuduo)

# cmake => makefile   make
# mymuduo最终编译成so动态库，设置动态库的路径，放在根目录的lib文件夹下面
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# 配置编译选项：设置调试信息、启动C++11语言标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -fPIC")

# 配置头文件信息
include_directories(${PROJECT_SOURCE_DIR}/include)

# 将.指定路径下，所有源文件名字放入变量名SRC_LIST下里面
aux_source_directory(${PROJECT_SOURCE_DIR}/src SRC_LIST) 
aux_source_directory(${PROJECT_SOURCE_DIR}/include SRC_LIST) 

# 编译生成动态库mymuduo
add_library(mymuduo SHARED ${SRC_LIST})