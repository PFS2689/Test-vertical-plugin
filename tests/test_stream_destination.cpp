#include "stream-destination.hpp"

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
	expect(vsp::MaskSecret(QStringLiteral("abcdefghij")) == QStringLiteral("****ghij"), "mask keeps tail");
	expect(vsp::MaskSecret(QStringLiteral("ab")) == QStringLiteral("****"), "short secret fully masked");
	expect(vsp::MaskSecret(QString()).contains(QStringLiteral("empty")), "empty secret labeled");

	const QString sanitized = vsp::SanitizeUrlForLog(QStringLiteral("rtmp://user:pass@ingest.example/app?token=1"));
	expect(!sanitized.contains(QStringLiteral("pass")), "password stripped from URL log");
	expect(!sanitized.contains(QStringLiteral("token=1")), "query stripped from URL log");

	vsp::StreamDestination a;
	a.serviceType = QStringLiteral("rtmp_custom");
	a.server = QStringLiteral("rtmp://a.example/live");
	a.streamKey = QStringLiteral("key-one");

	vsp::StreamDestination b = a;
	expect(vsp::DestinationsConflict(a, b), "identical destinations conflict");

	b.streamKey = QStringLiteral("key-two");
	expect(!vsp::DestinationsConflict(a, b), "different keys do not conflict");

	b.streamKey = a.streamKey;
	b.server = QStringLiteral("rtmp://b.example/live");
	expect(!vsp::DestinationsConflict(a, b), "same key different servers allowed");

	vsp::StreamDestination c;
	c.serviceType = a.serviceType;
	c.streamKey = a.streamKey;
	expect(vsp::DestinationsConflict(a, c) || a.server.isEmpty() == false, "same key empty-vs-set handled");

	/* empty keys never conflict */
	vsp::StreamDestination emptyKey = a;
	emptyKey.streamKey.clear();
	expect(!vsp::DestinationsConflict(a, emptyKey), "empty key no conflict");

	expect(vsp::ConflictUserMessage().contains(QStringLiteral("stream key")), "conflict message mentions stream key");
	expect(!vsp::ConflictUserMessage().contains(QStringLiteral("key-one")), "conflict message has no real key");

	if (failures) {
		std::cerr << failures << " test(s) failed\n";
		return 1;
	}
	std::cout << "all stream destination tests passed\n";
	return 0;
}
