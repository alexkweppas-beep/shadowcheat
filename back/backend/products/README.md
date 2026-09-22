# Protected product templates

Only protected variants belong here. For RAGE:MP deploy 32–64 cohorts:

`ragemp/variants/00/cheat.dll` … `ragemp/variants/31/cheat.dll`

The DLL must pass `internal/scripts/audit_protected_artifact.py` and contain
exactly one `SHADOW_CUSTOMER_TAG_...` placeholder. The backend replaces it
with an HMAC-derived customer tag before encryption. Cohort selection is a
stable HMAC of user/HWID and `RAGEMP_VARIANT_COUNT`.
Treat the count as release metadata: changing it remaps existing users.

Set a unique production `WATERMARK_SECRET` of at least 32 characters. Rotating
it changes future customer tags and should be treated as a release operation.

`ragemp/features/` contains server-only source for the Quant, FiveRP and
Ukraine bundles. The files are ignored by git and exported by
`Build-ProtectedVariants.ps1`. The backend adds a launch-specific semantic
canary and encrypts each response independently.
