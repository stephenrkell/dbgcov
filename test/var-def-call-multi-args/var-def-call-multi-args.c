// Extracted from Git's `wt-status.c`

#include <sys/stat.h>

struct wt_status_state {
  int am_in_progress;
  int am_empty_patch;
  int rebase_in_progress;
  int rebase_interactive_in_progress;
  char *branch;
  char *onto;
};

const char *worktree_git_path(const struct worktree *wt,
            const char *fmt, ...)
  __attribute__((format (printf, 2, 3)));

int wt_status_check_rebase(const struct worktree *wt,
                           struct wt_status_state *state)
{
  struct stat st;

  if (!stat(worktree_git_path(wt, "rebase-apply"), &st)) {
    if (!stat(worktree_git_path(wt, "rebase-apply/applying"), &st)) {
      state->am_in_progress = 1;
      if (!stat(worktree_git_path(wt, "rebase-apply/patch"), &st) && !st.st_size)
        state->am_empty_patch = 1;
    } else {
      state->rebase_in_progress = 1;
    }
  } else if (!stat(worktree_git_path(wt, "rebase-merge"), &st)) {
    if (!stat(worktree_git_path(wt, "rebase-merge/interactive"), &st))
      state->rebase_interactive_in_progress = 1;
    else
      state->rebase_in_progress = 1;
  } else
    return 0;
  return 1;
}
