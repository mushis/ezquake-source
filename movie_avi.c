/*
Copyright (C) 2001 Quake done Quick

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// movie_avi.c

#define USE_MEDIA_FOUNDATION

#include "quakedef.h"
#include <windows.h>
#include <vfw.h>
#include <msacm.h>
#include <mmreg.h>
#include <mmsystem.h>
#ifdef USE_MEDIA_FOUNDATION
#include <mfapi.h>
#include <mfidl.h>
#include <Mfreadwrite.h>
#include <mferror.h>
#include <Codecapi.h>
#include <Strmif.h>
#include <Wmcodecdsp.h>
#pragma comment(lib, "mf")
#pragma comment(lib, "strmiids")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfuuid")
#endif
#include "movie.h"
#include "movie_avi.h"
#include "qsound.h"
#include "gl_model.h"
#include "gl_local.h"
#include "utils.h"

#ifndef ACMAPI // mingw hax
#define ACMAPI WINAPI
#endif

static void OnChange_movie_codec(cvar_t *var, char *string, qbool *cancel);
static char movie_avi_filename[MAX_OSPATH]; // Stores the user's requested filename
static qbool Movie_Start_AVI_Capture(void);
static int avi_number = 0;                  // .avi files can lose sync/corrupt at 2GB/4GB, so we split into parts

//joe: capturing to avi
static qbool movie_avi_loaded = false;
static qbool movie_acm_loaded = false;
static char avipath[256];

enum {
	movieFormatAVI = 0,
	movieFormatWMV = 1,
	movieFormatH264 = 2
};

extern cvar_t   movie_fps;

static cvar_t   movie_codec          = {"demo_capture_codec", "0", 0, OnChange_movie_codec };	// Capturing to avi
static cvar_t   movie_mp3            = {"demo_capture_mp3", "0"};
static cvar_t   movie_mp3_kbps       = {"demo_capture_mp3_kbps", "128"};
static cvar_t   movie_vid_maxlen     = {"demo_capture_vid_maxlen", "0"};
#ifdef USE_MEDIA_FOUNDATION
static cvar_t   movie_format         = {"demo_capture_format", "0", 0, OnChange_movie_codec };
static cvar_t   movie_format_hwaccel = {"demo_capture_hwaccel", "0", 0, OnChange_movie_codec };
static cvar_t   movie_bitrate        = {"demo_capture_bitrate", "0", 0, OnChange_movie_codec };
static cvar_t   movie_bframes        = {"demo_capture_bframes", "0", 0, OnChange_movie_codec };
static cvar_t   movie_iframes_only   = {"demo_capture_iframes_only", "1", 0, OnChange_movie_codec };
#endif
static const char* formatExtensions[] = {
	".avi", ".wmv", ".mp4"
};

// 
static void (CALLBACK *qAVIFileInit)(void);
static HRESULT (CALLBACK *qAVIFileOpen)(PAVIFILE *, LPCTSTR, UINT, LPCLSID);
static HRESULT (CALLBACK *qAVIFileCreateStream)(PAVIFILE, PAVISTREAM *, AVISTREAMINFO *);
static HRESULT (CALLBACK *qAVIMakeCompressedStream)(PAVISTREAM *, PAVISTREAM, AVICOMPRESSOPTIONS *, CLSID *);
static HRESULT (CALLBACK *qAVIStreamSetFormat)(PAVISTREAM, LONG, LPVOID, LONG);
static HRESULT (CALLBACK *qAVIStreamWrite)(PAVISTREAM, LONG, LONG, LPVOID, LONG, DWORD, LONG *, LONG *);
static ULONG (CALLBACK *qAVIStreamRelease)(PAVISTREAM);
static ULONG (CALLBACK *qAVIFileRelease)(PAVIFILE);
static void (CALLBACK *qAVIFileExit)(void);

static MMRESULT (ACMAPI *qacmDriverOpen)(LPHACMDRIVER, HACMDRIVERID, DWORD);
static MMRESULT (ACMAPI *qacmDriverDetails)(HACMDRIVERID, LPACMDRIVERDETAILS, DWORD);
static MMRESULT (ACMAPI *qacmDriverEnum)(ACMDRIVERENUMCB, DWORD, DWORD);
static MMRESULT (ACMAPI *qacmFormatTagDetails)(HACMDRIVER, LPACMFORMATTAGDETAILS, DWORD);
static MMRESULT (ACMAPI *qacmStreamOpen)(LPHACMSTREAM, HACMDRIVER, LPWAVEFORMATEX, LPWAVEFORMATEX, LPWAVEFILTER, DWORD, DWORD, DWORD);
static MMRESULT (ACMAPI *qacmStreamSize)(HACMSTREAM, DWORD, LPDWORD, DWORD);
static MMRESULT (ACMAPI *qacmStreamPrepareHeader)(HACMSTREAM, LPACMSTREAMHEADER, DWORD);
static MMRESULT (ACMAPI *qacmStreamUnprepareHeader)(HACMSTREAM, LPACMSTREAMHEADER, DWORD);
static MMRESULT (ACMAPI *qacmStreamConvert)(HACMSTREAM, LPACMSTREAMHEADER, DWORD);
static MMRESULT (ACMAPI *qacmStreamClose)(HACMSTREAM, DWORD);
static MMRESULT (ACMAPI *qacmDriverClose)(HACMDRIVER, DWORD);

static HINSTANCE handle_avi = NULL, handle_acm = NULL;

static PAVIFILE	m_file;
static PAVISTREAM	m_uncompressed_video_stream;
static PAVISTREAM	m_compressed_video_stream;
static PAVISTREAM	m_audio_stream;

static unsigned long	m_codec_fourcc;
static int		m_video_frame_counter;
static int		m_video_frame_size;

static qbool	m_audio_is_mp3;
static int		m_audio_frame_counter;
static WAVEFORMATEX	m_wave_format;
static MPEGLAYER3WAVEFORMAT mp3_format;
static qbool	mp3_driver;
static HACMDRIVER	had;
static HACMSTREAM	hstr;
static ACMSTREAMHEADER	strhdr;
static LONG bytesWritten;

// Allocate video buffer at start, to save malloc()/free() calls every frame
static byte* videoBuffer = NULL;

#define AVI_GETFUNC(f) (qAVI##f = (void *)GetProcAddress(handle_avi, "AVI" #f))
#define ACM_GETFUNC(f) (qacm##f = (void *)GetProcAddress(handle_acm, "acm" #f))

static void AVIFile_Capture_InitAVI (void)
{
	movie_avi_loaded = false;

	if (!(handle_avi = LoadLibrary("avifil32.dll")))
	{
		Com_Printf ("\x02" "Avi capturing module not found\n");
		goto fail;
	}

	AVI_GETFUNC(FileInit);
	AVI_GETFUNC(FileOpen);
	AVI_GETFUNC(FileCreateStream);
	AVI_GETFUNC(MakeCompressedStream);
	AVI_GETFUNC(StreamSetFormat);
	AVI_GETFUNC(StreamWrite);
	AVI_GETFUNC(StreamRelease);
	AVI_GETFUNC(FileRelease);
	AVI_GETFUNC(FileExit);

	movie_avi_loaded = qAVIFileInit && qAVIFileOpen && qAVIFileCreateStream && 
			qAVIMakeCompressedStream && qAVIStreamSetFormat && qAVIStreamWrite && 
			qAVIStreamRelease && qAVIFileRelease && qAVIFileExit;

	if (!movie_avi_loaded)
	{
		Com_Printf_State (PRINT_FAIL, "Avi capturing module not initialized\n");
		goto fail;
	}

	Com_Printf_State (PRINT_OK, "Avi capturing module initialized\n");
	return;

fail:
	if (handle_avi)
	{
		FreeLibrary (handle_avi);
		handle_avi = NULL;
	}
}

static void AVIFile_Capture_InitACM (void)
{
	movie_acm_loaded = false;

	if (!(handle_acm = LoadLibrary("msacm32.dll")))
	{
		Com_Printf ("\x02" "ACM module not found\n");
		goto fail;
	}

	ACM_GETFUNC(DriverOpen);
	ACM_GETFUNC(DriverEnum);
	ACM_GETFUNC(StreamOpen);
	ACM_GETFUNC(StreamSize);
	ACM_GETFUNC(StreamPrepareHeader);
	ACM_GETFUNC(StreamUnprepareHeader);
	ACM_GETFUNC(StreamConvert);
	ACM_GETFUNC(StreamClose);
	ACM_GETFUNC(DriverClose);
	qacmDriverDetails = (void *)GetProcAddress (handle_acm, "acmDriverDetailsA");
	qacmFormatTagDetails = (void *)GetProcAddress (handle_acm, "acmFormatTagDetailsA");

	movie_acm_loaded = qacmDriverOpen && qacmDriverDetails && qacmDriverEnum && 
			qacmFormatTagDetails && qacmStreamOpen && qacmStreamSize && 
			qacmStreamPrepareHeader && qacmStreamUnprepareHeader && 
			qacmStreamConvert && qacmStreamClose && qacmDriverClose;

	if (!movie_acm_loaded)
	{
		Com_Printf_State (PRINT_FAIL, "ACM module not initialized\n");
		goto fail;
	}

	Com_Printf_State (PRINT_OK, "ACM module initialized\n");
	return;

fail:
	if (handle_acm)
	{
		FreeLibrary (handle_acm);
		handle_acm = NULL;
	}
}

static PAVISTREAM Capture_VideoStream (void)
{
	return m_codec_fourcc ? m_compressed_video_stream : m_uncompressed_video_stream;
}

#ifndef ACMDRIVERDETAILS_SUPPORTF_CODEC
#define ACMDRIVERDETAILS_SUPPORTF_CODEC 0x00000001L
#endif
#ifndef ACM_FORMATTAGDETAILSF_INDEX
#define ACM_FORMATTAGDETAILSF_INDEX         0x00000000L
#endif
#ifndef MPEGLAYER3_WFX_EXTRA_BYTES
#define MPEGLAYER3_WFX_EXTRA_BYTES   12
#define MPEGLAYER3_ID_UNKNOWN            0
#define MPEGLAYER3_ID_MPEG               1
#define MPEGLAYER3_ID_CONSTANTFRAMESIZE  2

#define MPEGLAYER3_FLAG_PADDING_ISO      0x00000000
#define MPEGLAYER3_FLAG_PADDING_ON       0x00000001
#define MPEGLAYER3_FLAG_PADDING_OFF      0x00000002
#endif
#ifndef ACMERR_BASE
#define ACMERR_BASE         (512)
#define ACMERR_NOTPOSSIBLE  (ACMERR_BASE + 0)
#define ACMERR_BUSY         (ACMERR_BASE + 1)
#define ACMERR_UNPREPARED   (ACMERR_BASE + 2)
#define ACMERR_CANCELED     (ACMERR_BASE + 3)
#endif

static BOOL CALLBACK acmDriverEnumCallback (HACMDRIVERID hadid, DWORD_PTR dwInstance, DWORD fdwSupport)
{
	if (fdwSupport & ACMDRIVERDETAILS_SUPPORTF_CODEC)
	{
		int	i;
		ACMDRIVERDETAILS details;

		details.cbStruct = sizeof(details);
		qacmDriverDetails (hadid, &details, 0);
		qacmDriverOpen (&had, hadid, 0);

		for (i = 0 ; i < details.cFormatTags ; i++)
		{
			ACMFORMATTAGDETAILS	fmtDetails;

			memset (&fmtDetails, 0, sizeof(fmtDetails));
			fmtDetails.cbStruct = sizeof(fmtDetails);
			fmtDetails.dwFormatTagIndex = i;
			qacmFormatTagDetails (had, &fmtDetails, ACM_FORMATTAGDETAILSF_INDEX);
			if (fmtDetails.dwFormatTag == WAVE_FORMAT_MPEGLAYER3)
			{
				Com_DPrintf ("MP3-capable ACM codec found: %s\n", details.szLongName);
				mp3_driver = true;

				return false;
			}
		}

		qacmDriverClose (had, 0);
	}

	return true;
}

static qbool AVIFile_Capture_Open (char *filename)
{
	HRESULT				hr;
	BITMAPINFOHEADER	bitmap_info_header;
	AVISTREAMINFO		stream_header;
	char				*fourcc;

	bytesWritten = 0;
	m_video_frame_counter = m_audio_frame_counter = 0;
	m_file = NULL;
	m_codec_fourcc = 0;
	m_compressed_video_stream = m_uncompressed_video_stream = m_audio_stream = NULL;
	m_audio_is_mp3 = (qbool)movie_mp3.value;

	if (*(fourcc = movie_codec.string) != '0')	// codec fourcc supplied
	{
		m_codec_fourcc = mmioFOURCC (fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
	}

	qAVIFileInit ();
	hr = qAVIFileOpen (&m_file, filename, OF_WRITE | OF_CREATE, NULL);
	if (FAILED(hr))
	{
		Com_Printf ("ERROR: Couldn't open AVI file\n");
		Capture_Close ();
		return false;
	}

	// initialize video data
	m_video_frame_size = glwidth * glheight * 3;

	memset (&bitmap_info_header, 0, sizeof(bitmap_info_header));
	bitmap_info_header.biSize = sizeof(BITMAPINFOHEADER);
	bitmap_info_header.biWidth = glwidth;
	bitmap_info_header.biHeight = glheight;
	bitmap_info_header.biPlanes = 1;
	bitmap_info_header.biBitCount = 24;
	bitmap_info_header.biCompression = BI_RGB;
	bitmap_info_header.biSizeImage = m_video_frame_size;

	memset (&stream_header, 0, sizeof(stream_header));
	stream_header.fccType = streamtypeVIDEO;
	stream_header.fccHandler = m_codec_fourcc;
	stream_header.dwScale = 1;
	stream_header.dwRate = (unsigned long)(0.5 + movie_fps.value);
	stream_header.dwSuggestedBufferSize = bitmap_info_header.biSizeImage;
	SetRect (&stream_header.rcFrame, 0, 0, bitmap_info_header.biWidth, bitmap_info_header.biHeight);

	hr = qAVIFileCreateStream (m_file, &m_uncompressed_video_stream, &stream_header);
	if (FAILED(hr))
	{
		Com_Printf ("ERROR: Couldn't create video stream (%X)\n", hr);
		Capture_Close ();
		return false;
	}

	if (m_codec_fourcc)
	{
		AVICOMPRESSOPTIONS	opts;

		memset (&opts, 0, sizeof(opts));
		opts.fccType = stream_header.fccType;
		opts.fccHandler = m_codec_fourcc;

		// Make the stream according to compression
		hr = qAVIMakeCompressedStream (&m_compressed_video_stream, m_uncompressed_video_stream, &opts, NULL);
		if (FAILED(hr))
		{
			Com_Printf ("ERROR: Couldn't make compressed video stream (%X)\n", hr);
			Capture_Close ();
			return false;
		}
	}

	hr = qAVIStreamSetFormat (Capture_VideoStream(), 0, &bitmap_info_header, bitmap_info_header.biSize);
	if (FAILED(hr))
	{
		Com_Printf ("ERROR: Couldn't set video stream format (%X)\n", hr);
		Capture_Close ();
		return false;
	}

	// initialize audio data
	memset (&m_wave_format, 0, sizeof(m_wave_format));
	m_wave_format.wFormatTag = WAVE_FORMAT_PCM;
	m_wave_format.nChannels = 2; // always stereo in Quake sound engine
	m_wave_format.nSamplesPerSec = shw ? shw->khz : 0;
	m_wave_format.wBitsPerSample = 16; // always 16bit in Quake sound engine
	m_wave_format.nBlockAlign = m_wave_format.wBitsPerSample/8 * m_wave_format.nChannels;
	m_wave_format.nAvgBytesPerSec = m_wave_format.nSamplesPerSec * m_wave_format.nBlockAlign;

	memset (&stream_header, 0, sizeof(stream_header));
	stream_header.fccType = streamtypeAUDIO;
	stream_header.dwScale = m_wave_format.nBlockAlign;
	stream_header.dwRate = stream_header.dwScale * (unsigned long)m_wave_format.nSamplesPerSec;
	stream_header.dwSampleSize = m_wave_format.nBlockAlign;

	hr = qAVIFileCreateStream (m_file, &m_audio_stream, &stream_header);
	if (FAILED(hr))
	{
		Com_Printf ("ERROR: Couldn't create audio stream\n");
		Capture_Close ();
		return false;
	}

	if (m_audio_is_mp3)
	{
		MMRESULT	mmr;

		// try to find an MP3 codec
		had = NULL;
		mp3_driver = false;
		qacmDriverEnum (acmDriverEnumCallback, 0, 0);
		if (!mp3_driver)
		{
			Com_Printf ("ERROR: Couldn't find any MP3 decoder\n");
			Capture_Close ();
			return false;
		}

		memset (&mp3_format, 0, sizeof(mp3_format));
		mp3_format.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
		mp3_format.wfx.nChannels = 2;
		mp3_format.wfx.nSamplesPerSec = shw->khz;
		mp3_format.wfx.wBitsPerSample = 0;
		mp3_format.wfx.nBlockAlign = 1;
		mp3_format.wfx.nAvgBytesPerSec = movie_mp3_kbps.value * 125;
		mp3_format.wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
		mp3_format.wID = MPEGLAYER3_ID_MPEG;
		mp3_format.fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
		mp3_format.nBlockSize = mp3_format.wfx.nAvgBytesPerSec / movie_fps.value;
		mp3_format.nFramesPerBlock = 1;
		mp3_format.nCodecDelay = 1393;

		hstr = NULL;
		if ((mmr = qacmStreamOpen(&hstr, had, &m_wave_format, &mp3_format.wfx, NULL, 0, 0, 0)))
		{
			switch (mmr)
			{
			case MMSYSERR_INVALPARAM:
				Com_Printf ("ERROR: Invalid parameters passed to acmStreamOpen\n");
				Capture_Close ();
				return false;

			case ACMERR_NOTPOSSIBLE:
				Com_Printf ("ERROR: No ACM filter found capable of decoding MP3\n");
				Capture_Close ();
				return false;

			default:
				Com_Printf ("ERROR: Couldn't open ACM decoding stream\n");
				Capture_Close ();
				return false;
			}
		}

		hr = qAVIStreamSetFormat (m_audio_stream, 0, &mp3_format, sizeof(MPEGLAYER3WAVEFORMAT));
		if (FAILED(hr))
		{
			Com_Printf ("ERROR: Couldn't set audio stream format\n");
			Capture_Close ();
			return false;
		}
	}
	else
	{
		hr = qAVIStreamSetFormat (m_audio_stream, 0, &m_wave_format, sizeof(WAVEFORMATEX));
		if (FAILED(hr))
		{
			Com_Printf ("ERROR: Couldn't set audio stream format\n");
			Capture_Close ();
			return false;
		}
	}

	return true;
}

static void AVIFile_Capture_Close (void)
{
	if (m_uncompressed_video_stream) {
		qAVIStreamRelease (m_uncompressed_video_stream);
		m_uncompressed_video_stream = 0;
	}
	if (m_compressed_video_stream) {
		qAVIStreamRelease (m_compressed_video_stream);
		m_compressed_video_stream = 0;
	}
	if (m_audio_stream) {
		qAVIStreamRelease (m_audio_stream);
		m_audio_stream = 0;
	}
	if (m_audio_is_mp3) {
		qacmStreamClose (hstr, 0);
		qacmDriverClose (had, 0);
		hstr = 0;
		had = 0;
	}
	if (m_file) {
		qAVIFileRelease (m_file);
		m_file = 0;
	}

	bytesWritten = 0;
	qAVIFileExit ();
}

static void AVIFile_Capture_WriteVideo (byte *pixel_buffer, int size)
{
	HRESULT	hr;
	LONG frameBytesWritten;

	// Check frame size (TODO: other things too?) hasn't changed
	if (m_video_frame_size != size)
	{
		Com_Printf ("ERROR: Frame size changed\n");
		return;
	}

	if (!Capture_VideoStream())
	{
		Com_Printf ("ERROR: Video stream is NULL\n");
		return;
	}

	// Write the pixel buffer to to the AVIFile, one sample/frame at the time
	// set each frame to be a keyframe (it doesn't depend on previous frames).
	hr = qAVIStreamWrite (Capture_VideoStream(), m_video_frame_counter++, 1, pixel_buffer, m_video_frame_size, AVIIF_KEYFRAME, NULL, &frameBytesWritten);
	if (FAILED(hr))
	{
		Com_Printf ("ERROR: Couldn't write to AVI file\n");
		return;
	}

	bytesWritten += frameBytesWritten;
}

#ifndef ACM_STREAMSIZEF_SOURCE
#define ACM_STREAMSIZEF_SOURCE          0x00000000L
#endif
#ifndef ACM_STREAMCONVERTF_BLOCKALIGN
#define ACM_STREAMCONVERTF_BLOCKALIGN   0x00000004
#define ACM_STREAMCONVERTF_START        0x00000010
#define ACM_STREAMCONVERTF_END          0x00000020
#endif

static void AVIFile_Capture_WriteAudio (int samples, byte *sample_buffer)
{
	HRESULT        hr = E_UNEXPECTED;
	LONG           frameBytesWritten;
	unsigned long  sample_bufsize;

	if (!m_audio_stream) {
		Com_Printf ("ERROR: Audio stream is NULL\n");
		return;
	}

	sample_bufsize = samples * m_wave_format.nBlockAlign;
	if (m_audio_is_mp3) {
		MMRESULT	mmr;
		byte		*mp3_buffer;
		unsigned long	mp3_bufsize;

		if ((mmr = qacmStreamSize (hstr, sample_bufsize, &mp3_bufsize, ACM_STREAMSIZEF_SOURCE))) {
			Com_Printf ("ERROR: Couldn't get mp3bufsize\n");
			return;
		}
		if (!mp3_bufsize) {
			Com_Printf ("ERROR: mp3bufsize is zero\n");
			return;
		}
		mp3_buffer = (byte *)Q_calloc (mp3_bufsize, 1);

		memset (&strhdr, 0, sizeof (strhdr));
		strhdr.cbStruct = sizeof (strhdr);
		strhdr.pbSrc = sample_buffer;
		strhdr.cbSrcLength = sample_bufsize;
		strhdr.pbDst = mp3_buffer;
		strhdr.cbDstLength = mp3_bufsize;

		if ((mmr = qacmStreamPrepareHeader (hstr, &strhdr, 0))) {
			Com_Printf ("ERROR: Couldn't prepare header\n");
			Q_free (mp3_buffer);
			return;
		}

		if ((mmr = qacmStreamConvert (hstr, &strhdr, ACM_STREAMCONVERTF_BLOCKALIGN))) {
			Com_Printf ("ERROR: Couldn't convert audio stream\n");
			goto clean;
		}

		hr = qAVIStreamWrite (m_audio_stream, m_audio_frame_counter++, 1, mp3_buffer, strhdr.cbDstLengthUsed, AVIIF_KEYFRAME, NULL, &frameBytesWritten);
		bytesWritten += frameBytesWritten;

	clean:
		if ((mmr = qacmStreamUnprepareHeader (hstr, &strhdr, 0))) {
			Com_Printf ("ERROR: Couldn't unprepare header\n");
			Q_free (mp3_buffer);
			return;
		}

		Q_free (mp3_buffer);
	}
	else {
		// The audio is not in MP3 format, just write the WAV data to the avi.
		hr = qAVIStreamWrite (m_audio_stream, m_audio_frame_counter++, 1, sample_buffer, samples * m_wave_format.nBlockAlign, AVIIF_KEYFRAME, NULL, &frameBytesWritten);
		bytesWritten += frameBytesWritten;
	}

	if (FAILED (hr)) {
		Com_Printf ("ERROR: Couldn't write to AVI file\n");
		return;
	}
}

static LONG Movie_CurrentLength (void)
{
	return bytesWritten;
}

static qbool ValidateMovieCodec (char* name)
{
	HKEY registryKey;
	int valueIndex;
	char valueName[64];
	DWORD valueLength = sizeof (valueName);

	// Uncompressed stream
	if (name[0] == 0 || !strcmp (name, "0")) {
		return true;
	}

	// Open / Create the key.
	if (RegCreateKeyEx (HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32",
		0, NULL, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE, NULL, &registryKey, NULL)) {
		Com_Printf_State (PRINT_WARNING, "Could not read list of valid codecs\n");
		return true; // assume ok, will find out later
	}

	strlcpy (valueName, "vidc.", sizeof (valueName));
	strlcat (valueName, name, sizeof (valueName));
	if (RegGetValue (registryKey, NULL, valueName, RRF_RT_ANY, NULL, NULL, NULL) == ERROR_SUCCESS) {
		RegCloseKey (registryKey);
		return true;
	}

	Con_Printf ("Warning: '%s' not found in list of valid codecs.\n", name);
	Con_Printf ("The following codecs are installed:\n", name);
	for (valueIndex = 0; RegEnumValue (registryKey, valueIndex, valueName, &valueLength, NULL, NULL, NULL, NULL) == ERROR_SUCCESS; valueIndex++, valueLength = sizeof (valueName)) {
		if (! strncmp (valueName, "vidc.", 5)) {
			Con_Printf ("  %s\n", valueName + 5);
		}
	}

	RegCloseKey (registryKey);
	return false;
}

static void OnChange_movie_codec(cvar_t *var, char *string, qbool *cancel)
{
	if (Movie_IsCapturingAVI()) {
		Con_Printf("Cannot change '%s' whilst capture in progress\n", var->name);
		*cancel = true;
		return;
	}

	// Print warning if codec specified isn't installed
	if (var == &movie_codec) {
		ValidateMovieCodec(string);
	}
}





#ifdef USE_MEDIA_FOUNDATION

static UINT64 Pack2UINT32AsUINT64(UINT32 unHigh, UINT32 unLow)
{
	return ((UINT64)unHigh << 32) | unLow;
}

#define MFSetAttributeSize(pAttributes,guidKey,unWidth,unHeight) pAttributes->lpVtbl->SetUINT64(pAttributes, guidKey, Pack2UINT32AsUINT64(unWidth, unHeight))
#define MFSetAttributeRatio(pAttributes,guidKey,unNumerator,unDenominator) pAttributes->lpVtbl->SetUINT64(pAttributes, guidKey, Pack2UINT32AsUINT64(unNumerator, unDenominator))
#define SafeRelease(x) \
	if (*x) { \
		(*x)->lpVtbl->Release((*x)); \
		*x = NULL; \
	}
#define HR_CHECK(x) \
	if (SUCCEEDED(hr)) { \
		hr = x; \
		if (FAILED(hr)) { \
			Con_Printf("%s = %X (%d)\n", #x, hr, __LINE__); \
		} \
	}

// Parameters for encoding, set as capture starts
// FIXME: Move to struct
static UINT32 videoWidth = 0;
static UINT32 videoHeight = 0;
static UINT32 videoFps = 30;
static qbool flipImageData = false;
static qbool useHardwareAcceleration = false;
static GUID videoEncodingFormat;
static GUID videoInputFormat;
static UINT32 mpeg2Profile = 0;
static UINT32 requestedBitRate = 0;
static qbool setRateControl = false;
static qbool setPrioritiseQuality = false;
static qbool allSamplesIndependent = true;
static int videoBFrameCount = 0;

// 
static IMFSinkWriter *pSinkWriter = NULL;
static DWORD videoStream = 0;
static DWORD audioStream = 0;
static LONGLONG videoTimestamp = 0;
static LONGLONG audioTimestamp = 0;
static qbool mediaFoundationInitialised = false;

static HRESULT CreateSinkWriter (const char* requestedName, IMFSinkWriter **ppWriter)
{
	extern cvar_t movie_dir;

	WCHAR           wideFilePath[192];
	HRESULT         hr            = 0;
	IMFAttributes   *pAttributes  = NULL;
	IMFSinkWriter   *pWriter      = NULL;

	// Convert to URL
	mbstowcs(wideFilePath, requestedName, sizeof(wideFilePath) / sizeof(wideFilePath[0]));

	*ppWriter = NULL;
	if (useHardwareAcceleration) {
		HR_CHECK(MFCreateAttributes(&pAttributes, 4));
		HR_CHECK(pAttributes->lpVtbl->SetUINT32(pAttributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
		HR_CHECK(MFCreateSinkWriterFromURL(wideFilePath, NULL, pAttributes, &pWriter));
	}
	else {
		HR_CHECK(MFCreateSinkWriterFromURL(wideFilePath, NULL, pAttributes, &pWriter));
	}
	if (SUCCEEDED(hr)) {
		*ppWriter = pWriter;
	}
	SafeRelease(&pAttributes);
	return hr;
}

static HRESULT InitializeSinkWriter(IMFSinkWriter **ppWriter, DWORD *pStreamIndex, DWORD *pAudioStreamIndex, const char* fileName)
{
	IMFSinkWriter   *pSinkWriter = NULL;
	IMFMediaType    *pMediaTypeOut = NULL;
	IMFMediaType    *pMediaTypeIn = NULL;
	IMFMediaType    *pAudioTypeOut = NULL;
	IMFMediaType    *pAudioTypeIn = NULL;
	HRESULT         hr = 0;
	DWORD           streamIndex = 0;
	DWORD           audioStream = 0;
	UINT32          defaultBitRate = 4000000;

	*ppWriter = NULL;
	*pStreamIndex = 0;

	// Create attributes
	HR_CHECK (CreateSinkWriter(fileName, &pSinkWriter))

	// Set the output media type.
	HR_CHECK (MFCreateMediaType (&pMediaTypeOut))
	HR_CHECK (pMediaTypeOut->lpVtbl->SetGUID(pMediaTypeOut, &MF_MT_MAJOR_TYPE, &MFMediaType_Video))
	HR_CHECK (pMediaTypeOut->lpVtbl->SetGUID(pMediaTypeOut, &MF_MT_SUBTYPE, &videoEncodingFormat))
	HR_CHECK (pMediaTypeOut->lpVtbl->SetUINT32(pMediaTypeOut, &MF_MT_AVG_BITRATE, requestedBitRate))
	HR_CHECK (pMediaTypeOut->lpVtbl->SetUINT32(pMediaTypeOut, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive))
	HR_CHECK (MFSetAttributeSize(pMediaTypeOut, &MF_MT_FRAME_SIZE, videoWidth, videoHeight))
	HR_CHECK (MFSetAttributeRatio(pMediaTypeOut, &MF_MT_FRAME_RATE, videoFps, 1))
	HR_CHECK (MFSetAttributeRatio(pMediaTypeOut, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1))

	if (videoBFrameCount) {
		HR_CHECK(pMediaTypeOut->lpVtbl->SetUINT32(pMediaTypeOut, &CODECAPI_AVEncMPVDefaultBPictureCount, videoBFrameCount));
	}
	if (movie_format.integer == movieFormatH264) {
		HR_CHECK (MFSetAttributeRatio(pMediaTypeOut, &MF_MT_FRAME_RATE_RANGE_MAX, videoFps, 1))
		HR_CHECK (MFSetAttributeRatio(pMediaTypeOut, &MF_MT_FRAME_RATE_RANGE_MIN, videoFps / 2, 1))
		HR_CHECK (pMediaTypeOut->lpVtbl->SetUINT32(pMediaTypeOut, &MF_MT_ALL_SAMPLES_INDEPENDENT, allSamplesIndependent))
		//pMediaTypeOut->lpVtbl->SetUINT32(pMediaTypeOut, &MF_MT_FIXED_SIZE_SAMPLES, 1);
		//pMediaTypeOut->lpVtbl->SetUINT32(pMediaTypeOut, &MF_MT_SAMPLE_SIZE, videoWidth * videoHeight * 4);
		HR_CHECK (pMediaTypeOut->lpVtbl->SetUINT32(pMediaTypeOut, &MF_MT_MPEG2_PROFILE, mpeg2Profile))
	}
	HR_CHECK (pSinkWriter->lpVtbl->AddStream(pSinkWriter, pMediaTypeOut, &streamIndex)) 

	// Set the input media type.
	HR_CHECK (MFCreateMediaType(&pMediaTypeIn))
	HR_CHECK (pMediaTypeIn->lpVtbl->SetGUID(pMediaTypeIn, &MF_MT_MAJOR_TYPE, &MFMediaType_Video))
	HR_CHECK (pMediaTypeIn->lpVtbl->SetGUID(pMediaTypeIn, &MF_MT_SUBTYPE, &videoInputFormat)) 
	HR_CHECK (pMediaTypeIn->lpVtbl->SetUINT32(pMediaTypeIn, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive))
	HR_CHECK (MFSetAttributeSize(pMediaTypeIn, &MF_MT_FRAME_SIZE, videoWidth, videoHeight))
	HR_CHECK (MFSetAttributeRatio(pMediaTypeIn, &MF_MT_FRAME_RATE, videoFps, 1))
	HR_CHECK (MFSetAttributeRatio(pMediaTypeIn, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1))
	HR_CHECK (pSinkWriter->lpVtbl->SetInputMediaType(pSinkWriter, streamIndex, pMediaTypeIn, NULL))

	// Create audio stream
	HR_CHECK (MFCreateMediaType(&pAudioTypeOut))
	HR_CHECK (pAudioTypeOut->lpVtbl->SetGUID(pAudioTypeOut, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio))
	HR_CHECK (pAudioTypeOut->lpVtbl->SetGUID(pAudioTypeOut, &MF_MT_SUBTYPE, &MFAudioFormat_AAC))
	HR_CHECK (pAudioTypeOut->lpVtbl->SetUINT32 (pAudioTypeOut, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16))  // "must be 16"
	HR_CHECK (pAudioTypeOut->lpVtbl->SetUINT32 (pAudioTypeOut, &MF_MT_AUDIO_SAMPLES_PER_SECOND, shw ? shw->khz : 0))  // "must match the input type"
	HR_CHECK (pAudioTypeOut->lpVtbl->SetUINT32 (pAudioTypeOut, &MF_MT_AUDIO_NUM_CHANNELS, 2))  // "must match the input type"
	HR_CHECK (pAudioTypeOut->lpVtbl->SetUINT32 (pAudioTypeOut, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 12000))  // "following values are supported: 12000, 16000, 20000, 24000"
	// HR_CHECK (pAudioTypeOut->lpVtbl->SetUINT32 (pAudioTypeOut, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 1))
	// HR_CHECK (pAudioTypeOut->lpVtbl->SetUINT32 (pAudioTypeOut, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29))
	HR_CHECK (pSinkWriter->lpVtbl->AddStream(pSinkWriter, pAudioTypeOut, &audioStream))

	// Set audio output type
	HR_CHECK (MFCreateMediaType(&pAudioTypeIn))
	HR_CHECK (pAudioTypeIn->lpVtbl->SetGUID(pAudioTypeIn, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio))
	HR_CHECK (pAudioTypeIn->lpVtbl->SetGUID(pAudioTypeIn, &MF_MT_SUBTYPE, &MFAudioFormat_PCM))
	HR_CHECK (pAudioTypeIn->lpVtbl->SetUINT32 (pAudioTypeIn, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16))
	HR_CHECK (pAudioTypeIn->lpVtbl->SetUINT32 (pAudioTypeIn, &MF_MT_AUDIO_SAMPLES_PER_SECOND, shw ? shw->khz : 0))
	HR_CHECK (pAudioTypeIn->lpVtbl->SetUINT32 (pAudioTypeIn, &MF_MT_AUDIO_NUM_CHANNELS, 2))
	HR_CHECK (pAudioTypeIn->lpVtbl->SetUINT32 (pAudioTypeIn, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (shw ? shw->khz : 0) * (16/8 * 2)))
	HR_CHECK (pAudioTypeIn->lpVtbl->SetUINT32 (pAudioTypeIn, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 4 /* 2 channels * (16 bits per sample / 8) */))
	HR_CHECK (pAudioTypeIn->lpVtbl->SetUINT32 (pAudioTypeIn, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1))
	HR_CHECK (pSinkWriter->lpVtbl->SetInputMediaType(pSinkWriter, audioStream, pAudioTypeIn, NULL))

	// Tell the sink writer to start accepting data.
	HR_CHECK (pSinkWriter->lpVtbl->BeginWriting(pSinkWriter))

	if (SUCCEEDED (hr)) {
		// Set quality setting
		ICodecAPI* pCodecApi = NULL;

		hr = pSinkWriter->lpVtbl->GetServiceForStream (pSinkWriter, streamIndex, &GUID_NULL, &IID_ICodecAPI, (LPVOID*)&pCodecApi);

		// Meag: Not really sure about these, trying to get the quality up
		if (SUCCEEDED (hr)) {
			VARIANT qualityTradeoff;
			VARIANT quality;
			VARIANT rateControl;
			GUID qualityVsSpeed  = CODECAPI_AVEncCommonQualityVsSpeed;
			GUID rateControlGUID = CODECAPI_AVEncCommonRateControlMode;
			GUID qualityGUID     = CODECAPI_AVEncCommonQuality;

			if (setPrioritiseQuality) {
				VariantInit(&qualityTradeoff);
				qualityTradeoff.vt = VT_UI4;
				qualityTradeoff.uintVal = 100;
				HR_CHECK(pCodecApi->lpVtbl->SetValue(pCodecApi, &qualityVsSpeed, &qualityTradeoff))
			}

			if (setRateControl) {
				VariantInit (&rateControl);
				rateControl.vt = VT_UI4;
				rateControl.uintVal = eAVEncCommonRateControlMode_Quality;  // Use CBR?

				HR_CHECK (pCodecApi->lpVtbl->SetValue (pCodecApi, &rateControlGUID, &rateControl))

				if (SUCCEEDED (hr)) {
					VariantInit (&quality);
					quality.vt = VT_UI4;
					quality.uintVal = 100;

					HR_CHECK (pCodecApi->lpVtbl->SetValue (pCodecApi, &qualityGUID, &quality))
				}
			}
		}

		// Success for these is optional, carry on
		hr = 0;
	}

	// Return the pointer to the caller.
	if (SUCCEEDED(hr)) {
		*ppWriter = pSinkWriter;
		(*ppWriter)->lpVtbl->AddRef(*ppWriter);
		*pStreamIndex = streamIndex;
		*pAudioStreamIndex = audioStream;
	}

	SafeRelease(&pSinkWriter);
	SafeRelease(&pMediaTypeOut);
	SafeRelease(&pMediaTypeIn);
	SafeRelease(&pAudioTypeOut);
	SafeRelease(&pAudioTypeIn);
	return hr;
}

