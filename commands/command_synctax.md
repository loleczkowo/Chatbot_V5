When adding commands first make a file named `channel_name.txt`, in it include the commands with the syntax.

## Command syntax
Each line is a new "command".
A command is seperated into two elements with "^". Left side is the command "name" and right is what it returns.
For a command to have multiple "names" use `|` to split them

Lines that are empty or start with `//` are ignored
`\` works as escape parsing, If you want to type it use `\\`

The current Replacements are
- `${NICKNAME}`
- `${MESSAGE}`
- `${TIMEZONE-Europe/Warsaw}` (Fake timezone/incorrect name might crash)

some day: To overwrite a command just do "`command_name^ `"