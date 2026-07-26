add_executable(autoterminal_tests
    test_tile_engine.cpp
    test_config_store.cpp
    test_hotkey.cpp
    test_monitor_index_layout.cpp
)
target_link_libraries(autoterminal_tests PRIVATE
    autoterminal_core
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(autoterminal_tests)