static HRESULT WriteFrame(
	IMFSinkWriter *pWriter,
	DWORD streamIndex,
	const LONGLONG* timeStamp,
	byte *buffer,
	DWORD buffer_length,
	LONGLONG frameDuration,
	qbool isVideoFrame
)
{
	IMFSample *pSample = NULL;
	IMFMediaBuffer *pBuffer = NULL;
	LONG cbWidth = 3 * videoWidth;
	BYTE *pData = NULL;
	HRESULT hr = 0;

	// Create a new memory buffer.
	HR_CHECK (MFCreateMemoryBuffer (buffer_length, &pBuffer))
	if (FAILED (hr)) {
		Con_Printf ("MFCreateMemoryBuffer has failed, length = %d\n", buffer_length);
	}

	// Lock the buffer and copy the data to the buffer.
	HR_CHECK (pBuffer->lpVtbl->Lock(pBuffer, &pData, NULL, NULL))

	if (SUCCEEDED(hr)) {

		if (isVideoFrame) {
			BYTE* first_row = (BYTE*)buffer;
			LONG stride = cbWidth;

			if (flipImageData) {
				// Image will be flipped in memory so start at end and use negative stride for wmv
				first_row = (BYTE*)buffer + (videoHeight - 1) * cbWidth;
				stride = -stride;
			}

			HR_CHECK( MFCopyImage (
				pData,                      // Destination buffer.
				cbWidth,                    // Destination stride.
				first_row,                  // First row in source image.
				stride,                     // Source stride.
				cbWidth,                    // Image width in bytes.
				videoHeight                 // Image height in pixels.
			) )
		}
		else {
			// Audio: straight copy
			memcpy (pData, buffer, buffer_length);
		}
	}
	if (pBuffer) {
		HRESULT temp = pBuffer->lpVtbl->Unlock(pBuffer);
		if (FAILED (temp)) {
			Con_Printf ("->Unlock() failed (%X)", temp);
		}
	}

	// Set the data length of the buffer.
	HR_CHECK (pBuffer->lpVtbl->SetCurrentLength(pBuffer, buffer_length))

	// Create a media sample and add the buffer to the sample.
	HR_CHECK (MFCreateSample(&pSample))
	HR_CHECK (pSample->lpVtbl->AddBuffer(pSample, pBuffer))

	// Set the time stamp and the duration.
	HR_CHECK (pSample->lpVtbl->SetSampleTime(pSample, *timeStamp))
	HR_CHECK (pSample->lpVtbl->SetSampleDuration(pSample, frameDuration))

	// Send the sample to the Sink Writer.
	HR_CHECK (pWriter->lpVtbl->WriteSample(pWriter, streamIndex, pSample))

	SafeRelease(&pSample);
	SafeRelease(&pBuffer);
	return hr;
}

