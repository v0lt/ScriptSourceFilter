/*
 * Copyright (C) 2020-2024 v0lt
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include "stdafx.h"
#include <fstream>
#include <d3d9types.h>
#include <dxva2api.h>
#include <mfobjects.h>
#include "Helper.h"
#include "VUIOptions.h"

// Rec. ITU-T H.264
// https://www.itu.int/itu-t/recommendations/rec.aspx?rec=14659

bool SetColorInfoFromFrameFrops(UINT& extFmtValue, const char* keyName, int64_t value)
{
	DXVA2_ExtendedFormat exFmt;
	exFmt.value = extFmtValue;

	if (strcmp(keyName, "_ChromaLocation") == 0) {
		// 0=left, 1=center, 2=topleft, 3=top, 4=bottomleft, 5=bottom.
		switch (value) {
		case 0: exFmt.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG2;   break;
		case 1: exFmt.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG1;   break;
		case 2: exFmt.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_Cosited; break;
		}
	}
	else if (strcmp(keyName, "_ColorRange") == 0) {
		// 0=full range, 1=limited range
		switch (value) {
		case 0: exFmt.NominalRange = DXVA2_NominalRange_0_255;  break;
		case 1: exFmt.NominalRange = DXVA2_NominalRange_16_235; break;
		}
	}
	else if (strcmp(keyName, "_Primaries") == 0) {
		switch (value) {
		case 1:  exFmt.VideoPrimaries = DXVA2_VideoPrimaries_BT709;         break;
		case 4:  exFmt.VideoPrimaries = DXVA2_VideoPrimaries_BT470_2_SysM;  break;
		case 5:  exFmt.VideoPrimaries = DXVA2_VideoPrimaries_BT470_2_SysBG; break;
		case 6:  exFmt.VideoPrimaries = DXVA2_VideoPrimaries_SMPTE170M;     break;
		case 7:  exFmt.VideoPrimaries = DXVA2_VideoPrimaries_SMPTE240M;     break;
		case 9:  exFmt.VideoPrimaries = MFVideoPrimaries_BT2020;            break;
		case 10: exFmt.VideoPrimaries = MFVideoPrimaries_XYZ;               break;
		case 11: exFmt.VideoPrimaries = MFVideoPrimaries_DCI_P3;            break;
		}
	}
	else if (strcmp(keyName, "_Matrix") == 0) {
		switch (value) {
		case 1:  exFmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709;     break;
		case 4:  exFmt.VideoTransferMatrix = VIDEOTRANSFERMATRIX_FCC;             break;
		case 5:
		case 6:  exFmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT601;     break;
		case 7:  exFmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_SMPTE240M; break;
		case 8:  exFmt.VideoTransferMatrix = VIDEOTRANSFERMATRIX_YCgCo;           break;
		case 10:
		case 11: exFmt.VideoTransferMatrix = MFVideoTransferMatrix_BT2020_10;     break;
		}
	}
	else if (strcmp(keyName, "_Transfer") == 0) {
		switch (value) {
		case 1:
		case 6:
		case 14:
		case 15: exFmt.VideoTransferFunction = DXVA2_VideoTransFunc_709;  break;
		case 4:  exFmt.VideoTransferFunction = DXVA2_VideoTransFunc_22;   break;
		case 5:  exFmt.VideoTransferFunction = DXVA2_VideoTransFunc_28;   break;
		case 7:  exFmt.VideoTransferFunction = DXVA2_VideoTransFunc_240M; break;
		case 8:  exFmt.VideoTransferFunction = DXVA2_VideoTransFunc_10;   break;
		case 9:  exFmt.VideoTransferFunction = MFVideoTransFunc_Log_100;  break;
		case 10: exFmt.VideoTransferFunction = MFVideoTransFunc_Log_316;  break;
		case 16: exFmt.VideoTransferFunction = MFVideoTransFunc_2084;     break;
		case 18: exFmt.VideoTransferFunction = MFVideoTransFunc_HLG;      break;
		}
	}

	if (exFmt.value != extFmtValue) {
		extFmtValue = exFmt.value;
		return true;
	}
	else {
		return false;
	}
}

/*
"Video Usability Info" https://code.videolan.org/videolan/x264/-/blob/master/x264.c

--range <string>        Specify color range
    auto, tv, pc

--colorprim <string>    Specify color primaries
    undef, bt709, bt470m, bt470bg, smpte170m,
    smpte240m, film, bt2020, smpte428,
    smpte431, smpte432

--transfer <string>     Specify transfer characteristics
    undef, bt709, bt470m, bt470bg, smpte170m,
    smpte240m, linear, log100, log316,
    iec61966-2-4, bt1361e, iec61966-2-1,
    bt2020-10, bt2020-12, smpte2084, smpte428,
    arib-std-b67

--colormatrix <string>  Specify color matrix setting
    undef, bt709, fcc, bt470bg, smpte170m,
    smpte240m, GBR, YCgCo, bt2020nc, bt2020c,
    smpte2085, chroma-derived-nc,
    chroma-derived-c, ICtCp

--chromaloc <integer>   Specify chroma sample location (0 to 5)
    0 - Left(MPEG-2)
    1 - Center(MPEG-1)
    2 - TopLeft(Co-sited)
*/

