"""Developer-only generator for original regression tones; no app dependency.

Generated with Python 3.14, soundfile 0.14.0, numpy 2.5.4, lameenc 1.9.0.
Install those packages into an isolated environment before regenerating.
"""
from pathlib import Path
import math
import struct
import soundfile
import lameenc
import numpy

out = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "audio"
out.mkdir(parents=True, exist_ok=True)
rate = 44100
pcm = [(round(12000 * math.sin(2 * math.pi * 440 * n / rate)),
        round(7000 * math.sin(2 * math.pi * 660 * n / rate))) for n in range(4410)]
values = numpy.array(pcm, dtype=numpy.int16)
soundfile.write(out / "tone.flac", values, rate, format="FLAC", subtype="PCM_16")
soundfile.write(out / "tone.ogg", values.astype(numpy.float32) / 32768.0, rate,
                format="OGG", subtype="VORBIS")
encoder = lameenc.Encoder()
encoder.set_in_sample_rate(rate)
encoder.set_channels(2)
encoder.set_bit_rate(128)
encoder.set_quality(2)
raw = b"".join(struct.pack("<hh", left, right) for left, right in pcm)
(out / "tone.mp3").write_bytes(encoder.encode(raw) + encoder.flush())
print(out)
