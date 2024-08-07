#!/bin/bash
# Copypasted from stackoverflow
make  --always-make --dry-run | grep -wE 'gcc|g\+\+' | grep -w '\-c' | jq -nR '[inputs|{directory:"'$(pwd)'", command:., file: match(" [^ ]+$").string[1:]}]' > compile_commands.json
