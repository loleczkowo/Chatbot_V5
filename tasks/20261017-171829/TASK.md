# Small chanse for core-dump when shutdown

- STATUS: OPEN
- PRIORITY: 80
- TAGS: core-dump, commands

```
Exiting program . . .
disconecting twitch chat
stopping twitch eventsub
stopping oauth2 server
Segmentation fault         (core dumped) ./run
```

Happened after linking api with lua.
Probably because giving a pointer to the api and removing it.
Not a large problem because its at the end of the program.
