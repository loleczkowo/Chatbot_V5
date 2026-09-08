### When adding commands to a channel first make a file named `channel_name.txt`, in it include the commands with the syntax.

# Command syntax
Each line is a new "command".

A command is separated into two elements with `^`. Left side is the command's names and options, where the right side is what it returns.

For a command to have aliases or/and options use `|` to split them.

Example, the main name is `!hello` with an alias of `!hi`:
```text
!hello|!hi^Hello!
```

Lines that are empty or start with `//` are ignored.

The current options are:
- `COOLDOWN=123`
- `LUA`
- `LUA_MESSAGE`
- `LUA_AUTHOR`
- `LUA_BADGES`
- `LUA_REPLY`

Default cooldown is 5 seconds.

Adding a cooldown to our previous example:
```text
!hello|!hi|COOLDOWN=42^Hello!
```

## LUA
Adding the `LUA` options makes the output a Lua expression instead of plain text.  
It also appends `return ` to the start of the script.

Example:
```text
!coin|LUA^RANDOM % 2 == 0 and "Heads" or "Tails"
```

(Lua commands are compiled when the command file is loaded/reloaded.)

The always available Lua values/functions are:
- `RANDOM`
- `TIME("Time/Zone")`
- `TIMESTAMP` (milliseconds)

Example:
```text
!time|LUA^"Warsaw: " .. TIME("Europe/Warsaw")
```

## LUA chat message info
Message info is stored inside the varible `MSG`.

`LUA_MESSAGE` option enables:
- `MSG.id`
- `MSG.room_id`
- `MSG.room_name`
- `MSG.message`
- `MSG.responding`
- `MSG.timestamp`
- `MSG.first_message`

Example:
```text
!echo|LUA|LUA_MESSAGE^"You said: " .. MSG.message
```

`LUA_AUTHOR` option enables:
- `MSG.author.login`
- `MSG.author.display_name`
- `MSG.author.user_id`
- `MSG.author.color`
- `MSG.author.returning_chatter`
- `MSG.author.sub`
- `MSG.author.vip`
- `MSG.author.mod`
- `MSG.author.turbo`
- `MSG.author.broadcaster`

Example:
```text
!hello|LUA|LUA_AUTHOR^"Welcome " .. MSG.author.display_name
```

`LUA_BADGES` option adds:
- `MSG.author.badges`

Where each badge contains:
- `.version`
- `.info`

Example:
```text
!sub|LUA|LUA_BADGES^MSG.author.badges.subscriber and "Subscribed :D!" or "Not subscribed smh smh"
```

`LUA_REPLY` option enables:
- `MSG.reply.parent_message_id`
- `MSG.reply.parent_user_id`
- `MSG.reply.parent_user_login`
- `MSG.reply.parent_display_name`
- `MSG.reply.parent_message`
- `MSG.reply.thread_parent_message_id`
- `MSG.reply.thread_parent_user_id`
- `MSG.reply.thread_parent_user_login`
- `MSG.reply.thread_parent_display_name`

Example:
```text
!reply|LUA|LUA_REPLY^"You were replying to " .. MSG.reply.parent_display_name
```

I reccomend to only enable the parts of `MSG` the command needs to optimise it.

For example the command:
```text
!hello|LUA|LUA_AUTHOR^"Hello " .. MSG.author.display_name
```
Does not include message/reply/badge info as it does not need it.

## Disabling a command

To disable a command simply do:
```text
command_name^
```
