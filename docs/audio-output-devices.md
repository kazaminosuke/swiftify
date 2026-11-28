# Native audio output devices

The standalone engine can enumerate CPAL output handles and open a chosen
one. `switch_output(None)` opens the current system default again. An active
track is reopened at the last estimated listening position, or at its
submitted position when the old backend lacks that estimate. A paused track
stays paused. Switching invalidates the old clock revision. Explicit mute
keeps the chosen volume and advances the media clock while emitting silence.

A backend can reject a second open of an exclusive device. For a healthy old
stream, the player retains its control state if opening the replacement fails
and resumes it if the new source cannot be prepared after pausing. When the
old device is disconnected the caller can retry with another output. The
underlying OS may still buffer old audio. The engine does not yet automatically
retry a failed device, persist a device choice, or expose these controls in Qt.

The Linux ALSA null-device test covers reopening a paused Opus track,
position and generation, and volume and mute preservation. Physical
WASAPI/ALSA/PipeWire device selection and disconnect timing are unverified.

## Qt desktop recovery

The Qt local file route also forwards CPAL stream and command failures to
the existing Qt recovery state. A lost sink does not report natural end of
track. The app waits for an explicit resume before rebuilding the output.
The standalone device selector is still separate from the Qt controls.

The local renderer gates PCM consumption on the paused and lost states.
Callbacks arriving while a pause command is still pending cannot consume
a fresh seek target. The regression test drives that callback edge directly.