static qbool MediaFoundation_Capture_InitAVI (void)
{
	mediaFoundationInitialised = false;

	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (SUCCEEDED (hr)) {
		hr = MFStartup (MF_VERSION, MFSTARTUP_FULL);
		if (SUCCEEDED (hr)) {
			mediaFoundationInitialised = true;
		}
		else {
			CoUninitialize ();
		}
	}

	return mediaFoundationInitialised;
}

static void MediaFoundation_Capture_InitACM (void)
{
	// Does nothing, sound initialised in Capture_InitAVI
	movie_acm_loaded = true;
}

static qbool MediaFoundation_Capture_Open (char *filename)
{
	HRESULT hr;

	if (!mediaFoundationInitialised) {
		Con_Printf ("Error: Media Foundation library not initialised\n");
		return false;
	}

	videoFps = movie_fps.integer;
	videoWidth = glwidth;
	videoHeight = glheight;
	videoBuffer = (byte *)Q_malloc(videoWidth * videoHeight * 3);
	videoInputFormat = MFVideoFormat_RGB24;
	videoBFrameCount = movie_bframes.integer;
	allSamplesIndependent = movie_iframes_only.integer;

	if (movie_format.integer == movieFormatWMV) {
		useHardwareAcceleration = movie_format_hwaccel.integer;
		flipImageData = true;
		videoEncodingFormat = MFVideoFormat_WVC1;
		if (movie_bitrate.integer < 0) {
			requestedBitRate = videoWidth * videoHeight * 3 * 8 * videoFps;
		}
		else if (movie_bitrate.integer == 0) {
			requestedBitRate = videoWidth * videoHeight * 3 * 8 * videoFps / 250;
		}
		else {
			requestedBitRate = movie_bitrate.integer;
		}
		setRateControl = false;
		setPrioritiseQuality = false;
	}
	else if (movie_format.integer == movieFormatH264) {
		useHardwareAcceleration = movie_format_hwaccel.integer;
		flipImageData = false;
		videoEncodingFormat = MFVideoFormat_H264;
		mpeg2Profile = eAVEncH264VProfile_Main;
		if (movie_bitrate.integer < 0) {
			requestedBitRate = 24000000;
		}
		else if (movie_bitrate.integer == 0) {
			requestedBitRate = videoWidth * videoHeight * 3 * 8 * videoFps / 250;
		}
		else {
			requestedBitRate = movie_bitrate.integer;
		}
		setRateControl = true;
		setPrioritiseQuality = true;
	}

	hr = InitializeSinkWriter (&pSinkWriter, &videoStream, &audioStream, filename);
	// Con_Printf ("InitializeSinkWriter = %X, stream = %d, audioStream = %d\n", hr, videoStream, audioStream);

	audioTimestamp = videoTimestamp = 0;

	return SUCCEEDED (hr);
}

