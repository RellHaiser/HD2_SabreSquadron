// The dedicated server's `-autorestart`: a copy of the server, started with that switch, that starts again the servers
// whose session has gone. See net_autorestart.cpp.
#pragma once

#ifdef HD2_DEDICATED

void RunAutoRestartWatchdog();                      // (DS 0x007ab1a0) the process with `-autorestart`, until it ends
void StartAutoRestartWatchdog();                    // (DS 0x007ab470) unless one is running

// A session a watchdog looks after, by its name (S_MultiplayerSettings::sessionName).
void WatchSession(const char *session);             // (DS 0x007aae40)
void UnwatchSession(const char *session);           // (DS 0x007aadf0)

#endif
