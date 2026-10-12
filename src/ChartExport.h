/* ChartExport - Writes every chart of a song pack as JSON, for the online
 * server: the same parsing, chart keys and MinaCalc rows as the game itself.
 *
 *   stepmania --export-charts=/path/to/Pack --export-out=/path/to/pack.json
 *
 * Runs instead of the game: nothing is shown and nothing is written but the
 * output file and the log.  The song cache isn't used.  The format is in
 * docs/API.md in the 39server repository. */

#ifndef CHART_EXPORT_H
#define CHART_EXPORT_H

class Song;
class Steps;
class TimingData;

/* Returns the process exit code: 0 on success. */
int ChartExportMain( const CString &sPackDir, const CString &sOutFile );

/* One chart of the export, and the "timing" object in it.  The game uses these
 * too, for test vectors and input logs, so both say the same thing. */
CString ChartExportChartJson( const Song *pSong, const Steps *pSteps, const CString &sSongDir );
CString ChartExportTimingJson( const TimingData &td );

#endif
