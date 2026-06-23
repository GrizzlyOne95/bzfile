## 2025-01-24 - Centralized Write Protection for Critical DLLs
**Vulnerability:** Lua scripts could potentially overwrite or delete the library itself (`bzfile.dll`) or other sensitive shims like `winmm.dll`, even if `g_AllowWinmmOverwrite` was only intended to control `ReplaceFileOnExit`.
**Learning:** Security checks were inconsistently applied across different filesystem operations (`Open`, `CopyFile`, `Delete`, `ReplaceFileOnExit`). `Delete` and `Open` (for writing) had no protection against targeting critical DLLs.
**Prevention:** Centralize write-protection logic into a single helper (`IsWriteProtected`) and ensure it is called by all functions that can modify or remove files.
