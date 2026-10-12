/* The C interface to MinaCalc; see minacalc_c.h.  Exceptions stop here. */
#include "minacalc_c.h"
#include "Etterna/MinaCalc/MinaCalc.h"

#include <algorithm>
#include <cmath>	// MinaCalcHelpers.h needs it but doesn't include it
#include <memory>
#include <vector>

#include "Etterna/MinaCalc/MinaCalcHelpers.h"

static_assert(sizeof(minacalc_row) == sizeof(NoteInfo), "minacalc_row must match NoteInfo");
static_assert(static_cast<int>(MINACALC_NUM_SKILLSETS) == static_cast<int>(NUM_Skillset), "skillset count");

/* A Calc remembers things from one run to the next (some of its pattern mod
 * state isn't reset), which changes the results of about one chart in a
 * hundred, sometimes by more than a point.  So every call gets a new one:
 * then the numbers depend only on the arguments, which the server and the
 * game need.  It is the same as Etterna's MinaSDCalc on a new Calc. */
struct minacalc_handle
{
	std::unique_ptr<Calc> calc;
	std::vector<NoteInfo> rows;

	Calc* fresh_calc()
	{
		calc.reset();
		calc = std::make_unique<Calc>();
		return calc.get();
	}
};

static bool
valid_args(minacalc_handle* h, const minacalc_row* rows, size_t num_rows, unsigned int keycount, const float* out)
{
	if (h == nullptr || out == nullptr || (rows == nullptr && num_rows != 0))
		return false;
	return keycount >= MINACALC_MIN_KEYS && keycount <= MINACALC_MAX_KEYS;
}

static void
copy_rows(minacalc_handle* h, const minacalc_row* rows, size_t num_rows)
{
	h->rows.clear();
	h->rows.reserve(num_rows);
	for (size_t i = 0; i < num_rows; i++)
		h->rows.push_back(NoteInfo{ rows[i].notes, rows[i].time });
}

extern "C" {

int
minacalc_version(void)
{
	return GetCalcVersion();
}

minacalc_handle*
minacalc_create(void)
{
	try {
		return new minacalc_handle;
	} catch (...) {
		return nullptr;
	}
}

void
minacalc_destroy(minacalc_handle* h)
{
	delete h;
}

int
minacalc_msd(minacalc_handle* h, const minacalc_row* rows, size_t num_rows, unsigned int keycount, float* out)
{
	if (!valid_args(h, rows, num_rows, keycount, out))
		return MINACALC_ERR_ARGS;
	try {
		copy_rows(h, rows, num_rows);
		const MinaSD msd = MinaSDCalc(h->rows, keycount, h->fresh_calc());
		if (msd.size() != MINACALC_NUM_RATES)
			return MINACALC_ERR_FAILED;
		for (size_t r = 0; r < msd.size(); r++) {
			if (msd[r].size() != MINACALC_NUM_SKILLSETS)
				return MINACALC_ERR_FAILED;
			std::copy(msd[r].begin(), msd[r].end(), out + r * MINACALC_NUM_SKILLSETS);
		}
		return MINACALC_OK;
	} catch (...) {
		return MINACALC_ERR_FAILED;
	}
}

int
minacalc_msd_at_rate(minacalc_handle* h, const minacalc_row* rows, size_t num_rows, unsigned int keycount, float rate, float* out)
{
	if (!valid_args(h, rows, num_rows, keycount, out) || !(rate > 0.F))
		return MINACALC_ERR_ARGS;
	try {
		copy_rows(h, rows, num_rows);
		if (h->rows.size() <= 1) {
			std::fill(out, out + MINACALC_NUM_SKILLSETS, 0.F);
			return MINACALC_OK;
		}
		// the body of MinaSDCalc's loop over the rates
		Calc* calc = h->fresh_calc();
		calc->ssr = false;
		calc->debugmode = false;
		calc->keycount = keycount;
		const std::vector<float> msd = calc->CalcMain(h->rows, rate, default_score_goal);
		if (msd.size() != MINACALC_NUM_SKILLSETS)
			return MINACALC_ERR_FAILED;
		std::copy(msd.begin(), msd.end(), out);
		return MINACALC_OK;
	} catch (...) {
		return MINACALC_ERR_FAILED;
	}
}

int
minacalc_ssr(minacalc_handle* h, const minacalc_row* rows, size_t num_rows, unsigned int keycount, float rate, float goal, float* out)
{
	if (!valid_args(h, rows, num_rows, keycount, out) || !(rate > 0.F))
		return MINACALC_ERR_ARGS;
	try {
		copy_rows(h, rows, num_rows);
		const std::vector<float> ssr = MinaSDCalc(h->rows, rate, goal, keycount, h->fresh_calc());
		if (ssr.size() != MINACALC_NUM_SKILLSETS)
			return MINACALC_ERR_FAILED;
		std::copy(ssr.begin(), ssr.end(), out);
		return MINACALC_OK;
	} catch (...) {
		return MINACALC_ERR_FAILED;
	}
}

float
minacalc_skill_rating(const float* ssrs, size_t num_ssrs)
{
	std::vector<float> v;
	if (ssrs != nullptr)
		v.assign(ssrs, ssrs + num_ssrs);
	// as in ScoreManager::CalcPlayerRating
	float r = aggregate_skill(v, 0.1L, (float)1.05, 0.0, (float)10.24);
	return std::clamp(r, 0.F, 100.F);
}

}
