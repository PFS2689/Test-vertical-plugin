#pragma once

#include "plugin-settings.hpp"

#include <obs.hpp>

#include <QDateTime>
#include <QObject>
#include <QString>

enum class ClipKind { Short = 0, Long = 1 };

enum class ClipSaveResult {
	Ok = 0,
	BufferNotReady,
	PartialAvailable,
	Error,
};

struct ClipSaveInfo {
	ClipSaveResult result = ClipSaveResult::Error;
	int requestedSeconds = 0;
	int availableSeconds = 0;
	QString path;
	QString message;
};

class VerticalOutputs : public QObject {
	Q_OBJECT
public:
	explicit VerticalOutputs(QObject *parent = nullptr);
	~VerticalOutputs() override;

	void SetVideo(video_t *video);
	void ApplySettings(const vsp::PluginSettings &settings, bool *bufferRestartRequired = nullptr);

	bool StartStreaming(QString *error);
	void StopStreaming();
	bool IsStreaming() const;

	bool StartRecording(QString *error);
	void StopRecording();
	bool IsRecording() const;
	QString LastRecordingPath() const { return lastRecordingPath; }

	bool EnsureClipBuffer(QString *error);
	void StopClipBuffer();
	bool IsClipBufferActive() const;
	int BufferedSecondsAvailable() const;
	int ConfiguredBufferSeconds() const;

	ClipSaveInfo SaveShortClip();
	ClipSaveInfo SaveLongClip();
	ClipSaveInfo SaveClipOfDuration(int seconds, ClipKind kind, bool allowPartial);
	QString LastClipPath() const { return lastClipPath; }

	void StopAll();

	QString ActiveEncoderSummary() const;
	QString ActiveBitrateSummary() const;
	QString RecordingStatusSummary() const;
	QString ResolveRecordingDirectory() const;

	bool CanStartRecording(QString *error) const;
	bool PathWritable(QString *error) const;

signals:
	void streamingChanged(bool active);
	void recordingChanged(bool active);
	void clipBufferChanged(bool active);
	void clipSaved(const QString &path, ClipKind kind);
	void recordingStarted(const QString &path);
	void recordingStopped();

private:
	QString MakeOutputFilename(const QString &outputType, int durationSeconds = 0) const;
	ClipSaveInfo SaveClipInternal(int seconds, ClipKind kind, bool allowPartial);
	static void OnStreamStop(void *data, calldata_t *cd);
	static void OnRecordStop(void *data, calldata_t *cd);
	static void OnReplaySaved(void *data, calldata_t *cd);

	video_t *video = nullptr;
	vsp::PluginSettings settings;

	obs_output_t *streamOutput = nullptr;
	obs_output_t *recordOutput = nullptr;
	obs_output_t *replayOutput = nullptr;

	QString lastClipPath;
	QString lastRecordingPath;
	QString lastEncoderName;
	int lastVideoBitrate = 0;
	int lastAudioBitrate = 0;
	int configuredBufferSeconds = 60;
	QDateTime clipBufferStartedAt;
	ClipKind pendingClipKind = ClipKind::Short;
	int pendingClipSeconds = 0;
};
