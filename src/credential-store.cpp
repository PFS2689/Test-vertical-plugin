#include "credential-store.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "Advapi32.lib")
#endif

namespace vsp {
namespace {

QString FallbackDir()
{
	const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	const QString dir = QDir(base).filePath(QStringLiteral("VerticalShorts/credentials"));
	QDir().mkpath(dir);
	return dir;
}

QString FallbackPath(const QString &targetId)
{
	QString safe = targetId;
	safe.replace(QLatin1Char('/'), QLatin1Char('_'));
	safe.replace(QLatin1Char('\\'), QLatin1Char('_'));
	safe.replace(QLatin1Char(':'), QLatin1Char('_'));
	return QDir(FallbackDir()).filePath(safe + QStringLiteral(".bin"));
}

CredentialStoreResult SaveFallback(const QString &targetId, const QString &secret)
{
	CredentialStoreResult r;
	const QString path = FallbackPath(targetId);
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		r.error = QStringLiteral("Could not write credential fallback file.");
		return r;
	}
	const QByteArray data = secret.toUtf8();
	if (f.write(data) != data.size()) {
		r.error = QStringLiteral("Failed writing credential fallback file.");
		return r;
	}
	f.close();
#ifdef _WIN32
	SetFileAttributesW((wchar_t *)path.utf16(), FILE_ATTRIBUTE_HIDDEN);
#endif
#ifndef _WIN32
	QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
	r.ok = true;
	r.usedSecureStorage = false;
	return r;
}

CredentialStoreResult LoadFallback(const QString &targetId, QString *out)
{
	CredentialStoreResult r;
	QFile f(FallbackPath(targetId));
	if (!f.exists()) {
		r.ok = true;
		r.usedSecureStorage = false;
		if (out)
			out->clear();
		return r;
	}
	if (!f.open(QIODevice::ReadOnly)) {
		r.error = QStringLiteral("Could not read credential fallback file.");
		return r;
	}
	if (out)
		*out = QString::fromUtf8(f.readAll());
	r.ok = true;
	r.usedSecureStorage = false;
	return r;
}

CredentialStoreResult DeleteFallback(const QString &targetId)
{
	CredentialStoreResult r;
	QFile::remove(FallbackPath(targetId));
	r.ok = true;
	r.usedSecureStorage = false;
	return r;
}

#ifdef _WIN32
QString CredTargetName(const QString &targetId)
{
	return QStringLiteral("VerticalShorts/") + targetId;
}
#endif

} // namespace

bool SecureStorageAvailable()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

QString SecureStorageDescription()
{
#ifdef _WIN32
	return QStringLiteral("Windows Credential Manager");
#else
	return QStringLiteral("Restricted local fallback file (secure OS store unavailable)");
#endif
}

CredentialStoreResult SaveSecret(const QString &targetId, const QString &secret)
{
#ifdef _WIN32
	CredentialStoreResult r;
	const QString name = CredTargetName(targetId);
	QByteArray blob = secret.toUtf8();
	CREDENTIALW cred{};
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = (LPWSTR)name.utf16();
	cred.CredentialBlobSize = (DWORD)blob.size();
	cred.CredentialBlob = (LPBYTE)blob.data();
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
	cred.UserName = (LPWSTR)L"VerticalShorts";
	if (!CredWriteW(&cred, 0)) {
		/* Fall back but report insecure path */
		r = SaveFallback(targetId, secret);
		if (r.ok)
			r.error = QStringLiteral(
				"Windows Credential Manager write failed; used restricted fallback file.");
		r.usedSecureStorage = false;
		return r;
	}
	DeleteFallback(targetId); /* remove any prior fallback */
	r.ok = true;
	r.usedSecureStorage = true;
	return r;
#else
	return SaveFallback(targetId, secret);
#endif
}

CredentialStoreResult LoadSecret(const QString &targetId, QString *outSecret)
{
#ifdef _WIN32
	CredentialStoreResult r;
	const QString name = CredTargetName(targetId);
	PCREDENTIALW cred = nullptr;
	if (CredReadW((LPCWSTR)name.utf16(), CRED_TYPE_GENERIC, 0, &cred)) {
		if (outSecret && cred->CredentialBlob && cred->CredentialBlobSize > 0) {
			*outSecret = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob),
						       (int)cred->CredentialBlobSize);
		} else if (outSecret) {
			outSecret->clear();
		}
		CredFree(cred);
		r.ok = true;
		r.usedSecureStorage = true;
		return r;
	}
	/* Fallback file */
	return LoadFallback(targetId, outSecret);
#else
	return LoadFallback(targetId, outSecret);
#endif
}

CredentialStoreResult DeleteSecret(const QString &targetId)
{
#ifdef _WIN32
	CredentialStoreResult r;
	const QString name = CredTargetName(targetId);
	CredDeleteW((LPCWSTR)name.utf16(), CRED_TYPE_GENERIC, 0);
	DeleteFallback(targetId);
	r.ok = true;
	r.usedSecureStorage = true;
	return r;
#else
	return DeleteFallback(targetId);
#endif
}

} // namespace vsp
