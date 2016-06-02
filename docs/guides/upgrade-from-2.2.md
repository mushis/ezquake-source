---
layout: default
tab: Manual
---

### Upgrading from 2.2 to 3.0

Some changes & bug fixes made in 3.0 mean configs from 2.2 will require some tweaking - here are some of the most common issues & queries.

---

#### FOV adjustment

ezQuake 2 requires different `/fov` settings for [different monitors/resolutions](http://wiki.quakeworld.nu/Widescreen_Guide).  ezQuake 3 does not - use the same fov value no matter what resolution/monitor, it will look the same.  It also supports 16:9 properly.

If you use a widescreen monitor/resolution, go back to your 4:3 fov.  If you can't remember your 4:3 fov, use the `/calc_fov` command to convert a 2.2 fov to the 4:3 equivalent.

---

#### Video mode adjustments

Video system has been simplified. Currently uses the desktop resolution by default (togglable) since most displays today have one native resolution only. `/vid_mode`, `/vid_customwidth`, `/vid_customheight` etc has been removed or replaced.

`/vid_usedesktopres 1`. If set to 0 then set resolution with `/vid_width` and `/vid_height` cvars.

To choose which monitor to use, use `/vid_displaynumber`

ezQuake now also keeps windowed parameters in different cvars, use `/vid_win_width`, `/vid_win_height`, `/vid_win_displaynumber` instead if using windowed mode.

---

#### Mouse input
    
All `/in_mouse` settings have been replaced with a `/in_raw 0/1` toggle. Raw input or no raw input. For Windows users using `/in_mouse 2/3` in ezQuake 2, nothing should need to be done.

OSX users can finally enjoy raw input with no acceleration!

Linux users no longer have to deal with permissions for evdev or anything like that, it just works&trade;

If you are used to `/in_mouse 1` on Windows, then set `/in_raw 0`, although we recommend keeping it on for the best input handling.

---

#### Audio

3.0 now mixes audio as your soundcard demands it, rather than preparing sound ahead of time.

This should lead to reduced latency between a sound packet arriving and you hearing the noise.

`/s_mixahead` variable has gone.  Instead use `/s_desiredsamples` to suggest a buffersize to the sound driver.  The higher the buffersize, the higher the latency but the likelihood of 'crackling' decreases.  You may need to play with this value to get best tradeoff between sound latency and quality.

---

#### Texture load order

2.2 would prioritise the search for textures based on format alone (.tga &gt; .png &gt; .jpg &gt; .pcx).  3.0 keeps the same order, but prioritises individual files over those found inside .pak/pk3 files

This might lead to files in your /textures/ directory suddenly being used after years of being ignored.  Remove or rename the files to get back to previous behaviour.

---

#### Keyboard mapping

3.0 uses the same keyboard mapping as your operating system.  `/in_builtinkeymap 1` will return standard behaviour (Quake/US keyboard layout)

If writing a config file where the key is important rather than the physical button ('r' to toggle radar, for instance), then set `/con_bindphysical 0` at the top of the file.

