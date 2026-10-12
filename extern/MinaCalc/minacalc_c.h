/* minacalc_c - a C interface to Etterna's MinaCalc difficulty calculator.
 *
 * MinaCalc itself is C++20; this header is plain C (C89 with stdint-free
 * types), so the game's C++03 code and Go (cgo) can both use it.  Nothing here
 * changes what the calculator does: every function is a thin wrapper around
 * MinaSDCalc() and friends in Etterna/MinaCalc/MinaCalc.cpp.
 *
 * Rows are what Etterna's NoteData::SerializeNoteData2 produces: one entry per
 * row that has a tap or hold head in some column, in time order, with
 *   notes = bit c set for a tap or hold head in column c (column 0 is bit 0);
 *   time  = seconds at music rate 1.0, relative to the first row that has
 *           anything in it (so usually the first row is at 0).
 * Rate scaling is done inside the calculator: always pass rate 1.0 times.
 * The calculator gives all zeros for rows that aren't strictly increasing in
 * time, have notes in columns past the key count, or are too dense (50 rows in
 * half a second) or too long (100000 half seconds). */

#ifndef MINACALC_C_H
#define MINACALC_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Same layout as Etterna's NoteInfo. */
typedef struct minacalc_row
{
	unsigned int notes;
	float time;
} minacalc_row;

/* Skillsets, in the order of every float[MINACALC_NUM_SKILLSETS] here. */
enum
{
	MINACALC_OVERALL = 0,
	MINACALC_STREAM,
	MINACALC_JUMPSTREAM,
	MINACALC_HANDSTREAM,
	MINACALC_STAMINA,
	MINACALC_JACKSPEED,
	MINACALC_CHORDJACK,
	MINACALC_TECHNICAL,
	MINACALC_NUM_SKILLSETS
};

/* The rates Etterna caches MSDs for: 0.7, 0.8, ... 2.0 (rate i is (i+7)/10). */
#define MINACALC_NUM_RATES 14

/* Return codes. */
enum
{
	MINACALC_OK = 0,
	MINACALC_ERR_ARGS = -1,		/* a NULL pointer, or a key count out of range */
	MINACALC_ERR_FAILED = -2	/* the calculator threw (out of memory and the like) */
};

/* Key counts the calculator accepts.  4 is the tuned one (Etterna ranks only
 * 4-key dance-single); 5, 6 and 7 have their own models, other counts use a
 * generic one. */
#define MINACALC_MIN_KEYS 2
#define MINACALC_MAX_KEYS 16

/* The version of the calculator (Etterna's GetCalcVersion()).  MSDs and SSRs
 * from different versions don't mix. */
int minacalc_version(void);

/* A calculator.  Every call starts from a new MinaCalc Calc, so the results
 * depend only on the arguments (upstream's Calc keeps some state from one run
 * to the next, which changes about one result in a hundred).  A handle may
 * only be used by one thread at a time; use one per thread. */
typedef struct minacalc_handle minacalc_handle;
minacalc_handle *minacalc_create(void);
void minacalc_destroy(minacalc_handle *h);

/* The MSD table, as Etterna caches it for a chart (Steps::CalcEtternaMetadata):
 * for each of the MINACALC_NUM_RATES rates, all skillsets at a 93% goal.
 * out must hold MINACALC_NUM_RATES * MINACALC_NUM_SKILLSETS floats; rate i,
 * skillset s is out[i * MINACALC_NUM_SKILLSETS + s].  Fewer than two rows give
 * all zeros, as in Etterna. */
int minacalc_msd(minacalc_handle *h, const minacalc_row *rows, size_t num_rows,
	unsigned int keycount, float *out);

/* One row of the MSD table at any music rate, not only the cached 0.7 to 2.0:
 * all skillsets at the 93% goal, calculated the way minacalc_msd does it, so at
 * a rate in the table the result is the same as the table's.  out must hold
 * MINACALC_NUM_SKILLSETS floats. */
int minacalc_msd_at_rate(minacalc_handle *h, const minacalc_row *rows, size_t num_rows,
	unsigned int keycount, float rate, float *out);

/* Score specific ratings: all skillsets for a score of goal (a Wife3 fraction,
 * 0.93 is 93%; above 0.965 counts as 0.965) at any music rate, as Etterna
 * works them out for a score.  out must hold MINACALC_NUM_SKILLSETS floats. */
int minacalc_ssr(minacalc_handle *h, const minacalc_row *rows, size_t num_rows,
	unsigned int keycount, float rate, float goal, float *out);

/* A player's rating in one skillset from the SSRs of their scores in it:
 * Etterna's aggregate_skill(ssrs, 0.1, 1.05, 0, 10.24) clamped to 0..100, as in
 * ScoreManager::CalcPlayerRating.  The order of ssrs doesn't matter.  The
 * overall rating is the mean of the seven skillset ratings (not Overall). */
float minacalc_skill_rating(const float *ssrs, size_t num_ssrs);

#ifdef __cplusplus
}
#endif

#endif
