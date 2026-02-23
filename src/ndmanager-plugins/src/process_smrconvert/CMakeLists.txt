project(ndmanager-plugins-src-process_smrconvert)
cmake_minimum_required(VERSION 3.16)

set(program process_smrconvert)
set(sources
    process_smrconvert.cpp sonadcchannels.cpp sonadcmarkchannel.cpp
    sonblockheaders.cpp sonchannel.cpp sonchannelinfo.cpp sonerror.cpp
    soneventchannel.cpp sonextractor.cpp sonfile.cpp sonfileheader.cpp)
add_executable(${program} ${sources})
target_compile_features(${program} PRIVATE cxx_std_17)
install(TARGETS ${program} DESTINATION bin)
add_manpage(${program})
