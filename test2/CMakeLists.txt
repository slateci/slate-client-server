add_executable(test2 main.cpp unit_tests.cpp)
target_include_directories (test2
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        )
target_link_libraries(test2
        PUBLIC
        slate-server
        )
