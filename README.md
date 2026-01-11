# tinysynth

A MIDI file parser and (basic) synthesizer written in C, that enables:
- Export of MIDI files to readable JSON format
- Generation of audio WAV files from MIDI

### Usage

To compile: `make`

Run the program:
```bash
./tinysynth <input.mid> [-o output.json] [-a output.wav]
```

Options:
- `-o output.json` : Parse MIDI and write to JSON file
- `-a output.wav` : Generate audio WAV file from MIDI
- At least one option must be specified

Examples:
```bash
./tinysynth song.mid -o song.json
./tinysynth song.mid -a song.wav
./tinysynth song.mid -o song.json -a song.wav
```

### Notes

The MIDI parser is quite strict in following the MIDI standard, but it tolerates some common encoding errors found in real-world MIDI files (e.g., parameter values > 127 are masked to 7 bits). If you encounter a parsing failure with a file you believe is valid, or if you notice missing event type support, feel free to open an issue or contact me.

### Dependencies

- [miniaudio](https://github.com/mackron/miniaudio) - Single-file audio library for WAV encoding (MIT License)

---

### Credits
The `.mid` files inside `resources` folder were downloaded from [here](https://homestuck.net/music/midis/nothomestuck/Undertale/piano/)
