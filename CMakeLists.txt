cmake_minimum_required(VERSION 3.16)
project(ros2_recorder)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcpputils REQUIRED)
find_package(rmw REQUIRED)
find_package(rosidl_runtime_cpp REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)

add_executable(recorder
    src/main.cpp
    src/node.cpp
    src/csv/file.cpp
    src/csv/formatter.cpp
)
target_include_directories(recorder PRIVATE include ${rosidl_typesupport_introspection_cpp_INCLUDE_DIRS})
target_link_libraries(recorder PRIVATE
    rclcpp::rclcpp
    rcpputils::rcpputils
    rmw::rmw
    ${rosidl_runtime_cpp_TARGETS}
)

install(TARGETS recorder
    DESTINATION lib/${PROJECT_NAME}
)

install(
    DIRECTORY config launch
    DESTINATION share/${PROJECT_NAME}
)

ament_package()
