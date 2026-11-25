#include "core/graphics/picture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "lodepng.h"

// lunasvg (src/3rdparty/lunasvg, a submodule taken as it is released; its plutovg draws) — the one place SVG is read
// and drawn. What a release of it learns to read, the core draws with nothing changed here (Max, 2026-11-25: "whoever
// keeps the SVG format up, we pull their updates").
#include <lunasvg.h>

namespace {

// A PICTURE'S PIXELS — RGBA, 8 bits a channel, rows top down, as LodePNG reads a PNG and writes one and a drawing is
// drawn into.
struct ibPixels {
	std::vector<unsigned char> m_rgba;
	unsigned m_width = 0;
	unsigned m_height = 0;
};

}

// THE BYTES ARE A DRAWING — an SVG's text: the first that is not a space is `<`. A PNG begins with its signature, which
// is not; bytes that are neither are no picture.
static bool IsDrawing(const ibMemoryBuffer& data)
{
	const char* const bytes = static_cast<const char*>(data.GetData());
	for (size_t at = 0; at < data.GetDataLen(); ++at) {
		if (bytes[at] != ' ' && bytes[at] != '\t' && bytes[at] != '\r' && bytes[at] != '\n')
			return bytes[at] == '<';
	}
	return false;
}

// …and its text.
static ibString DrawingOf(const ibMemoryBuffer& data)
{
	return ibString::FromUTF8(static_cast<const char*>(data.GetData()), data.GetDataLen());
}

static bool DecodePixels(const ibMemoryBuffer& png, ibPixels& pixels)
{
	return !png.IsEmpty() && !IsDrawing(png) && lodepng::decode(pixels.m_rgba, pixels.m_width, pixels.m_height,
		static_cast<const unsigned char*>(png.GetData()), png.GetDataLen()) == 0;
}

static ibPicture EncodePixels(const ibPixels& pixels)
{
	std::vector<unsigned char> png;
	if (lodepng::encode(png, pixels.m_rgba, pixels.m_width, pixels.m_height) != 0)
		return ibPicture();
	return ibPicture(ibMemoryBuffer(png.data(), png.size()));
}

// EVERY PIXEL OF THE SIZE ASKED IS THE AVERAGE OF THE AREA IT COVERS — each source pixel weighted by how much of it lies
// inside, and its colour by its alpha as well, so a transparent pixel's colour does not bleed into its neighbours'.
static ibPixels ResampleArea(const ibPixels& source, unsigned width, unsigned height)
{
	ibPixels target;
	target.m_rgba.resize(size_t(width) * height * 4);
	target.m_width = width;
	target.m_height = height;
	const double scaleX = double(source.m_width) / width;
	const double scaleY = double(source.m_height) / height;
	for (unsigned y = 0; y < height; ++y) {
		const double top = y * scaleY, bottom = top + scaleY;
		for (unsigned x = 0; x < width; ++x) {
			const double left = x * scaleX, right = left + scaleX;
			double red = 0, green = 0, blue = 0, alpha = 0, area = 0;
			for (unsigned sy = unsigned(top); sy < source.m_height && sy < bottom; ++sy) {
				const double coverY = std::min(bottom, sy + 1.0) - std::max(top, double(sy));
				for (unsigned sx = unsigned(left); sx < source.m_width && sx < right; ++sx) {
					const double cover = coverY * (std::min(right, sx + 1.0) - std::max(left, double(sx)));
					const unsigned char* const pixel = &source.m_rgba[(size_t(sy) * source.m_width + sx) * 4];
					const double weight = cover * pixel[3];
					red += weight * pixel[0];
					green += weight * pixel[1];
					blue += weight * pixel[2];
					alpha += weight;
					area += cover;
				}
			}
			unsigned char* const pixel = &target.m_rgba[(size_t(y) * width + x) * 4];
			if (alpha > 0) {
				pixel[0] = (unsigned char)std::lround(red / alpha);
				pixel[1] = (unsigned char)std::lround(green / alpha);
				pixel[2] = (unsigned char)std::lround(blue / alpha);
			}
			pixel[3] = area > 0 ? (unsigned char)std::lround(alpha / area) : 0;
		}
	}
	return target;
}

ibPicture ibPicture::FromSvg(const ibString& svg)
{
	const std::string text = svg.ToUtf8();
	const ibMemoryBuffer data(text.data(), text.size());
	return IsDrawing(data) ? ibPicture(data) : ibPicture();
}

