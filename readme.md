# CHATBOT V5
# THIS IS A LEARNING PROJECT!
**Do not expect much of it.**

Its a messy project that's only purpose was to help me learn c++ (and have some fun in the meanwhile)

Feel free to use any of the code here.


## How to use
- Create `client_secret.env` with two lines, the first one of your `client_id` and the second of the `client_secret`
- Create `config` with a shape of
```conf
nickname=bot_nickname
channel=channels,to_watch,over
chatbots=hardcoded,list_of,chatbots,to_prevent,self_feading,commands
```
(Currently channel supports only a single channel)
- Run `./run` and read logs for some instructions.

## Build
`./compile.sh` or use `make`