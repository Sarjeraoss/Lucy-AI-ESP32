"""Convert a licensed/locally synthesized 16 kHz mono PCM16 WAV to flash mu-law.
Usage: python tools/encode_greeting.py greeting.wav offline/src/greeting_data.h
The bundled English greeting was synthesized locally with Windows Zira.
"""
import pathlib
import struct
import sys
import wave


def mulaw(sample):
    sign = 0x80 if sample < 0 else 0
    sample = min(abs(sample), 32635) + 132
    exponent = max(0, sample.bit_length() - 8)
    mantissa = (sample >> (exponent + 3)) & 15
    return (~(sign | (exponent << 4) | mantissa)) & 255


if __name__ == '__main__':
    with wave.open(sys.argv[1], 'rb') as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 16000)
        pcm = wav.readframes(wav.getnframes())
    samples = struct.unpack('<' + 'h' * (len(pcm) // 2), pcm)
    encoded = [mulaw(x) for x in samples]
    rows = [', '.join(f'0x{x:02x}' for x in encoded[i:i+24]) for i in range(0, len(encoded), 24)]
    pathlib.Path(sys.argv[2]).write_text(
        '#pragma once\n#include <stdint.h>\n// 16 kHz mono mu-law: Hi! I am your virtual friend.\n'
        'static const uint8_t GREETING_DATA[] = {\n  ' + ',\n  '.join(rows) + '\n};\n', encoding='utf-8')
    print(f'Greeting: {len(encoded)} flash bytes, {len(encoded)/16000:.2f} seconds')
