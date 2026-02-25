/***************************************************************************
 * channelcolors.h  (ndmanager)
 *
 * The per-channel colour/id value type used throughout ndmanager is now
 * ChannelColorEntry from libklustersshared.  This header re-exports it
 * under the legacy name so the rest of ndmanager requires no changes.
 *
 * Note: this is distinct from the ChannelColors *container* class in
 * libklustersshared/channelcolors.h which maps channel ids to QColors
 * for neuroscope/klusters rendering.
 *
 * Copyright (C) 2024  neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include "channelcolorentry.h"

// Legacy alias — all ndmanager code that uses ChannelColors (the per-channel
// value struct with getId/setColor/setGroupColor/setSpikeGroupColor) continues
// to compile without modification.
using ChannelColors = ChannelColorEntry;