static void MediaFoundation_Capture_Stop (qbool success)
{
	if (pSinkWriter && success) {
		HRESULT hr = pSinkWriter->lpVtbl->Finalize (pSinkWriter);

		if (FAILED (hr)) {
			Con_Printf ("Finalize failed %X\n", hr);
		}
	}
	SafeRelease(&pSinkWriter);
}

static void MediaFoundation_Capture_WriteVideo (byte *pixel_buffer, int size)
{
	// This is set in 100-nanosecond units... which is 10m/second
	LONGLONG duration = 10 * 1000 * 1000 / videoFps;
	HRESULT hr;

	if (pSinkWriter == NULL)
		return;

	if (size != videoWidth * videoHeight * 3) {
		MediaFoundation_Capture_Stop (false);
		return;
	}

	hr = WriteFrame (pSinkWriter, videoStream, &videoTimestamp, pixel_buffer, size, duration, true);
	videoTimestamp += duration;

	if (FAILED (hr)) {
		Con_Printf ("WriteVideoFrame failed %X\n", hr);
		MediaFoundation_Capture_Stop (false);
	}
}

static void MediaFoundation_Capture_WriteAudio (int samples, byte *sample_buffer)
{
	// This is set in 100-nanosecond units... which is 10m/second
	LONGLONG duration       = 10 * 1000 * 1000 / videoFps;
	ULONG    sample_bufsize = samples * 4;
	HRESULT  hr;

	if (pSinkWriter == NULL)
		return;

	hr = WriteFrame (pSinkWriter, audioStream, &audioTimestamp, sample_buffer, sample_bufsize, duration, false);
	audioTimestamp += duration;

	if (FAILED (hr)) {
		Con_Printf ("WriteAudioFrame failed %X\n", hr);
		MediaFoundation_Capture_Stop (false);
	}
}

