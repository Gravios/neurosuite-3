add_executable(process_smrconvert
    process_smrconvert.cpp sonadcchannels.cpp sonadcmarkchannel.cpp
    sonblockheaders.cpp sonchannel.cpp sonchannelinfo.cpp sonerror.cpp
    soneventchannel.cpp sonextractor.cpp sonfile.cpp sonfileheader.cpp)
target_compile_features(process_smrconvert PRIVATE cxx_std_17)
install(TARGETS process_smrconvert DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_smrconvert)
