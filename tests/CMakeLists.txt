project(tst_qjsonsettings)

set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Test REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)

add_executable(${PROJECT_NAME} tst_qjsonsettings.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Test)
target_link_libraries(${PROJECT_NAME} PRIVATE qjsonsettings)