static void MediaFoundation_Capture_Close (void)
{
	MediaFoundation_Capture_Stop (true);
}

static void MediaFoundation_Capture_Shutdown (void)
{
	if (mediaFoundationInitialised) {
		MFShutdown ();
		CoUninitialize ();
	}
}

static void MovieEnumMFTs(void)
{
	HRESULT hr = 0;
	IMFActivate** mfts = NULL;
	UINT32 mftCount;

	if (!MediaFoundation_Capture_InitAVI()) {
		Com_Printf("Error: Failed to initialise Media Foundation\n");
		return;
	}

	HR_CHECK(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_TRANSCODE_ONLY, NULL, NULL, &mfts, &mftCount))
	if (SUCCEEDED(hr) && mfts) {
		UINT i;
		GUID friendlyNameGUID = MFT_FRIENDLY_NAME_Attribute;
		WCHAR value[128];

		for (i = 0; i < mftCount; ++i) {
			HRESULT hr2 = mfts[i]->lpVtbl->GetString(mfts[i], &friendlyNameGUID, value, sizeof(value) / sizeof(value[0]), NULL);
			if (SUCCEEDED(hr2)) {
				Com_Printf("> %S\n", value);
			}
		}
		CoTaskMemFree(mfts);
	}
}

#endif // USE_MEDIA_FOUNDATION

