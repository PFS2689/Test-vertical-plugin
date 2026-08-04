#include "plugin-settings.hpp"

#include <cassert>
#include <iostream>

static int failures = 0;

static void expect(bool cond, const char *msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << "\n";
		++failures;
	} else {
		std::cout << "ok: " << msg << "\n";
	}
}

int main()
{
	QString err;
	expect(vsp::ValidateCanvasSize(1080, 1920, &err), "1080x1920 valid");
	expect(!vsp::ValidateCanvasSize(0, 1920, &err), "zero width invalid");
	expect(!vsp::ValidateCanvasSize(1080, 100, &err), "too small height invalid");
	expect(!vsp::ValidateCanvasSize(9000, 9000, &err), "too large invalid");

	expect(vsp::ValidateClipSeconds(30, &err), "30s clip valid");
	expect(!vsp::ValidateClipSeconds(0, &err), "0s clip invalid");
	expect(!vsp::ValidateClipSeconds(-5, &err), "negative clip invalid");
	expect(!vsp::ValidateClipSeconds(601, &err), "too long clip invalid");

	expect(vsp::IsPortrait(1080, 1920), "portrait");
	expect(!vsp::IsPortrait(1920, 1080), "landscape not portrait");

	uint32_t w = 0, h = 0;
	vsp::CanvasSizeForPreset(vsp::CanvasPreset::YouTubeVertical, 1, 1, w, h);
	expect(w == 1080 && h == 1920, "youtube preset size");
	vsp::CanvasSizeForPreset(vsp::CanvasPreset::Custom, 720, 1280, w, h);
	expect(w == 720 && h == 1280, "custom preset size");

	vsp::PluginSettings s;
	s.clipPreset = vsp::ClipLengthPreset::Sec60;
	expect(vsp::EffectiveClipSeconds(s) == 60, "preset clip seconds");
	s.clipPreset = vsp::ClipLengthPreset::Custom;
	s.customClipSeconds = 42;
	expect(vsp::EffectiveClipSeconds(s) == 42, "custom clip seconds");

	if (failures) {
		std::cerr << failures << " test(s) failed\n";
		return 1;
	}
	std::cout << "all settings tests passed\n";
	return 0;
}
