# Windows Authenticode signing — Vertical Shorts Plugin

Production Release tags (**`vX.Y.Z`**) **require** a valid Authenticode signature on:

- `obs-shorts-vertical.dll`
- `Vertical-Shorts-Plugin-<version>-Setup.exe`

Unsigned Release publishes are rejected by CI.

This removes the SmartScreen **“Unknown publisher”** block that appears for unsigned apps. Do **not** disable Defender or add exclusions.

---

## Option A — Azure Artifact Signing (Trusted Signing) [recommended]

1. Create an [Azure Artifact Signing](https://learn.microsoft.com/azure/trusted-signing/) account and certificate profile (identity verification required).
2. Create an App Registration with a client secret.
3. Grant the app the **Artifact Signing Certificate Profile Signer** role on the signing account.
4. Add these **repository secrets** (Settings → Secrets and variables → Actions):

| Secret | Example |
|--------|---------|
| `AZURE_TENANT_ID` | directory GUID |
| `AZURE_CLIENT_ID` | app registration application ID |
| `AZURE_CLIENT_SECRET` | app client secret |
| `AZURE_TRUSTED_SIGNING_ENDPOINT` | `https://eus.codesigning.azure.net/` (use your region) |
| `AZURE_TRUSTED_SIGNING_ACCOUNT` | signing account name |
| `AZURE_TRUSTED_SIGNING_CERTIFICATE_PROFILE` | certificate profile name |

Regional endpoints include `eus`, `weu`, `wus2`, etc. The endpoint **must** match the region where the account was created.

---

## Option B — Traditional code-signing PFX

Purchase an OV/EV Authenticode certificate from a public CA, export as `.pfx`, then add:

| Secret | Value |
|--------|--------|
| `WINDOWS_CODESIGN_PFX_BASE64` | Base64 of the `.pfx` file (`base64 -w0 cert.pfx`) |
| `WINDOWS_CODESIGN_PASSWORD` | PFX password |

---

## Release flow (what CI does)

1. Build MSVC **Release** plugin DLL  
2. **Sign** `obs-shorts-vertical.dll`  
3. Package zip + Setup.exe (**Setup embeds the already-signed DLL**)  
4. **Sign** `Vertical-Shorts-Plugin-*-Setup.exe`  
5. Verify `Get-AuthenticodeSignature` → `Valid`  
6. Windows Defender + ClamAV scans  
7. Publish GitHub Release assets + `SHA256SUMS.txt`

---

## Local verification

```powershell
Get-AuthenticodeSignature '.\Vertical-Shorts-Plugin-1.0.5-Setup.exe'
Get-AuthenticodeSignature '.\obs-shorts-vertical.dll'
```

Both must report `Status : Valid`.
