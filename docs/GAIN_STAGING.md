# Gain staging and metering

The input GAIN control drives a preamp circuit: in microphone mode it adds up to about 40 dB.
The +20 dB switch adds further gain in the tube stage. These controls can overload the simulated
stages even when the source is a normal DAW track. The factory setup starts at minimum gain.

For added drive with a controlled listening level:

1. Set INPUT TRIM to give the preamp enough headroom for the source.
2. Adjust GAIN and PREAMP OUT for the amount of drive and the level feeding the compressor.
3. Adjust compressor OUTPUT when matching compression settings.
4. Use OUTPUT TRIM to match the final listening level. Reducing this trim lowers the signal after
   the circuit; use INPUT TRIM or the earlier gain controls to reduce overload inside the stages.

The digital main page exposes INPUT and OUTPUT TRIM alongside the existing controls. Trims use the
same saved parameters as their System-page counterparts.

## Meter sources

- Digital OUT shows independent left/right sample peaks in dBFS, after output gain and mode-switch
  fades. The numeric peak holds for one second. The bars release at 24 dB per second. Full scale is
  0 dBFS; the digital CLIP lamp detects final samples whose absolute value reaches or exceeds 1.
- PRE, COMP and EQ show the internal RMS taps in VU. Their internal calibration is unchanged.
- The analogue needle uses RMS VU metering, with the selected output alignment when OUT is chosen.
- In stereo, the single numeric/needle reading shows the louder channel. In dual mono it follows
  the selected channel tab. Digital bars still show both channels.

Meter behavior and drawing do not change the audio samples or the circuit model.
