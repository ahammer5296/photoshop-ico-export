// ICO Format - Photoshop file format plug-in (write only).
// Saves the flattened RGB 8-bit document (with transparency) as a multi-size
// Windows .ico made of PNG-encoded images (supported since Windows Vista).

#include "PIDefines.h"
#include "PIFormat.h"
#include "PIUtilities.h"
#include "FileUtilities.h"

#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

SPBasicSuite* sSPBasic = NULL;
SPPluginRef gPluginRef = NULL;
FormatRecordPtr gFormatRecord = NULL;
int16* gResult = NULL;

typedef std::vector<uint8_t> Bytes;

//-------------------------------------------------------------------------------
// PNG encoder (RGBA8, filter 0)
//-------------------------------------------------------------------------------

static void PutBE32(Bytes& b, uint32_t v)
{
	b.push_back(v >> 24); b.push_back(v >> 16); b.push_back(v >> 8); b.push_back(v);
}

static void PutChunk(Bytes& out, const char* type, const Bytes& data)
{
	PutBE32(out, (uint32_t)data.size());
	Bytes td(type, type + 4);
	td.insert(td.end(), data.begin(), data.end());
	out.insert(out.end(), td.begin(), td.end());
	PutBE32(out, (uint32_t)crc32(0L, td.data(), (uInt)td.size()));
}

static bool EncodePng(const Bytes& rgba, int s, Bytes& out)
{
	static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	out.assign(sig, sig + 8);

	Bytes ihdr;
	PutBE32(ihdr, s); PutBE32(ihdr, s);
	ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
	PutChunk(out, "IHDR", ihdr);

	size_t stride = (size_t)s * 4;
	Bytes raw((stride + 1) * s);
	for (int y = 0; y < s; ++y)
	{
		raw[(stride + 1) * y] = 0;
		memcpy(&raw[(stride + 1) * y + 1], &rgba[stride * y], stride);
	}
	uLongf cap = compressBound((uLong)raw.size());
	Bytes z(cap);
	if (compress2(z.data(), &cap, raw.data(), (uLong)raw.size(), 9) != Z_OK) return false;
	z.resize(cap);
	PutChunk(out, "IDAT", z);
	PutChunk(out, "IEND", Bytes());
	return true;
}

//-------------------------------------------------------------------------------
// Resampling: area average with premultiplied alpha. The source is centered in
// a transparent square of side max(W,H), which is scaled to s x s.
//-------------------------------------------------------------------------------

static Bytes Resample(const Bytes& src, int W, int H, int s)
{
	const double L = std::max(W, H);
	const double ox = (L - W) / 2.0, oy = (L - H) / 2.0;
	const double step = L / s;
	Bytes out((size_t)s * s * 4, 0);

	for (int ty = 0; ty < s; ++ty)
	{
		double y0 = ty * step, y1 = y0 + step;
		int jy0 = std::max(0, (int)std::floor(y0 - oy));
		int jy1 = std::min(H - 1, (int)std::ceil(y1 - oy) - 1);
		for (int tx = 0; tx < s; ++tx)
		{
			double x0 = tx * step, x1 = x0 + step;
			int jx0 = std::max(0, (int)std::floor(x0 - ox));
			int jx1 = std::min(W - 1, (int)std::ceil(x1 - ox) - 1);

			double sr = 0, sg = 0, sb = 0, sa = 0;
			for (int jy = jy0; jy <= jy1; ++jy)
			{
				double wy = std::min(y1, oy + jy + 1) - std::max(y0, oy + jy);
				if (wy <= 0) continue;
				for (int jx = jx0; jx <= jx1; ++jx)
				{
					double wx = std::min(x1, ox + jx + 1) - std::max(x0, ox + jx);
					if (wx <= 0) continue;
					const uint8_t* p = &src[((size_t)jy * W + jx) * 4];
					double w = wx * wy * (p[3] / 255.0);
					sr += p[0] * w; sg += p[1] * w; sb += p[2] * w; sa += w;
				}
			}
			uint8_t* o = &out[((size_t)ty * s + tx) * 4];
			if (sa > 0)
			{
				o[0] = (uint8_t)std::min(255.0, sr / sa + 0.5);
				o[1] = (uint8_t)std::min(255.0, sg / sa + 0.5);
				o[2] = (uint8_t)std::min(255.0, sb / sa + 0.5);
				o[3] = (uint8_t)std::min(255.0, sa / (step * step) * 255.0 + 0.5);
			}
		}
	}
	return out;
}

