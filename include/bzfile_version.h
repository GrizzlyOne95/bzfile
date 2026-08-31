#pragma once

// Central version identity for the GrizzlyOne95 bzfile fork. Keep the string
// and numeric resource tuple together so the DLL, helper, CI, and release tag
// cannot drift independently.
#define BZFILE_VERSION_MAJOR 1
#define BZFILE_VERSION_MINOR 0
#define BZFILE_VERSION_PATCH 0
#define BZFILE_VERSION_BUILD 0
#define BZFILE_VERSION_TUPLE 1,0,0,0
#define BZFILE_VERSION_STRING "1.0.0"
#define BZFILE_VERSION_FILE_STRING "1.0.0.0\0"
