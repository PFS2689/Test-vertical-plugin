#pragma once

#include "plugin-settings.hpp"

#include <obs.hpp>

#include <QObject>
#include <QString>

class VerticalOutputs : public QObject {
	Q_OBJECT
public:
	explicit VerticalOutputs(QObject *parent = nullptr);
	~VerticalOutputs() override;

	void SetVideo(video_t *video);
	void ApplySettings(const vsp::PluginSettings &settings);

	bool StartStreaming(QString *error);
	void StopStreaming();
	bool IsStreaming() const;

	bool StartRecording(QString *error);
	void StopRecording();
	bool IsRecording() const;

	bool EnsureClipBuffer(QString *error);
	void StopClipBuffer();
	bool IsClipBufferActive() const;
	bool SaveClip(QString *savedPath, QString *error);
	QString LastClipPath() const { return lastClipPath; }

	void StopAll();

	QString ActiveEncoderSummary() const;
	QString ActiveBitrateSummary() const;

signals:
	void streamingChanged(bool active);
	void recordingChanged(bool active);
	void clipBufferChanged(bool active);
	void clipSaved(const QString &path);

private:
	QString ResolveRecordingDirectory() const;
	QString MakeRecordingFilename() const;
	static void OnStreamStop(void *data, calldata_t *cd);
	static void OnRecordStop(void *data, calldata_t *cd);
	static void OnReplaySaved(void *data, calldata_t *cd);

	video_t *video = nullptr;
	vsp::PluginSettings settings;

	obs_output_t *streamOutput = nullptr;
	obs_output_t *recordOutput = nullptr;
	obs_output_t *replayOutput = nullptr;

	QString lastClipPath;
	QString lastEncoderName;
	int lastVideoBitrate = 0;
	int lastAudioBitrate = 0;
};
