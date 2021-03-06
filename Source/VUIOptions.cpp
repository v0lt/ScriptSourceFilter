/*
 * (C) 2020 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include <fstream>
#include <d3d9types.h>
#include <dxva2api.h>
#include "Helper.h"
#include "VUIOptions.h"

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
	{ "bt2020",    VIDEOPRIMARIES_BT2020 },
};

static const str_value vui_transfer[] {
	{ "bt709",        DXVA2_VideoTransFunc_709 },
	{ "bt470m",       DXVA2_VideoTransFunc_22 },
	{ "bt470bg",      DXVA2_VideoTransFunc_28 },
	{ "smpte170m",    DXVA2_VideoTransFunc_709 },
	{ "smpte240m",    DXVA2_VideoTransFunc_240M },
	{ "linear",       DXVA2_VideoTransFunc_10 },
	{ "log100",       VIDEOTRANSFUNC_Log_100 },
	{ "log316",       VIDEOTRANSFUNC_Log_316 },
	{ "smpte2084",    VIDEOTRANSFUNC_2084 },
	{ "arib-std-b67", VIDEOTRANSFUNC_HLG },
};

static const str_value vui_colormatrix[] {
	{ "bt709",     DXVA2_VideoTransferMatrix_BT709 },
	{ "fcc",       VIDEOTRANSFERMATRIX_FCC },
	{ "bt470bg",   DXVA2_VideoTransferMatrix_BT601 },
	{ "smpte240m", DXVA2_VideoTransferMatrix_SMPTE240M },
	{ "YCgCo",     VIDEOTRANSFERMATRIX_YCgCo },
	{ "bt2020nc",  VIDEOTRANSFERMATRIX_BT2020_10 },
	{ "bt2020c",   VIDEOTRANSFERMATRIX_BT2020_10 },
};

static const str_value vui_chromaloc[] {
	{ "0",     DXVA2_VideoChromaSubsampling_MPEG2 },
	{ "1",     DXVA2_VideoChromaSubsampling_MPEG1 },
	{ "2",     DXVA2_VideoChromaSubsampling_Cosited },
};

UINT GetColorInfoFromVUIOptions(LPCWSTR scriptfile)
{
	DXVA2_ExtendedFormat exFmt = {};
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

		if (exFmt.value) {
			exFmt.value |= (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT);
		}
	}

	return exFmt.value;
}