ibPicture ibPicture::FromSvg(const ibString& svg, const ibSize& size)
{
	if (size.m_width <= 0 || size.m_height <= 0)
		return ibPicture();

	const std::unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(svg.ToUtf8());

	// A text that is not SVG reads as nothing, or as an image of no size.
	if (document == nullptr || document->width() <= 0 || document->height() <= 0)
		return ibPicture();

	// Fitted whole from the top left corner, one scale both ways: the size asked holds the drawing, never stretches it.
	lunasvg::Bitmap bitmap(size.m_width, size.m_height);
	if (bitmap.isNull())
		return ibPicture();
	const float scale = std::min(size.m_width / document->width(), size.m_height / document->height());
	document->render(bitmap, lunasvg::Matrix(scale, 0, 0, scale, 0, 0));
	bitmap.convertToRGBA();   // its premultiplied ARGB words, as RGBA bytes

	ibPixels pixels;
	pixels.m_width = unsigned(size.m_width);
	pixels.m_height = unsigned(size.m_height);
	pixels.m_rgba.resize(size_t(pixels.m_width) * pixels.m_height * 4);
	for (unsigned row = 0; row < pixels.m_height; ++row)
		std::memcpy(&pixels.m_rgba[size_t(row) * pixels.m_width * 4], bitmap.data() + size_t(row) * bitmap.stride(),
			size_t(pixels.m_width) * 4);

	// 🛑 NOTHING DRAWN IS NO PICTURE, not a transparent square: the reader passes over what it does not know — filters,
	// animation — without a word, and a picture that shows nothing would be taken as set.
	bool visible = false;
	for (size_t at = 3; at < pixels.m_rgba.size() && !visible; at += 4)
		visible = pixels.m_rgba[at] != 0;
	return visible ? EncodePixels(pixels) : ibPicture();
}

ibSize ibPicture::GetSize() const
{
	if (IsDrawing(m_data)) {
		const std::unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData(
			static_cast<const char*>(m_data.GetData()), m_data.GetDataLen());
		return document != nullptr ? ibSize(int(std::lround(document->width())), int(std::lround(document->height())))
			: ibSize();
	}
	unsigned width = 0, height = 0;
	lodepng::State state;
	if (IsOk() && lodepng_inspect(&width, &height, &state, static_cast<const unsigned char*>(m_data.GetData()), m_data.GetDataLen()) == 0)
		return ibSize(int(width), int(height));
	return ibSize();
}

ibPicture ibPicture::Scale(const ibSize& size) const
{
	if (size.m_width <= 0 || size.m_height <= 0)
		return ibPicture();
	if (IsDrawing(m_data))
		return FromSvg(DrawingOf(m_data), size);
	if (GetSize() == size)
		return *this;

	ibPixels pixels;
	if (!DecodePixels(m_data, pixels))
		return ibPicture();
	return EncodePixels(ResampleArea(pixels, unsigned(size.m_width), unsigned(size.m_height)));
}

ibString ibPicture::ToSvg(size_t mostRects) const
{
	if (IsDrawing(m_data))
		return DrawingOf(m_data);

	ibPixels pixels;
	if (!DecodePixels(m_data, pixels))
		return ibString();
	const int width = int(pixels.m_width), height = int(pixels.m_height);

	// Alpha in the top byte, so a colour and its transparency are one key.
	const auto colourAt = [&pixels](int at) -> std::uint32_t {
		const unsigned char* const pixel = &pixels.m_rgba[size_t(at) * 4];
		return ((std::uint32_t)pixel[3] << 24) | ((std::uint32_t)pixel[0] << 16)
			| ((std::uint32_t)pixel[1] << 8) | (std::uint32_t)pixel[2];
	};

	struct Rect { int x, y, w, h; std::uint32_t colour; };
	std::vector<Rect> done, open;   // `open` reach the row above, sorted by x

	for (int y = 0; y < height; ++y) {

		std::vector<Rect> next;
		size_t above = 0;

		for (int x = 0; x < width; ) {

			const std::uint32_t colour = colourAt(y * width + x);
			int end = x + 1;
			while (end < width && colourAt(y * width + end) == colour)
				++end;

			if ((colour >> 24) != 0) {   // fully transparent is simply not drawn

				while (above < open.size() && open[above].x < x)
					done.push_back(open[above++]);

				if (above < open.size() && open[above].x == x && open[above].w == end - x
					&& open[above].colour == colour) {
					Rect grown = open[above++];
					grown.h++;
					next.push_back(grown);
				}
				else {
					next.push_back({ x, y, end - x, 1, colour });
				}
			}

			x = end;
		}

		while (above < open.size())
			done.push_back(open[above++]);
		open.swap(next);

		if (done.size() + open.size() > mostRects)
			return ibString();
	}

	done.insert(done.end(), open.begin(), open.end());

	// ONE PATH PER COLOUR, in the order the colours are first met.
	std::vector<std::uint32_t> order;
	std::unordered_map<std::uint32_t, ibString> paths;

	for (const Rect& rect : done) {
		const auto [found, inserted] = paths.try_emplace(rect.colour);
		if (inserted)
			order.push_back(rect.colour);
		found->second << ibString::Format(ibT("M%d %dh%dv%dh-%dz"), rect.x, rect.y, rect.w, rect.h, rect.w);
	}

	ibString svg = ibString::Format(ibT("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" "
		"viewBox=\"0 0 %d %d\">\n"), width, height, width, height);

	for (const std::uint32_t colour : order) {
		svg << ibString::Format(ibT("<path fill=\"#%06x\""), (unsigned int)(colour & 0xFFFFFF));

		// In thousandths, so the point is a point whatever the locale says.
		const unsigned int opacity = colour >> 24;
		if (opacity != 255)
			svg << ibString::Format(ibT(" fill-opacity=\"0.%03u\""), (opacity * 1000 + 127) / 255);

		svg << ibT(" d=\"") << paths[colour] << ibT("\"/>\n");
	}

	svg << ibT("</svg>");
	return svg;
}
