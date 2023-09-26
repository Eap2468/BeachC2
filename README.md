# BeachC2
C2 server for practice labs and boxes, helps with managing your sea shells all in one place!

# Usage
g++ main.cpp -o BeachC2

./BeachC2 [PORT]

# Commands
sessions: lists available sessions

use: opens to the session listed (usage: use [SESSION NUMBER] and press ctrl-c to go back the menu)

kill: kills the given session number(s) (usage: kill [SESSION NUMBER])

listen: restarts the server on a different port (usage: listen [PORT NUMBER])

rename: changes the display name of a given session (usage: rename [SESSION NUMBER] [NEW NAME (optional)])

exit: safely closes the server

help: shows the help menu
