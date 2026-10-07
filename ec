#!/bin/sh
# The CHGame tools on this project's sketch (Git Bash / Linux): ./ec build, ./ec run ...
here="$(cd "$(dirname "$0")" && pwd)"
exec python "$here/chgame/tools/chgame.py" --sketch "$here/EthansCritters" "$@"
