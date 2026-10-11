# Hardware acceptance checklist — Windows 11

All items are pending until run on real hardware. Record Windows build, GPU, driver, CPU, microphones/cameras, monitor DPI and output-file paths with each result.

- [ ] Monitor 1 and monitor 2 separately; different resolution/DPI, negative desktop coordinates, unplug a display during recording.
- [ ] Capture an overlapped window; resize it; minimize/restore; close it; open a different window after closure. The new window must never silently replace the selected source.
- [ ] System + microphone, system only, mic only, neither. Check recorded contents, not only UI switch positions. Disabling mic/camera must release those devices.
- [ ] Two applications play distinct tones; select one app and check that only its tone is present. Repeat with mic enabled and a multi-process browser. Verify there is no automatic full-system fallback.
- [ ] Start recording from a process that exits or whose PID is reused. The recording must not change to the unrelated new process.
- [ ] Every output and microphone endpoint; 44.1/48 kHz, mono/stereo/multichannel; device removal, USB reconnect and default-device changes.
- [ ] Software suppression off/on with quiet speech, fan noise and keyboard sounds. Off means unprocessed mic samples, aside from conversion to output format.
- [ ] Each webcam; all four corners; camera off; unplug camera; missing/invalid RVM model; PNG/JPEG background; moving person, hair and partial occlusion. Confirm circle and replacement in decoded MP4.
- [ ] Drag the circle repeatedly in the live preview during recording; switch corners mid-recording. Decode the MP4 and confirm continuous motion, correct pointer offset and a circle present on its first frame.
- [ ] Adjust system and mic gain independently while both are audible: 0%, 50%, 100%, 200%. Verify recorded levels and smooth transitions, including muted system with active mic and vice versa.
- [ ] Suppression strengths 0/25/50/75/100% with sustained fan noise and quiet speech. Verify 0% matches bypass, stronger settings reduce noise, speech remains intelligible.
- [ ] Background blur at low/high intensity, four solids, four gradients and all three interiors. Confirm the person stays sharp and background changes reach the recorded MP4. Run --camera-test for measured delivery rates without retaining personal camera images.
- [ ] All three themes, keyboard navigation, Russian text, 100/125/150/200% DPI, a small monitor, theme switch during recording.
- [ ] New destination, unwritable location, existing file, disk below free-space threshold, Stop during initialization, repeated start/stop, Close while recording. Keep existing recordings intact.
- [ ] 30–60 minute recording; listen and compare claps at beginning/end for drift. Measure CPU, GPU, memory, dropped/repeated frames and actual package size.
- [ ] Open completed MP4 in Windows Media Player and VLC, including silent video and odd-sized source window.
