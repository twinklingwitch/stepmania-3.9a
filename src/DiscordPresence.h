/* DiscordPresence - Discord Rich Presence: shows in Discord what is being
 * played ("Playing Stepmania 3.9", the song, the chart, a countdown to the end
 * of the song, the grade on the results).
 *
 * It talks to the Discord client running on this machine (see DiscordIpc.h),
 * from a thread of its own, so the game never waits on it.  If Discord isn't
 * running it looks again every 15 seconds without saying anything, and if the
 * connection drops it connects again.  Updates go out at most every 2 seconds,
 * always with the latest state.  The activity is cleared when the game exits.
 *
 * The DiscordRichPresence preference turns it off; an empty DiscordAppId does
 * too.  Nothing private is sent: no file paths, no profile names, and only
 * the public username of an online account that is logged in. */

#ifndef DISCORD_PRESENCE_H
#define DISCORD_PRESENCE_H

/* The game's Discord application ("Stepmania 3.9"), whose name is what
 * Discord shows as being played and which holds the "logo" and "online" art.
 * It's public, not a secret.  DiscordAppId in StepMania.ini overrides it. */
#define DISCORD_DEFAULT_APP_ID "1559247505366188054"

#include "ChartMSD.h"

class RageThread;
class Song;
class Steps;
struct DiscordShared;

class DiscordPresence
{
public:
	DiscordPresence();
	/* Clears the activity and stops the thread, waiting a moment at most. */
	~DiscordPresence();

	/* Main thread, every frame.  Looks at the game four times a second. */
	void Update( float fDeltaTime );

private:
	DiscordShared *m_pShared;
	RageThread *m_pThread;
	float m_fSinceLook;
	int64_t m_iSessionStartMs;
	CString m_sLastGroup;
	bool m_bHold;			// keep what's showing for now

	/* The song being played, and what goes with it. */
	const Song *m_pPlaySong;
	const Steps *m_pPlaySteps;
	float m_fPlayRate;
	int m_iPlayStage;
	float m_fPlayTime;		// on the gameplay screen, for this song
	int64_t m_iEndMs;

	/* Its MSD, worked out on the thread unless it's known already. */
	bool m_bHaveMSD;
	float m_fMSD[MINACALC_NUM_SKILLSETS];
	int m_iMSDJob;			// the job we're waiting for, or 0
	CString m_sMSDKey;
	unsigned m_uMSDHash;
	ChartMSDTable m_MSDTable;	// worked out on the thread,
	bool m_bStoreMSD;			// and to go in the song cache after the song

	void StartThread();
	CString BuildActivity();
	void StartSong( const Song *pSong, const Steps *pSteps, float fRate );
	void CollectMSD();
};

extern DiscordPresence *DISCORD;	// global and accessable from anywhere in our program

#endif
