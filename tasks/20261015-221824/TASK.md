# Refetch channel context when eventsub fails.

- STATUS: OPEN
- PRIORITY: 180
- TAGS: bug, eventsub

When eventsub fails for any reason, we might miss some event like a streamer going live.
This can block some of the commands in the old state. It would be great to re-fetch the context from fresh each time we loose comunications (ignoring recconecting)
