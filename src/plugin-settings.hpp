#pragma once

#include <QString>
#include <cstdint>

#ifdef VSP_SETTINGS_TEST
/* Test build: avoid linking OBS */
#else
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/config-file.h>
#endif

namespace vsp {

enum class WorkspaceLayout { Vertical = 0, Horizontal = 1 };

enum class CanvasPreset {
	YouTubeVertical = 0,
	TikTokVertical = 1,
	TwitchVertical = 2,
	InstagramVertical = 3,
	Custom = 4,
};

enum class ClipLengthPreset { Sec10 = 10, Sec20 = 20, Sec30 = 30, Sec60 = 60, Custom = 0 };

struct PluginSettings {
	WorkspaceLayout layout = WorkspaceLayout::Vertical;
	CanvasPreset canvasPreset = CanvasPreset::YouTubeVertical;
	uint32_t customWidth = 1080;
	uint32_t customHeight = 1920;
	ClipLengthPreset clipPreset = ClipLengthPreset::Sec30;
	int customClipSeconds = 45;
	QString recordingPath; /* empty => use OBS default when available */
	bool clipBufferEnabled = true;
};

inline void CanvasSizeForPreset(CanvasPreset preset, uint32_t customW, uint32_t customH, uint32_t &outW,
				uint32_t &outH)
{
	switch (preset) {
	case CanvasPreset::YouTubeVertical:
	case CanvasPreset::TikTokVertical:
	case CanvasPreset::TwitchVertical:
	case CanvasPreset::InstagramVertical:
		outW = 1080;
		outH = 1920;
		break;
	case CanvasPreset::Custom:
	default:
		outW = customW;
		outH = customH;
		break;
	}
}

inline int EffectiveClipSeconds(const PluginSettings &s)
{
	if (s.clipPreset == ClipLengthPreset::Custom)
		return s.customClipSeconds;
	return static_cast<int>(s.clipPreset);
}

inline bool ValidateCanvasSize(int w, int h, QString *error)
{
	if (w < 160 || h < 160) {
		if (error)
			*error = QStringLiteral("Minimum resolution is 160×160.");
		return false;
	}
	if (w > 7680 || h > 7680) {
		if (error)
			*error = QStringLiteral("Maximum resolution is 7680×7680.");
		return false;
	}
	return true;
}

inline bool ValidateClipSeconds(int seconds, QString *error)
{
	if (seconds < 1) {
		if (error)
			*error = QStringLiteral("Clip length must be at least 1 second.");
		return false;
	}
	if (seconds > 600) {
		if (error)
			*error = QStringLiteral("Clip length cannot exceed 600 seconds.");
		return false;
	}
	return true;
}

inline bool IsPortrait(uint32_t w, uint32_t h)
{
	return h > w;
}

inline QString DefaultRecordingPath()
{
#ifdef VSP_SETTINGS_TEST
	return {};
#else
	char *path = obs_frontend_get_current_record_output_path();
	QString result;
	if (path) {
		result = QString::fromUtf8(path);
		bfree(path);
	}
	if (result.isEmpty()) {
		config_t *cfg = obs_frontend_get_profile_config();
		if (cfg) {
			const char *simple = config_get_string(cfg, "SimpleOutput", "FilePath");
			if (simple && *simple)
				result = QString::fromUtf8(simple);
			if (result.isEmpty()) {
				const char *adv = config_get_string(cfg, "AdvOut", "RecFilePath");
				if (adv && *adv)
					result = QString::fromUtf8(adv);
			}
		}
	}
	return result;
#endif
}

#ifdef VSP_SETTINGS_TEST
inline void SaveSettingsToData(void *, const PluginSettings &, uint32_t, uint32_t) {}
inline PluginSettings LoadSettingsFromData(void *, uint32_t &canvasW, uint32_t &canvasH)
{
	PluginSettings s;
	canvasW = 1080;
	canvasH = 1920;
	return s;
}
#else
inline void SaveSettingsToData(obs_data_t *data, const PluginSettings &s, uint32_t canvasW, uint32_t canvasH)
{
	obs_data_set_int(data, "workspace_layout", static_cast<int>(s.layout));
	obs_data_set_int(data, "canvas_preset", static_cast<int>(s.canvasPreset));
	obs_data_set_int(data, "custom_width", s.customWidth);
	obs_data_set_int(data, "custom_height", s.customHeight);
	obs_data_set_int(data, "clip_preset", static_cast<int>(s.clipPreset));
	obs_data_set_int(data, "custom_clip_seconds", s.customClipSeconds);
	obs_data_set_string(data, "recording_path", s.recordingPath.toUtf8().constData());
	obs_data_set_bool(data, "clip_buffer_enabled", s.clipBufferEnabled);
	obs_data_set_int(data, "width", canvasW);
	obs_data_set_int(data, "height", canvasH);
}

inline PluginSettings LoadSettingsFromData(obs_data_t *data, uint32_t &canvasW, uint32_t &canvasH)
{
	PluginSettings s;
	s.layout = static_cast<WorkspaceLayout>(obs_data_get_int(data, "workspace_layout"));
	s.canvasPreset = static_cast<CanvasPreset>(obs_data_get_int(data, "canvas_preset"));
	s.customWidth = (uint32_t)obs_data_get_int(data, "custom_width");
	s.customHeight = (uint32_t)obs_data_get_int(data, "custom_height");
	s.clipPreset = static_cast<ClipLengthPreset>(obs_data_get_int(data, "clip_preset"));
	s.customClipSeconds = (int)obs_data_get_int(data, "custom_clip_seconds");
	s.recordingPath = QString::fromUtf8(obs_data_get_string(data, "recording_path"));
	s.clipBufferEnabled = obs_data_has_user_value(data, "clip_buffer_enabled")
				      ? obs_data_get_bool(data, "clip_buffer_enabled")
				      : true;

	if (s.customWidth == 0)
		s.customWidth = 1080;
	if (s.customHeight == 0)
		s.customHeight = 1920;
	if (s.customClipSeconds <= 0)
		s.customClipSeconds = 45;
	if (s.clipPreset != ClipLengthPreset::Custom && s.clipPreset != ClipLengthPreset::Sec10 &&
	    s.clipPreset != ClipLengthPreset::Sec20 && s.clipPreset != ClipLengthPreset::Sec30 &&
	    s.clipPreset != ClipLengthPreset::Sec60) {
		s.clipPreset = ClipLengthPreset::Sec30;
	}
	if (s.layout != WorkspaceLayout::Horizontal)
		s.layout = WorkspaceLayout::Vertical;

	canvasW = (uint32_t)obs_data_get_int(data, "width");
	canvasH = (uint32_t)obs_data_get_int(data, "height");
	if (canvasW == 0 || canvasH == 0)
		CanvasSizeForPreset(s.canvasPreset, s.customWidth, s.customHeight, canvasW, canvasH);

	if (s.recordingPath.isEmpty())
		s.recordingPath = DefaultRecordingPath();

	return s;
}
#endif

} // namespace vsp
