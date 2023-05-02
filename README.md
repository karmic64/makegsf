# makegsf

`makegsf` is a tool for scriptable generation of .gsflib and .minigsf Game Boy Advance music rip files. Usage is straightforward, just `makegsf scriptfile`.

To compile this program, you need a C compiler (preferably `gcc`), `make`, zlib, and libiconv.

## How it works

This tool assumes you have a hacked GBA ROM file (that will be converted to a .gsflib) that does nothing but play music. The .gsflib contains all the music code and data, and the .minigsfs outputted by this tool contain nothing but a song ID that your gsflib code reads.

Script files are plain text files, and each line is one command. Comments are supported, and last from a `#` character until the end of the line. Scripts are expected to be encoded in UTF-8.

The next section describes the syntax and functions of each script command. Here are some general details about script syntax:
* Command names are case-insensitive.
* Square brackets indicate an optional parameter.
* `STR` represents a string which begins and ends with a `"` character. `\"` is an escaped quote and `'n` is a newline.
* `NUM` represents a numeric constant. Numbers are assumed to be decimal unless they are prefixed with `$` or `0x`, in which case they will be interpreted as hexadecimal. Negative numbers and fractional numbers are not allowed.

## Script command reference

### MultiBoot

`MultiBoot`

Signals that the .gsflib's entry point/offset should be `0x2000000` instead of `0x8000000`.

### MakeGSFLib

`MakeGSFLib STR STR`

Defines the name of the source ROM and the name of the .gsflib file. This must be defined before any attempt to create a .minigsf. When this command is encountered, the .gsflib is written and any .gsflib commands after this will have no effect. Multiple .gsflibs are not supported.

### GSFLib

`GSFLib STR`

If you don't want to use this tool to make your .gsflib, use this command to directly specify its filename.

### FilenameTemplate

`FilenameTemplate STR`

Sets a filename template that will be used to create the .minigsfs. Numeric conversion specifiers can optionally be prefixed with a number to force a specific amount of digits with leading zeros.
* `%n` is the song number. This starts at 1 and is incremented with every .minigsf written.
* `%i` is the song ID.
* `%t` is the song title.
* `%a` is the song artist.

### MiniGSFOffset

`MiniGSFOffset NUM`

Defines the offset for the .minigsf data, which, as a reminder, consists of nothing but the 32-bit song ID. A recommended value is `0x9fffffc`, since this address is unlikely to collide with important .gsflib data, and it is trivial to access, even in Thumb code:

```
mov r0,#0x0a
lsl r0,#24
sub r0,#4
ldr r0,[r0]
```

### Tag commands

`Title/Artist/Game/Date/Year/Genre/Comment/Copyright/GSFBy/Volume/Length/Fade [STR]`

These commands are all very similar, so we'll cover them all here. Tagging commands apply to all .minigsfs until another value is specified. This is especially useful for `Game`/`Date`/`Year`/`Copyright`, since these tags will generally be shared for all .minigsfs in a set. A blank/nonexistent string will result in no such tag being written.

Note that `Date` and `Year` are synonyms for the PSF tag `year`, since people tend to use it for a full date.

### Tag

`Tag STR STR`

Defines a custom tag. The arguments are the tag name and value respectively. Tag names must be valid C identifiers. Tag names beginning with an `_` character, `filedir`, `filename`, and `fileext` are reserved by the PSF specification and are not allowed.

### SetSongNumber

`SetSongNumber NUM`
Changes the current song number used by the `%n` filename conversion specifier.

### MakeMiniGSF

`MakeMiniGSF NUM [STR] [STR] [STR] [STR] [STR] [STR] [STR]`

Creates a .minigsf containing the specified song ID. The strings are optional, and change the title, artist, comment, length, fade, volume, and genre respectively. Much like tag commands, a blank string will result in the tag not being written.

### MakeMiniGSFRange

`MakeMiniGSFRange NUM NUM [NUM]`

Creates .minigsfs for an inclusive range of song IDs. The values are the start, end, and step of the range respectively. If the step is not specified, it is 1 by default. This command is useful in the testing phase when you're still trying to figure out which song IDs are valid.

## Sample script

Let's put it all together and make a basic script example.

```
# Define source ROM and output .gsflib.
MakeGSFLib "hack.gba" "tsukihime.gsflib"

# Set up .minigsf information.
MiniGSFOffset 0x9fffffc
FilenameTemplate "%2n %t.minigsf"  # The song number will have 2 digits.

# Set up global tags.
GSFBy "karmic"
Tag "tagger" "karmic"  # This tag is not in the PSF spec, but is well-used
Game "Rinne Tsukihime"
Date "2001-12-21"
Artist "MTAK"
Copyright "TYPE-MOON, Inside-cap, AIR Pocket Project"

# Write a .minigsf for each song. Non-ASCII characters are supported.
MakeMiniGSF 25 "妖夏"
MakeMiniGSF 59 "月下"
MakeMiniGSF 65 "月下(half moon ver.)"
MakeMiniGSF 93 "灯火"
MakeMiniGSF 122 "日向"
MakeMiniGSF 139 "感情"
MakeMiniGSF 196 "訣別"
MakeMiniGSF 254 "nowhere"
MakeMiniGSF 488 "幻舞"
MakeMiniGSF 555 "望郷"
```
