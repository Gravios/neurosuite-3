# Installation

## How to obtain Neuroscope {#getting-neuroscope}

NeuroScope can be found on <http://neuroscope.sourceforge.net>.

## Requirements

NeuroScope requires QT \>= 4.8 and [libxml2](http://www.xmlsoft.org/downloads.html) \>= 2.5.4

To build NeuroScope from source, you will also need the corresponding devel packages and an ANSI C++ Standard compliant compiler (gcc version 3.2.2 is known to work).

## Compilation and Installation {#compilation}

In order to compile and install NeuroScope on your system, download and extract the source archive, then type the following in the base directory:

        % cmake -DCMAKE_INSTALL_PREFIX=... ./
    % cd src
    % make
    % make install