//-------------------------------------------------------------------------------
// ICO container
//-------------------------------------------------------------------------------

static void PutLE16(Bytes& b, uint16_t v) { b.push_back(v); b.push_back(v >> 8); }
static void PutLE32(Bytes& b, uint32_t v) { PutLE16(b, v & 0xFFFF); PutLE16(b, v >> 16); }

static Bytes BuildIco(const std::vector<int>& sizes, const std::vector<Bytes>& pngs)
{
	Bytes out;
	PutLE16(out, 0); PutLE16(out, 1); PutLE16(out, (uint16_t)sizes.size());
	uint32_t offset = 6 + 16 * (uint32_t)sizes.size();
	for (size_t i = 0; i < sizes.size(); ++i)
	{
		uint8_t dim = sizes[i] >= 256 ? 0 : (uint8_t)sizes[i];
		out.push_back(dim); out.push_back(dim); out.push_back(0); out.push_back(0);
		PutLE16(out, 1); PutLE16(out, 32);
		PutLE32(out, (uint32_t)pngs[i].size());
		PutLE32(out, offset);
		offset += (uint32_t)pngs[i].size();
	}
	for (const Bytes& p : pngs) out.insert(out.end(), p.begin(), p.end());
	return out;
}

//-------------------------------------------------------------------------------
// Host helpers
//-------------------------------------------------------------------------------

static void GetImageSize(int32& w, int32& h)
{
	if (gFormatRecord->PluginUsing32BitCoordinates)
	{
		w = gFormatRecord->imageSize32.h; h = gFormatRecord->imageSize32.v;
	}
	else
	{
		w = gFormatRecord->imageSize.h; h = gFormatRecord->imageSize.v;
	}
}

static void SetRect(int32 w, int32 row)
{
	if (gFormatRecord->PluginUsing32BitCoordinates)
	{
		gFormatRecord->theRect32.left = 0; gFormatRecord->theRect32.right = w;
		gFormatRecord->theRect32.top = row; gFormatRecord->theRect32.bottom = row + 1;
	}
	else
	{
		gFormatRecord->theRect.left = 0; gFormatRecord->theRect.right = (int16)w;
		gFormatRecord->theRect.top = (int16)row; gFormatRecord->theRect.bottom = (int16)(row + 1);
	}
}

//-------------------------------------------------------------------------------
// Selectors
//-------------------------------------------------------------------------------

static void DoEstimateStart(void)
{
	gFormatRecord->minDataBytes = 1024 * 1024;
	gFormatRecord->maxDataBytes = 4 * 1024 * 1024;
	gFormatRecord->data = NULL;
}

static void DoWritePrepare(void)
{
	gFormatRecord->maxData = 0;
	if (gFormatRecord->hostSupportsPOSIXIO)
		gFormatRecord->pluginUsingPOSIXIO = true;
}

