#pragma once

// Auto-generated version information - DO NOT EDIT

#define ASTRADB_VERSION_MAJOR 1
#define ASTRADB_VERSION_MINOR 0
#define ASTRADB_VERSION_PATCH 0
#define ASTRADB_VERSION "1.0.0"
#define ASTRADB_VERSION_FULL "1.0.0"

// Git information (if available)
#define ASTRADB_GIT_BRANCH "refactor/server-no-sharing"
#define ASTRADB_GIT_COMMIT_HASH "031bc7efc7dc5aeb997943cc6f607b2e5d874ddb"
#define ASTRADB_GIT_COMMIT_SHORT "031bc7e"
#define ASTRADB_GIT_COMMIT_DATE "2026-03-13 16:05:24 +0800"
#define ASTRADB_GIT_IS_DIRTY 1

namespace astra::base {

struct VersionInfo {
  int major;
  int minor;
  int patch;
  const char* version;
  const char* git_branch;
  const char* git_commit_hash;
  const char* git_commit_short;
  const char* git_commit_date;
  bool git_is_dirty;
  
  const char* GetFullVersion() const {
    return version;
  }
  
  bool IsDirty() const {
    return git_is_dirty;
  }
  
  const char* GetGitBranch() const {
    return git_branch;
  }
  
  const char* GetGitCommit() const {
    return git_commit_hash;
  }
  
  const char* GetGitCommitShort() const {
    return git_commit_short;
  }
} constexpr kVersion = {
  ASTRADB_VERSION_MAJOR,
  ASTRADB_VERSION_MINOR,
  ASTRADB_VERSION_PATCH,
  ASTRADB_VERSION,
  ASTRADB_GIT_BRANCH,
  ASTRADB_GIT_COMMIT_HASH,
  ASTRADB_GIT_COMMIT_SHORT,
  ASTRADB_GIT_COMMIT_DATE,
  ASTRADB_GIT_IS_DIRTY
};

}  // namespace astra::base
