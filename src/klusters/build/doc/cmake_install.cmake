# Install script for directory: /home/gravio/software/neurosuite-3/src/klusters/doc

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/klusters/HTML/en" TYPE FILE FILES
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/a1717.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/c1059.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/c1697.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/c41.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/c50.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/c835.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/c916.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/index.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x108.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1154.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1204.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1280.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1342.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1391.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1459.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1498.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1572.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1654.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1725.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x1739.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x178.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x368.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x482.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x539.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x638.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x705.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x730.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x769.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x782.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x808.html"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/html/x972.html"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/klusters/HTML/en/Images" TYPE FILE FILES
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/GroupingAssistant-Change.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/GroupingAssistant.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/Klusters.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/MultipleProjections.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/Overview.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/Parameters-Default.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/Parameters-TimeFrame.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/Parameters.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/Tools.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/TraceView.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/Waveform-Mean.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/Waveform-Overlay.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/Waveform-Scale.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/XCorrelation-Asymptote.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/XCorrelation-Raw.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/backCluster.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/clusterInformationWindow.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/clusterview.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/delete_artefact.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/delete_artefact_tool.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/delete_noise.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/delete_noise_tool.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/forwardCluster.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/group.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/grouping_assistant_update.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/new_cluster.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/new_clusters.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/printer.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/time_tool.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/update.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/waveformview.png"
    "/home/gravio/software/neurosuite-3/src/klusters/doc/en/Images/zoom_tool.png"
    )
endif()

