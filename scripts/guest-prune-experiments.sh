#!/bin/bash
# guest-prune-experiments.sh: free guest C: space by removing old experiment
# directories (C:\Users\pavel\EXPnnnn), oldest first, until the target free
# space is reached.
#
# Never removed:
#   - a directory that the newest local experiment's guest scripts name
#     (hashcheck*.ps1 / sample-run*.ps1: the pinned samples live there);
#   - the newest --keep experiment directories on the guest;
#   - anything given with --protect EXPnnnn;
#   - anything that is not named EXP followed by digits.
#
# By default every directory is hashed on the guest, copied to
# .local/experiments/guest-archive/<dir> and verified before it is deleted.
# --no-archive deletes it without a copy.
#
# usage: guest-prune-experiments.sh [--target-gb 6] [--keep 3]
#        [--protect EXPnnnn]... [--no-archive] [--dry-run]
# Prints PRUNE_PLAN, then ARCHIVED/DELETED/KEPT lines and PRUNE_DONE <free GB>.
# Do not run it while a recover.sh or an experiment pipeline uses the guest.
set -u
T=/Users/pavel/public_windows/.local/tandem
REPO=$(cd "$(dirname "$0")/.." && pwd)
EXPS=/Volumes/pwdev/public_windows/.local/experiments
ARCHIVE=$EXPS/guest-archive
TARGET=6; KEEP=3; ARCHIVE_ON=1; DRY=0; PROTECT=""
while [ $# -gt 0 ]; do
  case "$1" in
    --target-gb) TARGET=$2; shift 2;;
    --keep) KEEP=$2; shift 2;;
    --protect) PROTECT="$PROTECT $2"; shift 2;;
    --no-archive) ARCHIVE_ON=0; shift;;
    --dry-run) DRY=1; shift;;
    *) echo "unknown argument $1"; exit 2;;
  esac
done
g() { perl -e 'alarm 300; exec @ARGV' "$T/airssh" -o ConnectTimeout=10 "$@" | tr -d '\r'; }
free_gb() { g 'powershell -NoProfile -Command "[int]((Get-PSDrive C).Free/1MB)"' | tail -1; }
# the pinned sample directories named by the newest local experiment's guest scripts
newest=$(ls -d "$EXPS"/EXP[0-9]*-* 2>/dev/null | sort -V | tail -1)
pinned=$(cat "$newest"/guest/hashcheck*.ps1 "$newest"/guest/sample-run*.ps1 2>/dev/null |
         grep -o 'C:\\Users\\pavel\\EXP[0-9]*' | sed 's/.*\\//' | sort -u | tr '\n' ' ')
[ -n "$pinned" ] || { echo "PRUNE_REFUSED no pinned sample list in $newest/guest"; exit 1; }
guest_dirs=$(g 'powershell -NoProfile -Command "Get-ChildItem C:\Users\pavel -Directory | Where-Object Name -match ''^EXP[0-9]+$'' | ForEach-Object Name"' |
             grep -E '^EXP[0-9]+$' | sort -V)
newest_guest=$(echo "$guest_dirs" | tail -n "$KEEP" | tr '\n' ' ')
candidates=""
for d in $guest_dirs; do
  case " $pinned $newest_guest $PROTECT " in *" $d "*) continue;; esac
  candidates="$candidates $d"
done
mb=$(free_gb); want=$((TARGET * 1024))
echo "PRUNE_PLAN free ${mb}MB target ${want}MB pinned [$pinned] newest [$newest_guest] protect [$PROTECT] candidates [$candidates] archive $ARCHIVE_ON dry $DRY"
[ "$DRY" = 1 ] && { echo "PRUNE_DONE $mb"; exit 0; }
mkdir -p "$ARCHIVE"
for d in $candidates; do
  [ "$mb" -ge "$want" ] && break
  if [ "$ARCHIVE_ON" = 1 ]; then
    mkdir -p "$ARCHIVE/$d"
    g "powershell -NoProfile -Command \"Get-ChildItem C:\\Users\\pavel\\$d -Recurse -File | ForEach-Object { (Get-FileHash \$_.FullName).Hash.ToLower() + ' ' + \$_.FullName.Substring(\\\"C:\\Users\\pavel\\$d\\\".Length+1) }\"" > "$ARCHIVE/$d.guest-sha256.txt"
    perl -e 'alarm 1800; exec @ARGV' "$T/airscp" -r "pavel@192.168.1.37:C:/Users/pavel/$d/*" "$ARCHIVE/$d/" >/dev/null 2>&1
    verdict=$(python3 - "$ARCHIVE" "$d" <<'P'
import sys,hashlib,pathlib
root,d=sys.argv[1],sys.argv[2]; bad=n=0
for line in open(f'{root}/{d}.guest-sha256.txt'):
    line=line.strip()
    if not line: continue
    h,p=line.split(' ',1); n+=1
    f=pathlib.Path(root)/d/p.replace('\\','/')
    if not f.exists() or hashlib.sha256(f.read_bytes()).hexdigest()!=h: bad+=1
print(n,bad)
P
)
    read n bad <<< "$verdict"
    if [ "$bad" != 0 ] || [ "$n" -eq 0 ]; then echo "KEPT $d files $n bad $bad"; continue; fi
    echo "ARCHIVED $d files $n bad 0"
  fi
  g "powershell -NoProfile -Command \"Remove-Item -Recurse -Force C:\\Users\\pavel\\$d\"" >/dev/null
  echo "DELETED $d"
  mb=$(free_gb)
done
echo "PRUNE_DONE $mb"
