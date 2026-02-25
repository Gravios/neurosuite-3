/***************************************************************************
 * ndmanageryamlwriter.h  (ndmanager)
 *
 * Thin delegator to ParameterYamlWriter (libklustersshared).
 * The full implementation has moved to the shared library.
 *
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include "parameteryamlwriter.h"
#include "channelcolors.h"  // ChannelColors = ChannelColorEntry

#include <QList>
#include <QMap>
#include <QString>

/**
 * @brief ndmanager YAML writer — now a typedef for ParameterYamlWriter.
 *
 * All ndmanager code that instantiates NdManagerYamlWriter continues to
 * compile and behave correctly; the actual work is done by ParameterYamlWriter.
 */
using NdManagerYamlWriter = ParameterYamlWriter;
