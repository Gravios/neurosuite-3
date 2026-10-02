# process_initeap — pre-construct the EAP membership layer (.eap + .tcl) for a
# group.  Links the Qt-free core lib for initEap / initTcl / res I/O.
add_executable(process_initeap process_initeap.cpp)
target_compile_features(process_initeap PRIVATE cxx_std_17)
target_link_libraries(process_initeap PRIVATE Neurosuite::core)
install(TARGETS process_initeap DESTINATION "${CMAKE_INSTALL_BINDIR}")
