#include "vertical-outputs.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QDateTime>
#include <QDir>

namespace {

struct EncoderPair {
	obs_encoder_t *video = nullptr;
	obs_encoder_t *audio = nullptr;
};

void ReleasePair(EncoderPair &p)
{
	if (p.video) {
		obs_encoder_release(p.video);
		p.video = nullptr;
	}
	if (p.audio) {
		obs_encoder_release(p.audio);
		p.audio = nullptr;
	}
}

EncoderPair MakeEncoders(video_t *video, bool forStreaming, QString *encoderName, int *vBitrate, int *aBitrate,
			 QString *error)
{
	EncoderPair pair;
	obs_encoder_t *templateV = nullptr;
	obs_encoder_t *templateA = nullptr;

	obs_output_t *mainOut = forStreaming ? obs_frontend_get_streaming_output() : obs_frontend_get_recording_output();
	if (mainOut) {
		templateV = obs_output_get_video_encoder(mainOut);
		templateA = obs_output_get_audio_encoder(mainOut, 0);
		obs_output_release(mainOut);
	}

	const char *vId = "obs_x264";
	const char *aId = "ffmpeg_aac";
	OBSDataAutoRelease vSettings = obs_data_create();
	OBSDataAutoRelease aSettings = obs_data_create();

	if (templateV) {
		const char *id = obs_encoder_get_id(templateV);
		if (id && *id)
			vId = id;
		OBSDataAutoRelease src = obs_encoder_get_settings(templateV);
		if (src)
			obs_data_apply(vSettings, src);
	} else {
		obs_data_set_string(vSettings, "rate_control", "CBR");
		obs_data_set_int(vSettings, "bitrate", forStreaming ? 4500 : 6000);
		obs_data_set_string(vSettings, "preset", "veryfast");
		obs_data_set_string(vSettings, "profile", "high");
		obs_data_set_int(vSettings, "keyint_sec", 2);
	}

	if (templateA) {
		const char *id = obs_encoder_get_id(templateA);
		if (id && *id)
			aId = id;
		OBSDataAutoRelease src = obs_encoder_get_settings(templateA);
		if (src)
			obs_data_apply(aSettings, src);
	} else {
		obs_data_set_int(aSettings, "bitrate", 160);
	}

	if (encoderName)
		*encoderName = QString::fromUtf8(vId);
	if (vBitrate)
		*vBitrate = (int)obs_data_get_int(vSettings, "bitrate");
	if (aBitrate)
		*aBitrate = (int)obs_data_get_int(aSettings, "bitrate");

	pair.video = obs_video_encoder_create(vId, "vertical_shorts_video", vSettings, nullptr);
	if (!pair.video) {
		vId = "obs_x264";
		if (encoderName)
			*encoderName = QStringLiteral("obs_x264");
		pair.video = obs_video_encoder_create(vId, "vertical_shorts_video", vSettings, nullptr);
	}
	if (!pair.video) {
		if (error)
			*error = QStringLiteral("Could not create a video encoder for the vertical canvas.");
		return pair;
	}

	pair.audio = obs_audio_encoder_create(aId, "vertical_shorts_audio", aSettings, 0, nullptr);
	if (!pair.audio)
		pair.audio = obs_audio_encoder_create("ffmpeg_aac", "vertical_shorts_audio", aSettings, 0, nullptr);
	if (!pair.audio) {
		ReleasePair(pair);
		if (error)
			*error = QStringLiteral("Could not create an audio encoder for the vertical canvas.");
		return pair;
	}

	obs_encoder_set_video(pair.video, video);
	obs_encoder_set_audio(pair.audio, obs_get_audio());
	return pair;
}

} // namespace

VerticalOutputs::VerticalOutputs(QObject *parent) : QObject(parent) {}

VerticalOutputs::~VerticalOutputs()
{
	StopAll();
}

void VerticalOutputs::SetVideo(video_t *v)
{
	video = v;
}