void Capture_InitAVI (void)
{
	AVIFile_Capture_InitAVI ();
#ifdef USE_MEDIA_FOUNDATION
	MediaFoundation_Capture_InitAVI ();
#endif

	if (!movie_avi_loaded)
		return;

	Cvar_SetCurrentGroup(CVAR_GROUP_DEMO);
	Cvar_Register(&movie_codec);
	Cvar_Register(&movie_vid_maxlen);
	Cvar_Register(&movie_format);
	Cvar_Register(&movie_format_hwaccel);
	Cvar_Register(&movie_bitrate);
	Cvar_Register(&movie_bframes);
	Cvar_Register(&movie_iframes_only);
	Cvar_ResetCurrentGroup();

	Cmd_AddCommand("movie_enum_mfts", MovieEnumMFTs);

	AVIFile_Capture_InitACM ();
	if (!movie_acm_loaded)
		return;

	Cvar_SetCurrentGroup(CVAR_GROUP_DEMO);
	Cvar_Register(&movie_mp3);
	Cvar_Register(&movie_mp3_kbps);
	Cvar_ResetCurrentGroup();
}

qbool Capture_Open (void)
{
#ifdef USE_MEDIA_FOUNDATION
	if (movie_format.integer) {
		return MediaFoundation_Capture_Open (avipath);
	}
#endif
	return AVIFile_Capture_Open (avipath);
}

