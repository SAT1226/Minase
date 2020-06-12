# Minase
(Here is [Japanese version](./README_JP.md))  
Minase is terminal file manager.

![image](./screenshot00.png)

![image](./screenshot01.png)

## Features
* Preview of the selected file/directory
* Preview text syntax highlight (use Nano editor syntax highlight files)
* Preview text auto encodeing
* Preview audio tags
* Preview archive files (needs lsar or bsdtar)
* Preview image using Sixel Graphics (needs img2sixel)
* FreeDesktop compliant trash (needs trash-cli)
* Batch rename (needs vidir)
* UTF-8 support
* Fix "East Asian Ambiguous Width Characters" problem (use wcwidth-cjk)

## System Requirements
* Linux

## Dependencies
* uchardet
* iconv
* TagLib

optional:
* libsixel
* trash-cli
* vidir
* lsar or bsdtar
* cmigemo

## Usage
|Keys|Description|
| ---- | ---- |
|h, Right| Parent directory|
|j, Down| Down|
|k, Up| Up|
|l, Right| Open file/directory|
|PgUp, ^U| Scroll up|
|PgDn, ^D| Scroll down|
|H| Move to top of screen|
|M| Move to middle of screen|
|L| Move to bottom of screen|
|g| Go to first entry|
|G| Go to last entry|
|^L| Redraw|
|q| Quit|
|^G|  Quit and cd|
|0| View log|
|1| Switch tab 1|
|2| Switch tab 2|
|3| Switch tab 3|
|4| Switch tab 4|
|,| FileView simple/detail|
|.| Show/Hide dot files|
|i| Enable/Disable image preview|
|z| Current line to the middle of the screen|
|s| Sort files|
|e| Edit File|
|Space| Mark file|
|u| Clear marks|
|a| Invert marks (current directory only)|
|c| Mark files for copy|
|m| Mark files for move|
|d| Delete mark files|
|p| Paste|
|r| Rename current file|
|^R| Batch rename (vidir)|
|!| Spawn SHELL|
|n| Create file/directory|
|b| Open Bookmarks|
|/| Filter|
|^/| Change Filter type|
|x| Run Plugin |
|*| Toggle execute permission|
|Alt + key| Run Plugin |

Quit and cd:
```
 $ minase; if [ -f ~/.config/Minase/lastdir ]; then cd "`cat ~/.config/Minase/lastdir`"; rm ~/.config/Minase/lastdir; fi;
```

## Installation
```
$ cmake .
$ make
$ sudo make install
```

## Customization
~/.config/Minase/config.ini
```
[Options]
; File Opener
Opener = xdg-open

; Log view Max lines
LogMaxLines = 100

; Preview Max lines
PreViewMaxLines = 50

; Use trash-cli
UseTrash = true

; Nano Editor Syntax Highlighting Files
NanorcPath = /usr/share/nano/

; East Asian Ambiguous Width
wcwidth-cjk = false

; 0: simple / 1: detail
FileViewType = 0

; 0: name / 1: size / 2: date
SortType = 2

; 0: Ascending / 1: Descending
SortOrder = 1

; Migemo Dictionary File
;MigemoDict = /usr/share/migemo/utf-8/migemo-dict

; 0: Normal / 1: Regexp / 2: Migemo
FilterType = 0
```

~/.config/Minase/bookmarks    
```
~/Download
~/Documents
/usr/local
```

~/.config/Minase/plugin.ini    
```
[sxiv]
; Plugin Path
filepath = ~/.config/Minase/plugins/sxiv
; GUI: true / false
gui = true
; 0: NONE / 1: ChDir
operation = 0
; Shortcut Key
key = s

[autojump]
filepath = ~/.config/Minase/plugins/autojump
operation = 1
key = j
```

## License
* MIT