void VerticalOutputs::ApplySettings(const vsp::PluginSettings &s)
{
	settings = s;
	if (replayOutput && obs_output_active(replayOutput)) {
		OBSDataAutoRelease data = obs_data_create();
		obs_data_set_string(data, "directory", ResolveRecordingDirectory().toUtf8().constData());
		obs_data_set_int(data, "max_time_sec", vsp::EffectiveClipSeconds(settings));
		obs_data_set_int(data, "max_size_mb", 0);
		obs_output_update(replayOutput, data);
	}
}

QString VerticalOutputs::ResolveRecordingDirectory() const
{
	QString path = settings.recordingPath.trimmed();
	if (path.isEmpty())
		path = vsp::DefaultRecordingPath();
	if (path.isEmpty())
		path = QDir::homePath();
	QDir().mkpath(path);
	return path;
}

QString VerticalOutputs::MakeRecordingFilename() const
{
	const QString dir = ResolveRecordingDirectory();
	const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
	return QDir(dir).filePath(QStringLiteral("VerticalShorts_%1.mp4").arg(stamp));
}

QString VerticalOutputs::ActiveEncoderSummary() const
{
	if (lastEncoderName.isEmpty())
		return QStringLiteral("(inherits OBS when output starts)");
	return lastEncoderName;
}

QString VerticalOutputs::ActiveBitrateSummary() const
{
	if (lastVideoBitrate <= 0)
		return QStringLiteral("(inherits OBS when output starts)");
	return QStringLiteral("%1 kbps video / %2 kbps audio").arg(lastVideoBitrate).arg(lastAudioBitrate);
}

bool VerticalOutputs::StartStreaming(QString *error)
{
	if (!video) {
		if (error)
			*error = QStringLiteral("Vertical video pipeline is not ready.");
		return false;
	}
	if (IsStreaming())
		return true;

	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service) {
		if (error)
			*error = QStringLiteral("No streaming service is configured in OBS Settings → Stream.");
		return false;
	}

	EncoderPair pair = MakeEncoders(video, true, &lastEncoderName, &lastVideoBitrate, &lastAudioBitrate, error);
	if (!pair.video || !pair.audio) {
		obs_service_release(service);
		ReleasePair(pair);
		return false;
	}

	if (!streamOutput) {
		streamOutput = obs_output_create("rtmp_output", "vertical_shorts_stream", nullptr, nullptr);
		if (!streamOutput)
			streamOutput = obs_output_create("ffmpeg_mpegts_muxer", "vertical_shorts_stream", nullptr, nullptr);
		if (streamOutput) {
			signal_handler_t *sh = obs_output_get_signal_handler(streamOutput);
			signal_handler_connect(sh, "stop", OnStreamStop, this);
		}
	}
	if (!streamOutput) {
		ReleasePair(pair);
		obs_service_release(service);
		if (error)
			*error = QStringLiteral("Could not create a vertical streaming output.");
		return false;
	}

	obs_output_set_service(streamOutput, service);
	obs_service_release(service);
	obs_output_set_media(streamOutput, video, obs_get_audio());
	obs_output_set_video_encoder(streamOutput, pair.video);
	obs_output_set_audio_encoder(streamOutput, pair.audio, 0);
	ReleasePair(pair); /* output holds refs */

	if (!obs_output_start(streamOutput)) {
		const char *err = obs_output_get_last_error(streamOutput);
		if (error)
			*error = err && *err
					 ? QString::fromUtf8(err)
					 : QStringLiteral(
						   "Failed to start vertical stream. The service may already be in use by the main OBS stream.");
		return false;
	}

	emit streamingChanged(true);
	blog(LOG_INFO, "[obs-shorts-vertical] Vertical stream started");
	return true;
}

void VerticalOutputs::StopStreaming()
{
	if (streamOutput && obs_output_active(streamOutput))
		obs_output_stop(streamOutput);
	emit streamingChanged(false);
}

