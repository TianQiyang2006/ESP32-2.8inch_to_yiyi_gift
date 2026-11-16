The `.bin` fixture contains 16 mono IMA WAV blocks (1024 bytes each), generated
with FFmpeg from a 1-second 6 kHz sine at 32 kHz. `.pcm` is FFmpeg's decoded
signed 16-bit little-endian reference, including the final padded block.

Generation:

```
ffmpeg -f lavfi -i sine=frequency=6000:duration=1:sample_rate=32000 -c:a adpcm_ima_wav -block_size 1024 reference.wav
ffmpeg -i reference.wav -f s16le music-ima-6khz.pcm
```

`.bin` is the RIFF `data` chunk, without its chunk header. The host decoder test
compares every output sample with the reference, checks invalid block headers,
and verifies that decoding a block after a seek does not require earlier blocks.
