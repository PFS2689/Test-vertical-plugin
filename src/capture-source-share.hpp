#pragma once

#include <obs.hpp>

#include <string>
#include <vector>

/* Capture device helpers: identify Video Capture Device sources by stable
 * source IDs / device keys. Explicit Share Existing Camera may reuse a main
 * OBS capture; Create New always builds an independent Vertical Shorts source. */

namespace vsp {

struct CaptureSourceInfo {
	OBSSource source;
	std::string typeId;
	std::string deviceKey;
	std::string displayName;
	uint32_t width = 0;
	uint32_t height = 0;
	bool active = false;
	bool showing = false;
};

bool IsVideoCaptureSourceId(const char *id);

/* Resolve the create-time source id (latest versioned) for an unversioned or versioned id. */
std::string ResolveLatestInputTypeId(const char *idOrUnversioned);

/* Enumerate registered input types, log id/name/flags, and return the best Video Capture Device id
 * for this OBS build (Windows: dshow_input). Empty if none found. */
std::string ResolveVideoCaptureSourceId();

/* Unversioned family: "dshow_input", "av_capture_input", "v4l2_input", … */
std::string CaptureSourceFamily(const char *id);

/* Stable device key: "family|device_id". Empty if unknown / unset. */
std::string GetCaptureDeviceKey(obs_source_t *source);
std::string GetCaptureDeviceKeyFromSettings(const char *typeId, obs_data_t *settings);

/* Human-readable device name from source settings (e.g. DirectShow video_device). */
std::string GetCaptureDeviceDisplayName(obs_source_t *source);

/* Strong-ref to an existing capture source with the same device key, excluding `exclude`. */
obs_source_t *FindExistingCaptureByDeviceKey(const std::string &deviceKey, obs_source_t *exclude = nullptr);

/* All Video Capture Device sources currently registered in OBS. */
std::vector<CaptureSourceInfo> EnumerateCaptureSources();

/* True if this source already has a scene item in the given vertical scene. */
bool VerticalSceneHasSource(obs_scene_t *scene, obs_source_t *source);

} // namespace vsp
