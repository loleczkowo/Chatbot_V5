# Add user IDs clean-up

- STATUS: OPEN
- PRIORITY: 200
- TAGS: feature,commands

The IDs can pile up in `user_cooldowns`. I would every ~15 minutes (up to config) remove those that are useless.
Just a basic loop over users. For each last use get the command's cooldown and compare (if old then remove), after that if user ID has nothing left remove them.