bool VerticalOutputs::IsStreaming() const
{
	return streamOutput && obs_output_active(streamOutput);
}

bool VerticalOutputs::StartRecording(QString *error)
{
	if (!video) {
		if (error)
			*error = QStringLiteral("Vertical video pipeline is not ready.");
		return false;
	}
	if (IsRecording())
		return true;

	EncoderPair pair = MakeEncoders(video, false, &lastEncoderName, &lastVideoBitrate, &lastAudioBitrate, error);
	if (!pair.video || !pair.audio) {
		ReleasePair(pair);
		return false;
	}

	if (!recordOutput) {
		recordOutput = obs_output_create("ffmpeg_muxer", "vertical_shorts_record", nullptr, nullptr);
		if (!recordOutput)
			recordOutput = obs_output_create("ffmpeg_output", "vertical_shorts_record", nullptr, nullptr);
		if (recordOutput) {
			signal_handler_t *sh = obs_output_get_signal_handler(recordOutput);
			signal_handler_connect(sh, "stop", OnRecordStop, this);
		}
	}
	if (!recordOutput) {
		ReleasePair(pair);
		if (error)
			*error = QStringLiteral("Could not create a vertical recording output.");
		return false;
	}

	const QString path = MakeRecordingFilename();
	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_string(data, "path", path.toUtf8().constData());
	obs_data_set_string(data, "muxer_settings", "");
	obs_output_update(recordOutput, data);

	obs_output_set_media(recordOutput, video, obs_get_audio());
	obs_output_set_video_encoder(recordOutput, pair.video);
	obs_output_set_audio_encoder(recordOutput, pair.audio, 0);
	ReleasePair(pair);

	if (!obs_output_start(recordOutput)) {
		const char *err = obs_output_get_last_error(recordOutput);
		if (error)
			*error = err && *err ? QString::fromUtf8(err) : QStringLiteral("Failed to start vertical recording.");
		return false;
	}

	emit recordingChanged(true);
	blog(LOG_INFO, "[obs-shorts-vertical] Vertical recording started: %s", path.toUtf8().constData());
	return true;
}

void VerticalOutputs::StopRecording()
{
	if (recordOutput && obs_output_active(recordOutput))
		obs_output_stop(recordOutput);
	emit recordingChanged(false);
}

bool VerticalOutputs::IsRecording() const
{
	return recordOutput && obs_output_active(recordOutput);
}

bool VerticalOutputs::EnsureClipBuffer(QString *error)
{
	if (!settings.clipBufferEnabled) {
		if (error)
			*error = QStringLiteral("Clip buffer is disabled in Settings.");
		return false;
	}
	if (!video) {
		if (error)
			*error = QStringLiteral("Vertical video pipeline is not ready.");
		return false;
	}
	if (IsClipBufferActive())
		return true;

	EncoderPair pair = MakeEncoders(video, false, &lastEncoderName, &lastVideoBitrate, &lastAudioBitrate, error);
	if (!pair.video || !pair.audio) {
		ReleasePair(pair);
		return false;
	}

	if (!replayOutput) {
		replayOutput = obs_output_create("replay_buffer", "vertical_shorts_clip", nullptr, nullptr);
		if (replayOutput) {
			signal_handler_t *sh = obs_output_get_signal_handler(replayOutput);
			signal_handler_connect(sh, "saved", OnReplaySaved, this);
		}
	}
	if (!replayOutput) {
		ReleasePair(pair);
		if (error)
			*error = QStringLiteral("Could not create a vertical clip buffer (replay_buffer unavailable).");
		return false;
	}

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_string(data, "directory", ResolveRecordingDirectory().toUtf8().constData());
	obs_data_set_string(data, "format", "%CCYY-%MM-%DD %hh-%mm-%ss");
	obs_data_set_string(data, "extension", "mp4");
	obs_data_set_bool(data, "allow_spaces", true);
	obs_data_set_int(data, "max_time_sec", vsp::EffectiveClipSeconds(settings));
	obs_data_set_int(data, "max_size_mb", 0);
	obs_output_update(replayOutput, data);

	obs_output_set_media(replayOutput, video, obs_get_audio());
	obs_output_set_video_encoder(replayOutput, pair.video);
	obs_output_set_audio_encoder(replayOutput, pair.audio, 0);
	ReleasePair(pair);

	if (!obs_output_start(replayOutput)) {
		const char *err = obs_output_get_last_error(replayOutput);
		if (error)
			*error = err && *err ? QString::fromUtf8(err)
					     : QStringLiteral("Failed to start the vertical clip buffer.");
		return false;
	}

	emit clipBufferChanged(true);
	blog(LOG_INFO, "[obs-shorts-vertical] Vertical clip buffer started (%d sec)",
	     vsp::EffectiveClipSeconds(settings));
	return true;
}

