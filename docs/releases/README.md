# Release notes

`<tag>.md` in this folder is what a release says: it becomes the text on the GitHub release page (above the
install instructions and the generated commit list) and the message posted to Discord.

Without a file here, the release falls back to the tag's own message (`git tag -a v1.2.3 -m "what changed"`),
and without that to a plain line. Keep it to a few lines: it is read in Discord, not in a changelog.