void Capture_WriteAudio (int samples, byte *sample_buffer)
{
#ifdef USE_MEDIA_FOUNDATION
	if (movie_format.integer) {
		MediaFoundation_Capture_WriteAudio (samples, sample_buffer);
		return;
	}
#endif
	AVIFile_Capture_WriteAudio (samples, sample_buffer);
}

void Capture_Close (void)
{
	S_StopAllSounds();

#ifdef USE_MEDIA_FOUNDATION
	if (movie_format.integer) {
		MediaFoundation_Capture_Close ();
	}
	else
#endif
	{
		AVIFile_Capture_Close();
	}
}

void Capture_Shutdown (void)
{
#ifdef USE_MEDIA_FOUNDATION
	MediaFoundation_Capture_Shutdown ();
#endif
	if (videoBuffer) {
		Q_free(videoBuffer);
	}
}

void Capture_Stop(void)
{
	if (videoBuffer) {
		Q_free(videoBuffer);
	}
	Capture_Close ();
}

// Called whenever a new file needs to be opened (start of capture, or .avi splitting)
static qbool Movie_Start_AVI_Capture(void)
{
	extern cvar_t movie_dir;
	char aviname[MAX_OSPATH];
	FILE* avifile = NULL;

	++avi_number;

	// If we're going to break up the movie, append number
	if (avi_number > 1) {
		snprintf (aviname, sizeof (aviname), "%s-%03d", movie_avi_filename, avi_number);
	}
	else {
		strlcpy (aviname, movie_avi_filename, sizeof (aviname));
	}

	if (movie_format.integer < 0 || movie_format.integer >= sizeof (formatExtensions) / sizeof (formatExtensions[0])) {
		Con_Printf ("Invalid movie_format value\n");
		return false;
	}

	if (!(Util_Is_Valid_Filename(aviname))) {
		Com_Printf(Util_Invalid_Filename_Msg(aviname));
		return false;
	}

	COM_ForceExtensionEx (aviname, formatExtensions[movie_format.integer], sizeof (aviname));
	snprintf (avipath, sizeof(avipath), "%s/%s/%s", com_basedir, movie_dir.string, aviname);
	if (!(avifile = fopen(avipath, "wb"))) {
		FS_CreatePath (avipath);
		if (!(avifile = fopen(avipath, "wb"))) {
			Com_Printf("Error: Couldn't open %s\n", aviname);
			return false;
		}
	}
	fclose (avifile);
	avifile = NULL;
	return true;
}

