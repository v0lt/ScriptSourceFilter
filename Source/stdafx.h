/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#define VC_EXTRALEAN        // Exclude rarely-used stuff from Windows headers

#include <atlbase.h>
#include <atlwin.h>

#include <dmodshow.h>
#include <dvdmedia.h>
#include <VersionHelpers.h>

#include <algorithm>
#include <numeric>
#include <vector>
#include <exception>
#include <string>
#include <format>

#include "../external/BaseClasses/streams.h"
