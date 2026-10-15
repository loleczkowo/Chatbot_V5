# strange semi crash bug with websocket

- STATUS: OPEN
- PRIORITY: 200
- TAGS: bug,eventsub

Had a End of File error with asio, but the moment we attached to the screen with -r, it fixed itself.
I suspect its from the `async_read` in the eventsub system