struct str_value {
	LPCSTR str;
	UINT value;
};

static const str_value vui_range[] {
	{ "tv", DXVA2_NominalRange_16_235 },
	{ "pc", DXVA2_NominalRange_0_255 },
};

static const str_value vui_colorprim[] {
	{ "bt709",     DXVA2_VideoPrimaries_BT709 },
	{ "bt470m",    DXVA2_VideoPrimaries_BT470_2_SysM },
	{ "bt470bg",   DXVA2_VideoPrimaries_BT470_2_SysBG },
	{ "smpte170m", DXVA2_VideoPrimaries_SMPTE170M },
	{ "smpte240m", DXVA2_VideoPrimaries_SMPTE240M },
	{ "bt2020",    MFVideoPrimaries_BT2020 },
};

static const str_value vui_transfer[] {
	{ "bt709",        DXVA2_VideoTransFunc_709 },
	{ "bt470m",       DXVA2_VideoTransFunc_22 },
	{ "bt470bg",      DXVA2_VideoTransFunc_28 },
	{ "smpte170m",    DXVA2_VideoTransFunc_709 },
	{ "smpte240m",    DXVA2_VideoTransFunc_240M },
	{ "linear",       DXVA2_VideoTransFunc_10 },
	{ "log100",       MFVideoTransFunc_Log_100 },
	{ "log316",       MFVideoTransFunc_Log_316 },
	{ "smpte2084",    MFVideoTransFunc_2084 },
	{ "arib-std-b67", MFVideoTransFunc_HLG },
};

static const str_value vui_colormatrix[] {
	{ "bt709",     DXVA2_VideoTransferMatrix_BT709 },
	{ "fcc",       VIDEOTRANSFERMATRIX_FCC },
	{ "bt470bg",   DXVA2_VideoTransferMatrix_BT601 },
	{ "smpte240m", DXVA2_VideoTransferMatrix_SMPTE240M },
	{ "YCgCo",     VIDEOTRANSFERMATRIX_YCgCo },
	{ "bt2020nc",  MFVideoTransferMatrix_BT2020_10 },
	{ "bt2020c",   MFVideoTransferMatrix_BT2020_10 },
};

static const str_value vui_chromaloc[] {
	{ "0",     DXVA2_VideoChromaSubsampling_MPEG2 },
	{ "1",     DXVA2_VideoChromaSubsampling_MPEG1 },
	{ "2",     DXVA2_VideoChromaSubsampling_Cosited },
};

bool SetColorInfoFromVUIOptions(UINT& extFmtValue, LPCWSTR scriptfile)
{
	DXVA2_ExtendedFormat exFmt;
	exFmt.value = extFmtValue;

	std::string vui_options;
	std::ifstream scrypt(scriptfile);

	if (scrypt.is_open()) {
		const std::string vui_prefix("# $VUI:");
		std::string line;
		while (std::getline(scrypt, line)) {
			// looking for the last line of VUI options
			if (line.compare(0, vui_prefix.size(), vui_prefix) == 0) {
				vui_options = line.substr(vui_prefix.size());
			}
		}
		scrypt.close();
	}

	std::vector<std::string> tokens;
	str_split(vui_options, tokens, ' ');

	if (tokens.size() >= 2) {
		unsigned i = 0;
		while (i+1 < tokens.size()) {
			const auto& param = tokens[i];
			const auto& value = tokens[i+1];
			if (param == "--range") {
				for (const auto& item : vui_range) {
					if (value == item.str) {
						exFmt.NominalRange = item.value;
						i++;
						break;
					}
				}
			}
			else if (param == "--colorprim") {
				for (const auto& item : vui_colorprim) {
					if (value == item.str) {
						exFmt.VideoPrimaries = item.value;
						i++;
						break;
					}
				}
			}
			else if (param == "--transfer") {
				for (const auto& item : vui_transfer) {
					if (value == item.str) {
						exFmt.VideoTransferFunction = item.value;
						i++;
						break;
					}
				}
			}
			else if (param == "--colormatrix") {
				for (const auto& item : vui_colormatrix) {
					if (value == item.str) {
						exFmt.VideoTransferMatrix = item.value;
						i++;
						break;
					}
				}
			}
			else if (param == "--chromaloc") {
				for (const auto& item : vui_chromaloc) {
					if (value == item.str) {
						exFmt.VideoChromaSubsampling = item.value;
						i++;
						break;
					}
				}
			}
			i++;
		}
	}

	if (exFmt.value != extFmtValue) {
		extFmtValue = exFmt.value;
		return true;
	}
	else {
		return false;
	}
}