static void DoWriteStart(void)
{
	if (gFormatRecord->imageMode != plugInModeRGBColor || gFormatRecord->depth != 8)
	{
		*gResult = formatBadParameters;
		return;
	}

	int32 W = 0, H = 0;
	GetImageSize(W, H);
	if (W <= 0 || H <= 0) { *gResult = formatBadParameters; return; }

	const int32 tp = gFormatRecord->transparencyPlane;
	const bool hasAlpha = tp > 0 && tp < gFormatRecord->planes;

	Bytes rgba((size_t)W * H * 4, 255);
	Bytes row(W);

	gFormatRecord->colBytes = 1;
	gFormatRecord->rowBytes = W;
	gFormatRecord->planeBytes = 0;
	gFormatRecord->transparencyMatting = 0;
	gFormatRecord->data = row.data();

	const int channels = hasAlpha ? 4 : 3;
	int32 done = 0, total = H * channels;
	for (int c = 0; c < channels && *gResult == noErr; ++c)
	{
		int plane = (c < 3) ? c : tp;
		gFormatRecord->loPlane = gFormatRecord->hiPlane = (int16)plane;
		for (int32 y = 0; y < H && *gResult == noErr; ++y)
		{
			SetRect(W, y);
			*gResult = gFormatRecord->advanceState();
			if (*gResult != noErr) break;
			uint8_t* dst = &rgba[(size_t)y * W * 4 + c];
			for (int32 x = 0; x < W; ++x) dst[(size_t)x * 4] = row[x];
			gFormatRecord->progressProc(++done, total);
		}
	}
	gFormatRecord->data = NULL;
	if (*gResult != noErr) return;

	// Entry sizes: standard sizes up to the (capped) image side, plus the side itself.
	const int side = std::min<int>(256, std::max(W, H));
	std::vector<int> sizes;
	for (int s : {16, 32, 48, 64, 128, 256})
		if (s <= side) sizes.push_back(s);
	if (sizes.empty() || sizes.back() != side) sizes.push_back(side);

	std::vector<Bytes> pngs;
	for (int s : sizes)
	{
		Bytes scaled = Resample(rgba, W, H, s), png;
		if (!EncodePng(scaled, s, png)) { *gResult = memFullErr; return; }
		pngs.push_back(std::move(png));
	}

	Bytes ico = BuildIco(sizes, pngs);

	*gResult = PSSDKSetFPos((int)gFormatRecord->dataFork, gFormatRecord->posixFileDescriptor,
	                        gFormatRecord->pluginUsingPOSIXIO, fsFromStart, 0);
	if (*gResult != noErr) return;

	int32 count = (int32)ico.size();
	*gResult = PSSDKWrite((int)gFormatRecord->dataFork, gFormatRecord->posixFileDescriptor,
	                      gFormatRecord->pluginUsingPOSIXIO, &count, ico.data());
	if (*gResult == noErr && count != (int32)ico.size()) *gResult = dskFulErr;
}

//-------------------------------------------------------------------------------
// Entry point
//-------------------------------------------------------------------------------

DLLExport MACPASCAL void PluginMain(const int16 selector, FormatRecordPtr formatParamBlock,
                                    intptr_t* data, int16* result)
{
	(void)data;
	try
	{
		gResult = result;
		if (selector == formatSelectorAbout)
		{
			AboutRecordPtr about = reinterpret_cast<AboutRecordPtr>(formatParamBlock);
			sSPBasic = about->sSPBasic;
			gPluginRef = reinterpret_cast<SPPluginRef>(about->plugInRef);
			return;
		}

		gFormatRecord = formatParamBlock;
		gPluginRef = reinterpret_cast<SPPluginRef>(gFormatRecord->plugInRef);
		sSPBasic = formatParamBlock->sSPBasic;

		if (gFormatRecord->advanceState == NULL)
		{
			*gResult = errPlugInHostInsufficient;
			return;
		}
		if (gFormatRecord->HostSupports32BitCoordinates)
			gFormatRecord->PluginUsing32BitCoordinates = true;

		switch (selector)
		{
			case formatSelectorEstimatePrepare: gFormatRecord->maxData = 0; break;
			case formatSelectorEstimateStart: DoEstimateStart(); break;
			case formatSelectorWritePrepare: DoWritePrepare(); break;
			case formatSelectorWriteStart: DoWriteStart(); break;
			case formatSelectorReadPrepare:
			case formatSelectorReadStart:
			case formatSelectorFilterFile:
				*gResult = formatCannotRead;
				break;
			default: break;
		}
	}
	catch (...)
	{
		if (result) *result = -1;
	}
}