void VerticalOutputs::StopClipBuffer()
{
	if (replayOutput && obs_output_active(replayOutput))
		obs_output_stop(replayOutput);
	emit clipBufferChanged(false);
}

bool VerticalOutputs::IsClipBufferActive() const
{
	return replayOutput && obs_output_active(replayOutput);
}

bool VerticalOutputs::SaveClip(QString *savedPath, QString *error)
{
	if (!IsClipBufferActive()) {
		if (!EnsureClipBuffer(error))
			return false;
		if (error)
			*error = QStringLiteral(
				"Clip buffer just started — wait a few seconds for it to fill, then try again.");
		return false;
	}

	proc_handler_t *ph = obs_output_get_proc_handler(replayOutput);
	if (!ph) {
		if (error)
			*error = QStringLiteral("Clip buffer does not support save.");
		return false;
	}

	calldata_t cd;
	calldata_init(&cd);
	const bool ok = proc_handler_call(ph, "save", &cd);
	calldata_free(&cd);
	if (!ok) {
		if (error)
			*error = QStringLiteral("Could not save clip — buffer may still be filling.");
		return false;
	}

	calldata_t cd2;
	calldata_init(&cd2);
	if (proc_handler_call(ph, "get_last_replay", &cd2)) {
		const char *path = calldata_string(&cd2, "path");
		if (path && *path) {
			lastClipPath = QString::fromUtf8(path);
			if (savedPath)
				*savedPath = lastClipPath;
		}
	}
	calldata_free(&cd2);
	return true;
}

void VerticalOutputs::StopAll()
{
	StopStreaming();
	StopRecording();
	StopClipBuffer();

	if (streamOutput) {
		obs_output_release(streamOutput);
		streamOutput = nullptr;
	}
	if (recordOutput) {
		obs_output_release(recordOutput);
		recordOutput = nullptr;
	}
	if (replayOutput) {
		obs_output_release(replayOutput);
		replayOutput = nullptr;
	}
}

void VerticalOutputs::OnStreamStop(void *data, calldata_t *)
{
	auto *self = static_cast<VerticalOutputs *>(data);
	QMetaObject::invokeMethod(self, [self]() { emit self->streamingChanged(false); }, Qt::QueuedConnection);
}

void VerticalOutputs::OnRecordStop(void *data, calldata_t *)
{
	auto *self = static_cast<VerticalOutputs *>(data);
	QMetaObject::invokeMethod(self, [self]() { emit self->recordingChanged(false); }, Qt::QueuedConnection);
}

void VerticalOutputs::OnReplaySaved(void *data, calldata_t *cd)
{
	auto *self = static_cast<VerticalOutputs *>(data);
	const char *path = calldata_string(cd, "path");
	QString qpath = path ? QString::fromUtf8(path) : QString();
	QMetaObject::invokeMethod(
		self,
		[self, qpath]() {
			if (!qpath.isEmpty()) {
				self->lastClipPath = qpath;
				emit self->clipSaved(qpath);
			}
		},
		Qt::QueuedConnection);
}
