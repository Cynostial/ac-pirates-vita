#ifndef ACP_ONLINE_CONFIG_H
#define ACP_ONLINE_CONFIG_H

/* Optional service configuration is supplied with build.py --online-config.
 * Unconfigured builds keep native trophies but make no leaderboard requests.
 */
#ifndef ACP_LEADERBOARD_URL
#define ACP_LEADERBOARD_URL ""
#endif
#ifndef ACP_LEADERBOARD_KEY
#define ACP_LEADERBOARD_KEY ""
#endif

#endif
