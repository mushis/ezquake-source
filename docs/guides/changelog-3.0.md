---
layout: default
tab: Manual
---

## Changes in 3.0

### General

* Major overhaul of backend code - now using SDL2 library for all three platforms.  Linux & OSX versions in particular are now much improved.
* fov was calculated at 4:3 or 16:10 aspect ratios.  Now use 4:3 `/fov` value and ezQuake will adjust for your resolution
* Added ruleset 'thunderdome', a less restrictive version of smackdown, allowing `cl_rollalpha` and particle effects during game
* Added ruleset 'qcon`, a more restrictive version of smackdown for tournament play
* Added AFK status on scoreboard (see `/scr_scoreboard_afk` and `/scr_scoreboard_afk_style`)
* Added support for 'new' player model by CapnBub (passes f_modified)
* Added `/gender` to control gender of player in messages.  Requires server support for streak messages to be correct
* Added `/con_fragmessages 2` option to show frag messages in the console, but not in the notification area
* Added `/con_mm2_only` option to hide all messages which are not mm2 (`/say_team`) from the notification area during game
* Added `/lg_color` command & `gl_custom_lg_color_enabled` to allow lg bolt color to be set without replacing texture
* Added ability for all all mm2 codes to have fixed widths and alignments set.  For example `say_team %<10r>l` will print the player location, but fixed-width 10 characters, aligned right.  Defaults can be set by (e.g.) `/set tp_length_l 10; /set tp_align_l r`
* Added `/tp_tooktimeout` & `/tp_pointtimeout` to customise time before last taken/pointed item is blanked
* Added `/cl_textencoding` to control outgoing formatting of unicode characters.  FTE's ^U encoding now supported
* Added `/hud_score_bar` options to allow player/team names to stay in the same order, regardless of current tracked player
* Bugfix: server browser should be more stable
* Bugfix: scoreboard would not appear in auto-screenshot if console was down at match end
* Bugfix: `/cl_delay_packet` could cause packet loss
* Bugfix: extended uptime led to jerky player prediction
* Bugfix: starting client direct from qw:// url could lead to viewsize, fakeshaft having incorrect settings
* Bugfix: where chat icons & player identifications were shown during intermission
* Fixed bug in downloading from FTE servers
* Bug where `/setinfo rsnd 0` caused pickups to not be detected correctly... not fully fixed, but should happen less often
* Removed `/vid_wideaspect` - use `/calc_fov` to work from your 2.2 `/fov` value back to the original 4:3

### Input

* ezQuake now uses operating system keyboard layout by default.  `/in_builtinkeymap 1` switches back to Quake/American default
* `/sys_inactivesound 2` will play sounds when out of focus but not minimised
* Mouse acceleration replaced with [povohat's method](http://www.quakeworld.nu/forum/topic/6488).  Added `/m_accel_power`, `/m_accel_senscap`, `/m_accel_offset`
* `/in_release_mouse_modes` cvar controls when the mouse cursor is released by the application when in windowed mode
* `/in_mouse` removed, instead use `/in_raw` toggle.  Raw input is enabled by default
* Removed `/m_showrate`

### Audio

* Lower latency audio - audio is now mixed in its own thread
* Added `/s_desired_samples` to suggest a buffersize to audio thread
* Added `/s_listdrivers` to print sound drivers supported (SDL information)
* Added `/s_audiodevice` to set which audio device to use (`/s_audiodevicelist` gives list)
* Removed `/s_mixahead`

### Video settings

* Game runs at desktop resolution by default (`/vid_usedesktopres 0` to disable)
* Alt-enter toggles fullscreen on all platforms (`/sys_disable_alt_enter 1` to disable)
* Added `/vid_conscale` to be used to calculate `/vid_conwidth` & `/vid_conheight` based on resolution
* Added `/vid_displaynumber`, to choose which display to run on
* Added `/vid_win_width`, `/vid_win_displaynumber`, `/vid_win_width`, `/vid_win_height` to store windowed resolution independently of fullscreen
* Added `/vid_win_save_pos`, `/vid_win_save_size` to control saving of window position
* Added `/vid_24bit_depth` to control using 24-bit z-buffer (on by default)
* Removed `/vid_mode`, `/vid_customwidth`, `/vid_customheight`

### QTV

* `/join` command will work when spectating via QTV
* `/qtv <server>` command will spectate `<server>` through QTV (requires that server is listed on [QTV website](qtv.quakeworld.nu))
* `/find <name>` will return all players matching `<name>` (requires that server is listed on [QTV website](qtv.quakeworld.nu))
* `/qtv_fixuser` command allows you to fix player userinfo strings on older versions of QTV
* Bugfix: muzzle flashes no longer appear in incorrect positions
* Bugfix: stop `/cl_demospeed` from causing frozen explosion effects (can re-enable with `/qtv_allow_pause`)

### Demo playback

* New hud element 'static_text'.  Enabled only when spectating or playing, allows custom, multi-line text area on screen
* Added support for playback of .qwd demos from QW 2.1 and up (protocol versions 24/25)
* `/cam_pos` now stops tracking player.  Rewinding with `/demo_jump` doesn't lose camera position
* Added `/cl_demoteamplay` to let you view standard QW `+showteamscores` when watching NQ (.dem) demos.  Players with -99 frags will appear as spectators
* Bugfix: `/demo_jump` - works on .dem files, doesn't break prediction of players in .qwd files
* Bugfix: `/playdemo x.mvd` will not attempt to play x.qwd then x.mvd
* Bugfix: .dem playback - player colors set, particle trails, textures, third-person cameras and .lit files should work as normal.
* Bugfix: Spectator scoreboard now obeys `/teamlock` (current player can have enemy color)
* Bugfix: .avi file would be corrupt if too large.  `/demo_capture_max_vid_len` controls splitting of output over multiple files.
* Bugfix: Player prediction enabled on .qwd playback, should be much smoother

### Other

* Console variables can have "auto" values supplied, so the user can see the value being used by the engine rather than just the default values & currently set
* Added `/con_bindphysical` to toggle `/bind <key>` interpretting `<key>` as based on OS or Quake layout.  Defaults to 1 at start of every configuration file and is reset afterwards.
* Added `/f_focusgained` trigger
* Added `/batteryinfo` command
* Set limit of 10 seconds on console notification times.
* When searching for textures, precedence given to individual files over those found in .pak or .pk3 files
* Can associate client directly with URLs in browser (i.e. `ezquake-gl.exe qw://<serverip>`)
* Command and variable documentation is now stored in .json files and compiled into the executable
* Single makefile to make all platforms
* Repo now uses Travis CI and Docker
* Removed: plugin system, software renderer, assembly code, security module, TCL scripting, FTE2 voice chat


