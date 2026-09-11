# Fix Commands 'Corrupted double linked list'

- STATUS: OPEN
- PRIORITY: 500
- TAGS: bug,critical,commands,core-dump

`command_names` contains pointers to `command_lookup`. When `command_lookup` resizes `command_names` bugs out.
