# ---------------------------------------------------------------------------
# test_scrollview — internal scrollview test (not installed)
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(test_scrollview
    main.cpp listview.cpp mainview.cpp scrollarea.cpp itemgroupview.cpp)

target_link_libraries(test_scrollview PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets)