void Capture_FinishFrame(void)
{
	int size = 0;
	int i;
	byte temp;
	extern void applyHWGamma(byte *buffer, int size);

	if (videoBuffer == NULL || glwidth != videoWidth || glheight != videoHeight) {
		Capture_Close();
		return;
	}

	// Split up if we're over the time limit for each segment
	if (movie_vid_maxlen.integer && Movie_CurrentLength() >= movie_vid_maxlen.value * 1024 * 1024 && movie_format.integer == 0) {
		// Close, advance filename, re-open
		Capture_Close();
		Movie_Start_AVI_Capture();
		Capture_Open();
	}

	// Capture
	// Set buffer size to fit RGB data for the image.
	size = glwidth * glheight * 3;

	// Allocate the RGB buffer, get the pixels from GL and apply the gamma.
	glReadPixels (glx, gly, glwidth, glheight, GL_RGB, GL_UNSIGNED_BYTE, videoBuffer);
	applyHWGamma (videoBuffer, size);

	// We now have a byte buffer with RGB values, but
	// before we write it to the file, we need to swap
	// them to GBR instead, which windows DIBs uses.
	// (There's a GL Extension that allows you to use
	// BGR_EXT instead of GL_RGB in the glReadPixels call
	// instead, but there is no real speed gain using it).
	for (i = 0; i < size; i += 3)
	{
		// Swap RGB => GBR
		temp = videoBuffer[i];
		videoBuffer[i] = videoBuffer[i+2];
		videoBuffer[i+2] = temp;
	}

	// Write the buffer to video.
#ifdef USE_MEDIA_FOUNDATION
	if (movie_format.integer) {
		MediaFoundation_Capture_WriteVideo(videoBuffer, size);
	}
	else
#endif
	{
		AVIFile_Capture_WriteVideo(videoBuffer, size);
	}
}

qbool Capture_StartCapture(char* fileName)
{
	strlcpy(movie_avi_filename, fileName, sizeof(movie_avi_filename)-10);		// Store user's requested filename
	if (!movie_avi_loaded) {
		Com_Printf_State (PRINT_FAIL, "Avi capturing not initialized\n");
		return false;
	}

	if (movie_format.integer && s_khz.integer < 44) {
		Com_Printf("Set s_khz to 44 beforing saving in .wmv or .mp4\n");
		return false;
	}

	// Start capture
	avi_number = 0;
	return Movie_Start_AVI_Capture();
}
