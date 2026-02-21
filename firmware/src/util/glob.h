#pragma once

#define MAX_GLOB_MATCHES 64

// Returns true if s contains wildcard characters (* or ?)
bool has_glob_chars(const char *s);

// Match pattern against name. * matches any sequence, ? matches one char.
bool glob_match(const char *pattern, const char *name);

// Expand a glob pattern (e.g. "/scripts/*.bas") into matching file paths.
// Splits pattern into directory + filename part, iterates directory,
// collects matches into a heap-allocated flat array (count x 64 bytes).
// Returns match count. Caller must free(*results) when done.
// Only matches regular files, not directories.
int glob_expand(const char *pattern, char (**results)[64]);
