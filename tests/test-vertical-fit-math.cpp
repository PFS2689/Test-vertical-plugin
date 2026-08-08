/* Standalone math check for Fit / Fill scale + center on vertical canvas.
 * Build: g++ -std=c++17 -o test-vertical-fit-math tests/test-vertical-fit-math.cpp && ./test-vertical-fit-math
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>

struct FitResult {
	float scale;
	float posX;
	float posY;
	float scaledW;
	float scaledH;
};

static FitResult ComputeFit(float srcW, float srcH, float canvasW, float canvasH, bool fill)
{
	const float sx = canvasW / srcW;
	const float sy = canvasH / srcH;
	FitResult r{};
	r.scale = fill ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
	r.scaledW = srcW * r.scale;
	r.scaledH = srcH * r.scale;
	r.posX = (canvasW - r.scaledW) * 0.5f;
	r.posY = (canvasH - r.scaledH) * 0.5f;
	return r;
}

static bool Near(float a, float b, float eps = 0.01f)
{
	return std::fabs(a - b) <= eps;
}

int main()
{
	/* 1920x1080 camera into 1080x1920 vertical canvas */
	const FitResult fit = ComputeFit(1920, 1080, 1080, 1920, false);
	const FitResult fill = ComputeFit(1920, 1080, 1080, 1920, true);

	std::printf("FIT  scale=%.6f scaled=%.1fx%.1f pos=(%.1f,%.1f)\n", fit.scale, fit.scaledW, fit.scaledH,
		    fit.posX, fit.posY);
	std::printf("FILL scale=%.6f scaled=%.1fx%.1f pos=(%.1f,%.1f)\n", fill.scale, fill.scaledW, fill.scaledH,
		    fill.posX, fill.posY);

	/* Fit: scale = min(1080/1920, 1920/1080) = min(0.5625, 1.777...) = 0.5625
	 * scaled = 1080 x 607.5, centered X=0, Y=(1920-607.5)/2 = 656.25 */
	if (!Near(fit.scale, 0.5625f) || !Near(fit.scaledW, 1080.f) || !Near(fit.scaledH, 607.5f) ||
	    !Near(fit.posX, 0.f) || !Near(fit.posY, 656.25f)) {
		std::fprintf(stderr, "FIT math failed\n");
		return 1;
	}

	/* Fill: scale = max(0.5625, 1.777...) = 1.777...
	 * scaled = 3413.33 x 1920, centered X=(1080-3413.33)/2 = -1166.67, Y=0 */
	if (!Near(fill.scale, 1920.f / 1080.f) || !Near(fill.scaledH, 1920.f) || !Near(fill.posY, 0.f) ||
	    fill.posX >= 0.f) {
		std::fprintf(stderr, "FILL math failed\n");
		return 1;
	}

	/* Entire fit image inside canvas */
	if (fit.posX < -0.01f || fit.posY < -0.01f || fit.posX + fit.scaledW > 1080.01f ||
	    fit.posY + fit.scaledH > 1920.01f) {
		std::fprintf(stderr, "FIT not contained in canvas\n");
		return 1;
	}

	/* Fill covers canvas */
	if (fill.posX > 0.01f || fill.posY > 0.01f || fill.posX + fill.scaledW < 1079.99f ||
	    fill.posY + fill.scaledH < 1919.99f) {
		std::fprintf(stderr, "FILL does not cover canvas\n");
		return 1;
	}

	std::printf("OK\n");
	return 0;
}
