# Changelog

## 0.2.0 (beta)

The first public version: the Iceberg method of Iceberg MATLAB v1.0.0 as a C++ library
and a standalone app for macOS, Windows and Linux.

### Render

- Iceberg MATLAB v1.0.0's render: the impulse response split at its centre time, VBAP
  for the direct sound and early reflections, first-order Ambisonics for the late
  reverberation, the room calibration of both paths, and the `dserFactor` and anechoic
  options.
- Its input handling: FuMa, AmbiX and N3D impulse responses of any order, either azimuth
  convention, a signal at another rate resampled as MATLAB's `resample`, and MATLAB's
  warnings as notices.
- Checked against reference data from Iceberg MATLAB: a 5-degree sweep in three rooms,
  layouts of 4, 8 and 24 loudspeakers and every stage of the chain, to the rounding of
  32-bit floats. Each run of the tests writes a page with every measured error against
  its limit.

### App

- Stimuli as a list of slots, each an impulse response, a signal, a source angle and a
  level (dBFS, or dB SPL with a room calibration), played one by one or in sequence. The
  signal can be WAV, AIFF, FLAC, Ogg Vorbis or MP3 (MP3 on macOS and Windows).
- Four loudspeakers at 0, 90, 180 and 270 degrees without a layout file; larger rings
  from a layout file in either of two formats (`examples/layouts`), with example
  calibrations (`examples/calibration`).
- A position pad that shows the loudspeakers and lights the pair VBAP pans on.
- Prep mode: any stimulus in stereo on headphones.
- Export of a render as a 32-bit float WAV file, one channel per loudspeaker.
- Light and dark themes, following the operating system or fixed in View > Appearance.

### Experimental

- Room calibration by measurement (Calibration > Calibrate room): the microphone factor
  with checks of the calibrator recording, each loudspeaker's response and equalisation
  (with a plot of the response before and through it), and the level alignment. It
  matches MATLAB on simulated devices and has not been run with real hardware.
- Moving sources: Live mode (the source follows the pad while it plays) and programmed
  trajectories of `time:angle` points. Held still they reproduce the static render; a
  moving source has no MATLAB counterpart.
