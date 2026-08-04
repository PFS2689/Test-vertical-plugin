#pragma once

#include <QString>
#include <cstdint>

#ifdef VSP_SETTINGS_TEST
#else
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <obs-module.h>
#endif

namespace vsp {

enum class StreamDestMode {
	InheritMain = 0,
	SeparateKey = 1,
	CustomServerAndKey = 2,
};

struct StreamDestination {
	QString serviceType;   /* e.g. rtmp_common, rtmp_custom */
	QString serviceName;   /* friendly service / platform name */
	QString server;        /* ingest URL (may be empty) */
	QString streamKey;     /* never log this unmasked */
	QString protocol;      /* rtmp / rtmps / srt / etc when known */
	QString accountHint;   /* non-secret account/channel label if present */
};

inline QString MaskSecret(const QString &secret, int keepTail = 4)
{
	if (secret.isEmpty())
		return QStringLiteral("(empty)");
	if (secret.size() <= keepTail)
		return QStringLiteral("****");
	return QStringLiteral("****") + secret.right(keepTail);
}

inline QString SanitizeUrlForLog(const QString &url)
{
	/* Strip userinfo and query that might contain tokens */
	QString s = url.trimmed();
	const int scheme = s.indexOf(QStringLiteral("://"));
	if (scheme >= 0) {
		const int authEnd = s.indexOf(QLatin1Char('@'), scheme + 3);
		if (authEnd > scheme) {
			s = s.left(scheme + 3) + QStringLiteral("***@") + s.mid(authEnd + 1);
		}
	}
	const int q = s.indexOf(QLatin1Char('?'));
	if (q >= 0)
		s = s.left(q) + QStringLiteral("?***");
	return s;
}

inline bool DestinationsConflict(const StreamDestination &a, const StreamDestination &b)
{
	if (a.streamKey.isEmpty() || b.streamKey.isEmpty())
		return false;
	if (a.streamKey != b.streamKey)
		return false;

	/* Same key + same server (or both empty custom) is a conflict */
	const QString sa = a.server.trimmed();
	const QString sb = b.server.trimmed();
	if (!sa.isEmpty() && !sb.isEmpty() && sa.compare(sb, Qt::CaseInsensitive) == 0)
		return true;
	if (sa.isEmpty() && sb.isEmpty() && a.serviceType == b.serviceType)
		return true;
	/* Same key with different servers: allow (distinct destinations) */
	if (!sa.isEmpty() && !sb.isEmpty() && sa.compare(sb, Qt::CaseInsensitive) != 0)
		return false;
	/* Same key, one server unknown — treat as conflict when service types match */
	return a.serviceType == b.serviceType;
}

inline QString DestinationModeLabel(StreamDestMode mode)
{
	switch (mode) {
	case StreamDestMode::InheritMain:
		return QStringLiteral("Main OBS destination");
	case StreamDestMode::SeparateKey:
		return QStringLiteral("Separate vertical stream key");
	case StreamDestMode::CustomServerAndKey:
		return QStringLiteral("Custom vertical server + key");
	}
	return QStringLiteral("Unknown");
}

inline QString ConflictUserMessage()
{
	return QStringLiteral(
		"Vertical Shorts cannot start a second live connection to the same destination "
		"while the main OBS stream is already active.\n\n"
		"Most platforms reject two simultaneous encoders using the same stream key.\n\n"
		"To go live vertically at the same time:\n"
		"• Configure a separate Vertical Streaming destination in Settings "
		"(different stream key and/or ingest server), or\n"
		"• Stop the main OBS stream first, or\n"
		"• Use a multi-output / dual-stream feature provided by your platform "
		"(if available) with distinct keys.\n\n"
		"This is a platform restriction — the plugin cannot bypass it.");
}

#ifndef VSP_SETTINGS_TEST
inline StreamDestination DestinationFromService(obs_service_t *service)
{
	StreamDestination d;
	if (!service)
		return d;

	const char *type = obs_service_get_type(service);
	d.serviceType = type ? QString::fromUtf8(type) : QString();

	OBSDataAutoRelease settings = obs_service_get_settings(service);
	if (settings) {
		d.serviceName = QString::fromUtf8(obs_data_get_string(settings, "service"));
		d.server = QString::fromUtf8(obs_data_get_string(settings, "server"));
		d.streamKey = QString::fromUtf8(obs_data_get_string(settings, "key"));
		if (d.streamKey.isEmpty())
			d.streamKey = QString::fromUtf8(obs_data_get_string(settings, "stream_key"));
		d.accountHint = QString::fromUtf8(obs_data_get_string(settings, "username"));
		if (d.accountHint.isEmpty())
			d.accountHint = QString::fromUtf8(obs_data_get_string(settings, "channel"));
	}

	if (d.server.startsWith(QStringLiteral("rtmps://"), Qt::CaseInsensitive))
		d.protocol = QStringLiteral("rtmps");
	else if (d.server.startsWith(QStringLiteral("rtmp://"), Qt::CaseInsensitive))
		d.protocol = QStringLiteral("rtmp");
	else if (d.server.startsWith(QStringLiteral("srt://"), Qt::CaseInsensitive))
		d.protocol = QStringLiteral("srt");

	return d;
}

inline StreamDestination MainStreamingDestination()
{
	obs_service_t *service = obs_frontend_get_streaming_service();
	StreamDestination d = DestinationFromService(service);
	if (service)
		obs_service_release(service);
	return d;
}

inline bool MainStreamingActive()
{
	return obs_frontend_streaming_active();
}
#endif

} // namespace vsp
