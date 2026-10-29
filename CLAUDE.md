# CLAUDE.md

@AGENTS.md

## Claude Code specifics

- A hook in `.claude/settings.json` checks every `Write`/`Edit` of a C/C++/C#/XAML/`.ps1`/HLSL file with `Tests/check_conventions.py --hook`. When a hook reports a violation, fix it (`--fix` handles BOMs) instead of working around it. If a rule keeps being missed, make the check stricter rather than adding emphasis here.
- In orchestrated work the **coordinator** runs `CodexReview.ps1`, never the implementing worker (`docs/dev/ReviewAndMerge.md`).
- Run `./CleanupMerged.ps1` without being asked after merges.
- **Write edit scripts to a file, don't inline them.** A quoted heredoc passed to `python -`/`pwsh -Command` loses backslashes on the way in: Windows paths break (`\v` in `\vswhere` becomes a vertical tab), `\n` becomes a real newline so patterns stop matching, and regex character classes like `[^"\\]` fail to compile. Write the script into the scratchpad directory and run it by path. The same applies to JSON with Windows paths piped through Git Bash `echo`.
- Don't re-read a file right after writing it; prefer `sed -n '<range>p'` over reading a whole file when one function is what matters.

# Compact instructions

When compacting, keep: the pitfalls in `AGENTS.md` and this file, which issues are open and their decisions, exact build/test commands used, and any measured numbers. Drop: full build logs, file contents already written to disk, and superseded intermediate attempts.
