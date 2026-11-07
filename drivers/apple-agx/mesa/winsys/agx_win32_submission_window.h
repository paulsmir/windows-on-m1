/* SPDX-License-Identifier: MIT
 * Bounded CPU submission tracking; independent of KMD execution depth.
 * Zero-initialized/legacy owners keep four. Only the selected experiment
 * value enlarges the window. Invalid values cannot enlarge array bounds.
 */
#ifndef AGX_WIN32_SUBMISSION_WINDOW_H
#define AGX_WIN32_SUBMISSION_WINDOW_H
#define AGX_SUBMISSION_WINDOW_DEFAULT 4u
#define AGX_SUBMISSION_WINDOW_MAX 16u
static inline unsigned AgxSubmissionWindowLimit(unsigned selected) {
  return selected == AGX_SUBMISSION_WINDOW_MAX ? selected : AGX_SUBMISSION_WINDOW_DEFAULT;
}
#endif
